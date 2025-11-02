#include "glps_x11.h"
#include "glps_egl_context.h"
#include <X11/Xatom.h>
#include "utils/logger/pico_logger.h"

#define MAX_EVENTS_PER_FRAME 64
#define TARGET_FPS 60
#define NS_PER_FRAME (1000000000 / TARGET_FPS)
#define WINDOW_LOOKUP_SIZE 256

#define XC_arrow 2
#define XC_hand1 58
#define XC_crosshair 34
#define XC_right_side 96
#define XC_top_side 138
#define XC_xterm 152
#define XC_X_cursor 0

typedef struct {
    Window xid;
    size_t window_id;
} WindowLookupEntry;

static WindowLookupEntry g_window_lookup[WINDOW_LOOKUP_SIZE];
static size_t g_window_lookup_count = 0;
static Cursor g_cursor_cache[7] = {0};
static bool g_cursors_initialized = false;

static inline ssize_t __get_window_id_by_xid(const glps_WindowManager *wm, Window xid)
{
    (void)wm;
    
    for (size_t i = 0; i < g_window_lookup_count; ++i) {
        if (g_window_lookup[i].xid == xid) {
            return (ssize_t)g_window_lookup[i].window_id;
        }
    }
    return -1;
}

static void __add_window_lookup(Window xid, size_t window_id)
{
    if (g_window_lookup_count < WINDOW_LOOKUP_SIZE) {
        g_window_lookup[g_window_lookup_count].xid = xid;
        g_window_lookup[g_window_lookup_count].window_id = window_id;
        g_window_lookup_count++;
    }
}

static void __remove_window_lookup(Window xid)
{
    for (size_t i = 0; i < g_window_lookup_count; ++i) {
        if (g_window_lookup[i].xid == xid) {
            g_window_lookup[i] = g_window_lookup[g_window_lookup_count - 1];
            g_window_lookup_count--;
            break;
        }
    }
}

static void __cleanup_window_lookup(void)
{
    g_window_lookup_count = 0;
}

static bool __initialize_cursors(Display* display)
{
    if (g_cursors_initialized) return true;
    
    int cursor_ids[] = {XC_arrow, XC_xterm, XC_crosshair, XC_hand1, 
                       XC_right_side, XC_top_side, XC_X_cursor};
    
    for (int i = 0; i < 7; i++) {
        g_cursor_cache[i] = XCreateFontCursor(display, cursor_ids[i]);
        if (g_cursor_cache[i] == None) {
            return false;
        }
    }
    
    g_cursors_initialized = true;
    return true;
}

static void __cleanup_cursors(Display* display)
{
    if (!g_cursors_initialized) return;
    
    for (int i = 0; i < 7; i++) {
        if (g_cursor_cache[i] != None) {
            XFreeCursor(display, g_cursor_cache[i]);
        }
    }
    g_cursors_initialized = false;
}

void __remove_window(glps_WindowManager *wm, Window xid)
{
    ssize_t window_id = __get_window_id_by_xid(wm, xid);
    if (window_id < 0) {
        return;
    }

    glps_X11Window* window = wm->windows[window_id];
    
    if (window->egl_surface != EGL_NO_SURFACE && wm->egl_ctx != NULL) {
        eglDestroySurface(wm->egl_ctx->dpy, window->egl_surface);
        window->egl_surface = EGL_NO_SURFACE;
    }

    if (wm->x11_ctx != NULL && wm->x11_ctx->display != NULL) {
        XDestroyWindow(wm->x11_ctx->display, window->window);
    }

    __remove_window_lookup(xid);
    free(window);
    wm->windows[window_id] = NULL;

    size_t last_index = wm->window_count - 1;
    if (window_id < last_index) {
        wm->windows[window_id] = wm->windows[last_index];
        if (wm->windows[window_id] != NULL) {
            __remove_window_lookup(wm->windows[window_id]->window);
            __add_window_lookup(wm->windows[window_id]->window, window_id);
        }
        wm->windows[last_index] = NULL;
    }
    
    wm->window_count--;
}

void glps_x11_init(glps_WindowManager *wm)
{
    if (wm == NULL) {
        LOG_CRITICAL("Window Manager is NULL");
        exit(EXIT_FAILURE);
    }

    wm->x11_ctx = calloc(1, sizeof(glps_X11Context));
    if (wm->x11_ctx == NULL) {
        LOG_CRITICAL("Failed to allocate X11 context");
        exit(EXIT_FAILURE);
    }

    wm->windows = calloc(MAX_WINDOWS, sizeof(glps_X11Window*));
    if (wm->windows == NULL) {
        LOG_CRITICAL("Failed to allocate windows array");
        free(wm->x11_ctx);
        exit(EXIT_FAILURE);
    }

    wm->x11_ctx->display = XOpenDisplay(NULL);
    if (!wm->x11_ctx->display) {
        LOG_CRITICAL("Failed to open X display");
        free(wm->windows);
        free(wm->x11_ctx);
        exit(EXIT_FAILURE);
    }

    if (!__initialize_cursors(wm->x11_ctx->display)) {
        LOG_CRITICAL("Failed to initialize cursors");
        XCloseDisplay(wm->x11_ctx->display);
        free(wm->windows);
        free(wm->x11_ctx);
        exit(EXIT_FAILURE);
    }

    wm->x11_ctx->cursor = g_cursor_cache[0];

    wm->x11_ctx->font = XLoadQueryFont(wm->x11_ctx->display, "fixed");
    if (!wm->x11_ctx->font) {
        LOG_CRITICAL("Failed to load system font");
        __cleanup_cursors(wm->x11_ctx->display);
        XCloseDisplay(wm->x11_ctx->display);
        free(wm->windows);
        free(wm->x11_ctx);
        exit(EXIT_FAILURE);
    }

    wm->x11_ctx->wm_delete_window = XInternAtom(wm->x11_ctx->display, "WM_DELETE_WINDOW", False);
}

ssize_t glps_x11_window_create(glps_WindowManager *wm, const char *title,
                               int width, int height)
{
    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL) {
        LOG_CRITICAL("Failed to create X11 window");
        exit(EXIT_FAILURE);
    }

    if (wm->window_count >= MAX_WINDOWS) {
        LOG_ERROR("Maximum number of windows reached");
        return -1;
    }

    size_t window_index = wm->window_count;
    wm->windows[window_index] = calloc(1, sizeof(glps_X11Window));
    if (wm->windows[window_index] == NULL) {
        LOG_ERROR("Failed to allocate window");
        return -1;
    }

    int screen = DefaultScreen(wm->x11_ctx->display);
    Window window = XCreateSimpleWindow(
        wm->x11_ctx->display,
        RootWindow(wm->x11_ctx->display, screen),
        10, 10, width, height, 1,
        BlackPixel(wm->x11_ctx->display, screen),
        WhitePixel(wm->x11_ctx->display, screen));

    if (window == 0) {
        LOG_ERROR("Failed to create X11 window");
        free(wm->windows[window_index]);
        wm->windows[window_index] = NULL;
        return -1;
    }

    wm->windows[window_index]->window = window;
    wm->windows[window_index]->fps_start_time = (struct timespec){0};
    wm->windows[window_index]->fps_is_init = false;

    XSetWindowBackground(wm->x11_ctx->display, window, 0xFFFFFF);
    
    XSetWindowAttributes swa;
    swa.backing_store = WhenMapped;
    XChangeWindowAttributes(wm->x11_ctx->display, window, CWBackingStore, &swa);
    
    XStoreName(wm->x11_ctx->display, window, title);

    if (wm->x11_ctx->gc == NULL) {
        wm->x11_ctx->gc = XCreateGC(wm->x11_ctx->display, window, 0, NULL);
        if (wm->x11_ctx->gc == NULL) {
            LOG_ERROR("Failed to create graphics context");
            XDestroyWindow(wm->x11_ctx->display, window);
            free(wm->windows[window_index]);
            wm->windows[window_index] = NULL;
            return -1;
        }
    }

    XSetWMProtocols(wm->x11_ctx->display, window, &wm->x11_ctx->wm_delete_window, 1);

    long event_mask = PointerMotionMask | ButtonPressMask | ButtonReleaseMask | 
                     KeyPressMask | KeyReleaseMask | StructureNotifyMask | ExposureMask;

    if (XSelectInput(wm->x11_ctx->display, window, event_mask) == BadWindow) {
        LOG_ERROR("Failed to select input events");
        XDestroyWindow(wm->x11_ctx->display, window);
        free(wm->windows[window_index]);
        wm->windows[window_index] = NULL;
        return -1;
    }

    if (wm->egl_ctx != NULL) {
        EGLSurface egl_surface = eglCreateWindowSurface(wm->egl_ctx->dpy, wm->egl_ctx->conf,
                                                       (NativeWindowType)window, NULL);
        if (egl_surface == EGL_NO_SURFACE) {
            LOG_ERROR("Failed to create EGL surface");
            XDestroyWindow(wm->x11_ctx->display, window);
            free(wm->windows[window_index]);
            wm->windows[window_index] = NULL;
            return -1;
        }
        wm->windows[window_index]->egl_surface = egl_surface;
    }

    if (window_index == 0 && wm->egl_ctx == NULL) {
        glps_egl_create_ctx(wm);
    }

    if (wm->egl_ctx != NULL) {
        glps_egl_make_ctx_current(wm, window_index);
    }

    XMapWindow(wm->x11_ctx->display, window);
    XFlush(wm->x11_ctx->display);

    __add_window_lookup(window, window_index);
    wm->window_count++;

    return window_index;
}

void glps_x11_toggle_window_decorations(glps_WindowManager *wm, bool state, size_t window_id)
{
    if (wm == NULL || wm->x11_ctx == NULL || window_id >= wm->window_count) {
        return;
    }

    Atom motif_hints = XInternAtom(wm->x11_ctx->display, "_MOTIF_WM_HINTS", False);
    if (motif_hints != None) {
        struct {
            unsigned long flags;
            unsigned long functions;
            unsigned long decorations;
            long input_mode;
            unsigned long status;
        } hints = {2, 0, state ? 1 : 0, 0, 0};

        XChangeProperty(wm->x11_ctx->display, wm->windows[window_id]->window, 
                       motif_hints, motif_hints, 32, PropModeReplace, 
                       (unsigned char*)&hints, 5);
    }

    Atom net_wm_window_type = XInternAtom(wm->x11_ctx->display, "_NET_WM_WINDOW_TYPE", False);
    Atom window_type = state ? 
        XInternAtom(wm->x11_ctx->display, "_NET_WM_WINDOW_TYPE_NORMAL", False) : 
        XInternAtom(wm->x11_ctx->display, "_NET_WM_WINDOW_TYPE_DOCK", False);

    if (net_wm_window_type != None && window_type != None) {
        XChangeProperty(wm->x11_ctx->display, wm->windows[window_id]->window, 
                       net_wm_window_type, XA_ATOM, 32, PropModeReplace, 
                       (unsigned char*)&window_type, 1);
    }

    XFlush(wm->x11_ctx->display);
}

bool glps_x11_should_close(glps_WindowManager *wm)
{
    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL) {
        return true;
    }

    Display *display = wm->x11_ctx->display;
    XEvent event;

    int events_processed = 0;
    while (XPending(display) > 0 && events_processed++ < MAX_EVENTS_PER_FRAME) {
        XNextEvent(display, &event);

        ssize_t window_id = __get_window_id_by_xid(wm, event.xany.window);
        if (window_id < 0) {
            continue;
        }

        switch (event.type) {
        case ClientMessage:
            if ((Atom)event.xclient.data.l[0] == wm->x11_ctx->wm_delete_window) {
                if (wm->callbacks.window_close_callback) {
                    wm->callbacks.window_close_callback((size_t)window_id, wm->callbacks.window_close_data);
                }
                __remove_window(wm, event.xclient.window);
                return (wm->window_count == 0);
            }
            break;

        case DestroyNotify:
            if (wm->callbacks.window_close_callback) {
                wm->callbacks.window_close_callback((size_t)window_id, wm->callbacks.window_close_data);
            }
            __remove_window(wm, event.xdestroywindow.window);
            return (wm->window_count == 0);

        case ConfigureNotify:
            if (wm->callbacks.window_resize_callback) {
                wm->callbacks.window_resize_callback((size_t)window_id, event.xconfigure.width,
                                                    event.xconfigure.height, wm->callbacks.window_resize_data);
            }
            break;

        case MotionNotify:
            if (wm->callbacks.mouse_move_callback) {
                wm->callbacks.mouse_move_callback((size_t)window_id, event.xmotion.x,
                                                 event.xmotion.y, wm->callbacks.mouse_move_data);
            }
            XDefineCursor(display, wm->windows[window_id]->window, wm->x11_ctx->cursor);
            break;

        case ButtonPress:
            switch (event.xbutton.button) {
            case 4:
                if (wm->callbacks.mouse_scroll_callback) {
                    wm->callbacks.mouse_scroll_callback((size_t)window_id, GLPS_SCROLL_V_AXIS,
                                                       GLPS_SCROLL_SOURCE_WHEEL, 1.0f, 1.0f,
                                                       false, wm->callbacks.mouse_scroll_data);
                }
                break;
            case 5:
                if (wm->callbacks.mouse_scroll_callback) {
                    wm->callbacks.mouse_scroll_callback((size_t)window_id, GLPS_SCROLL_V_AXIS,
                                                       GLPS_SCROLL_SOURCE_WHEEL, -1.0f, -1.0f,
                                                       false, wm->callbacks.mouse_scroll_data);
                }
                break;
            case 6:
                if (wm->callbacks.mouse_scroll_callback) {
                    wm->callbacks.mouse_scroll_callback((size_t)window_id, GLPS_SCROLL_H_AXIS,
                                                       GLPS_SCROLL_SOURCE_WHEEL, -1.0f, -1.0f,
                                                       false, wm->callbacks.mouse_scroll_data);
                }
                break;
            case 7:
                if (wm->callbacks.mouse_scroll_callback) {
                    wm->callbacks.mouse_scroll_callback((size_t)window_id, GLPS_SCROLL_H_AXIS,
                                                       GLPS_SCROLL_SOURCE_WHEEL, 1.0f, 1.0f,
                                                       false, wm->callbacks.mouse_scroll_data);
                }
                break;
            default:
                if (wm->callbacks.mouse_click_callback) {
                    wm->callbacks.mouse_click_callback((size_t)window_id, true, wm->callbacks.mouse_click_data);
                }
                break;
            }
            break;

        case ButtonRelease:
            switch (event.xbutton.button) {
            case 4: case 5: case 6: case 7:
                break;
            default:
                if (wm->callbacks.mouse_click_callback) {
                    wm->callbacks.mouse_click_callback((size_t)window_id, false, wm->callbacks.mouse_click_data);
                }
                break;
            }
            break;

        case KeyPress:
            if (wm->callbacks.keyboard_callback) {
                char buf[32];
                KeySym keysym;
                XLookupString(&event.xkey, buf, sizeof(buf), &keysym, NULL);
                KeyCode keycode = XKeysymToKeycode(display, keysym);
                if (keycode != 0) {
                    wm->callbacks.keyboard_callback((size_t)window_id, true, buf, keycode, wm->callbacks.keyboard_data);
                }
            }
            break;

        case KeyRelease:
            if (wm->callbacks.keyboard_callback) {
                char buf[32];
                KeySym keysym;
                XLookupString(&event.xkey, buf, sizeof(buf), &keysym, NULL);
                KeyCode keycode = XKeysymToKeycode(display, keysym);
                if (keycode != 0) {
                    wm->callbacks.keyboard_callback((size_t)window_id, false, buf, keycode, wm->callbacks.keyboard_data);
                }
            }
            break;

        case Expose:
            if (wm->callbacks.window_frame_update_callback) {
                wm->callbacks.window_frame_update_callback((size_t)window_id, wm->callbacks.window_frame_update_data);
            }
            break;

        default:
            break;
        }
    }

    return (wm->window_count == 0);
}

void glps_x11_window_update(glps_WindowManager *wm, size_t window_id)
{
    if (wm == NULL || wm->x11_ctx == NULL || window_id >= wm->window_count || 
        !wm->callbacks.window_frame_update_callback) {
        return;
    }

    static struct timespec last_time;
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    if (last_time.tv_sec != 0 || last_time.tv_nsec != 0) {
        long elapsed_ns = (current_time.tv_sec - last_time.tv_sec) * 1000000000 +
                         (current_time.tv_nsec - last_time.tv_nsec);

        if (elapsed_ns < NS_PER_FRAME) {
            struct timespec sleep_time = {0, NS_PER_FRAME - elapsed_ns};
            nanosleep(&sleep_time, NULL);
        }
    }
    last_time = current_time;

    wm->callbacks.window_frame_update_callback(window_id, wm->callbacks.window_frame_update_data);
    XFlush(wm->x11_ctx->display);
}

void glps_x11_destroy(glps_WindowManager *wm)
{
    if (wm == NULL) {
        return;
    }

    if (wm->windows) {
        for (size_t i = 0; i < wm->window_count; ++i) {
            if (wm->windows[i] != NULL) {
                if (wm->windows[i]->egl_surface != EGL_NO_SURFACE && wm->egl_ctx != NULL) {
                    eglDestroySurface(wm->egl_ctx->dpy, wm->windows[i]->egl_surface);
                }
                if (wm->windows[i]->window && wm->x11_ctx != NULL && wm->x11_ctx->display != NULL) {
                    XDestroyWindow(wm->x11_ctx->display, wm->windows[i]->window);
                }
                free(wm->windows[i]);
            }
        }
        free(wm->windows);
        wm->windows = NULL;
        wm->window_count = 0;
    }

    __cleanup_window_lookup();

    if (wm->x11_ctx) {
        if (wm->x11_ctx->display) {
            __cleanup_cursors(wm->x11_ctx->display);
            if (wm->x11_ctx->font) {
                XFreeFont(wm->x11_ctx->display, wm->x11_ctx->font);
            }
            if (wm->x11_ctx->gc) {
                XFreeGC(wm->x11_ctx->display, wm->x11_ctx->gc);
            }
            XCloseDisplay(wm->x11_ctx->display);
        }
        free(wm->x11_ctx);
        wm->x11_ctx = NULL;
    }

    if (wm->egl_ctx) {
        glps_egl_destroy(wm);
    }
}

void glps_x11_get_window_dimensions(glps_WindowManager *wm, size_t window_id,
                                    int *width, int *height)
{
    if (wm == NULL || wm->x11_ctx == NULL || window_id >= wm->window_count ||
        width == NULL || height == NULL) {
        return;
    }

    Window root;
    int x, y;
    unsigned int border_width, depth;
    XGetGeometry(wm->x11_ctx->display, wm->windows[window_id]->window, &root,
                 &x, &y, (unsigned int *)width, (unsigned int *)height,
                 &border_width, &depth);
}

void glps_x11_window_is_resizable(glps_WindowManager *wm, bool state, size_t window_id)
{
    if (wm == NULL || wm->x11_ctx == NULL || window_id >= wm->window_count) {
        return;
    }

    Display *display = wm->x11_ctx->display;
    Window win = wm->windows[window_id]->window;

    Window root;
    int x, y;
    unsigned int width, height, border_width, depth;
    if (!XGetGeometry(display, win, &root, &x, &y, &width, &height, &border_width, &depth)) {
        return;
    }

    XSizeHints *size_hints = XAllocSizeHints();
    if (size_hints == NULL) {
        return;
    }

    long supplied_return;
    XGetWMNormalHints(display, win, size_hints, &supplied_return);

    if (state) {
        size_hints->flags &= ~(PMinSize | PMaxSize);
        size_hints->min_width = 1;
        size_hints->min_height = 1;
        size_hints->max_width = INT_MAX;
        size_hints->max_height = INT_MAX;
        size_hints->flags |= PResizeInc;
        size_hints->width_inc = 1;
        size_hints->height_inc = 1;
    } else {
        size_hints->flags |= PMinSize | PMaxSize;
        size_hints->min_width = width;
        size_hints->min_height = height;
        size_hints->max_width = width;
        size_hints->max_height = height;
    }

    XSetWMNormalHints(display, win, size_hints);
    XFree(size_hints);
    XFlush(display);
}

void glps_x11_attach_to_clipboard(glps_WindowManager *wm, char *mime, char *data)
{
    (void)wm;
    (void)mime;
    (void)data;
}

void glps_x11_get_from_clipboard(glps_WindowManager *wm, char *data, size_t data_size)
{
    (void)wm;
    (void)data;
    (void)data_size;
}

void glps_x11_cursor_change(glps_WindowManager *wm, GLPS_CURSOR_TYPE user_cursor)
{
    if (!wm || !wm->x11_ctx || !g_cursors_initialized) {
        return;
    }

    if (user_cursor >= 0 && user_cursor < 7) {
        wm->x11_ctx->cursor = g_cursor_cache[user_cursor];
    }
}