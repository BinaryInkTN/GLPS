#include "glps_x11.h"
#include "glps_egl_context.h"
#include <X11/Xatom.h>
#include "utils/logger/pico_logger.h"
#include <string.h>

#define MAX_EVENTS_PER_FRAME 10
#define TARGET_FPS 60
#define NS_PER_FRAME (1000000000 / TARGET_FPS)

#define XC_arrow 2
#define XC_hand1 58
#define XC_crosshair 34
#define XC_right_side 96
#define XC_top_side 138
#define XC_xterm 152
#define XC_X_cursor 0

static ssize_t __get_window_id_by_xid(glps_WindowManager *wm, Window xid)
{
    if (wm == NULL || wm->windows == NULL)
    {
        return -1;
    }

    for (size_t i = 0; i < wm->window_count; ++i)
    {
        if (wm->windows[i] != NULL && wm->windows[i]->window == xid)
        {
            return i;
        }
    }

    return -1;
}

void __remove_window(glps_WindowManager *wm, Window xid)
{
    if (wm == NULL || wm->windows == NULL)
    {
        return;
    }

    ssize_t window_id = __get_window_id_by_xid(wm, xid);
    if (window_id < 0)
    {
        return;
    }

    // Notify application about window removal
    if (wm->callbacks.window_close_callback)
    {
        wm->callbacks.window_close_callback(
            (size_t)window_id,
            wm->callbacks.window_close_data);
    }

    // Clean up EGL surface
    if (wm->windows[window_id] != NULL && 
        wm->windows[window_id]->egl_surface != EGL_NO_SURFACE && 
        wm->egl_ctx != NULL && wm->egl_ctx->dpy != EGL_NO_DISPLAY)
    {
        eglDestroySurface(wm->egl_ctx->dpy, wm->windows[window_id]->egl_surface);
        wm->windows[window_id]->egl_surface = EGL_NO_SURFACE;
    }

    // Clean up X11 window
    if (wm->windows[window_id] != NULL && 
        wm->x11_ctx != NULL && wm->x11_ctx->display != NULL)
    {
        XDestroyWindow(wm->x11_ctx->display, wm->windows[window_id]->window);
    }

    free(wm->windows[window_id]);
    wm->windows[window_id] = NULL;

    // Compact the windows array
    for (size_t i = window_id; i < wm->window_count - 1; i++)
    {
        wm->windows[i] = wm->windows[i + 1];
    }
    wm->windows[wm->window_count - 1] = NULL;
    wm->window_count--;
}

void glps_x11_init(glps_WindowManager *wm)
{
    if (wm == NULL)
    {
        LOG_CRITICAL("Window Manager is NULL. Exiting..");
        exit(EXIT_FAILURE);
    }

    // Initialize all pointers to NULL
    wm->x11_ctx = NULL;
    wm->windows = NULL;
    wm->egl_ctx = NULL;
    wm->window_count = 0;
    
    // Initialize callbacks to NULL
    memset(&wm->callbacks, 0, sizeof(wm->callbacks));

    wm->x11_ctx = (glps_X11Context *)calloc(1, sizeof(glps_X11Context));
    if (wm->x11_ctx == NULL)
    {
        LOG_CRITICAL("Failed to allocate X11 context");
        exit(EXIT_FAILURE);
    }

    // Initialize X11 context
    wm->x11_ctx->display = NULL;
    wm->x11_ctx->font = NULL;
    wm->x11_ctx->gc = NULL;
    wm->x11_ctx->cursor = None;
    wm->x11_ctx->wm_delete_window = None;

    wm->windows = (glps_X11Window **)calloc(MAX_WINDOWS, sizeof(glps_X11Window *));
    if (wm->windows == NULL)
    {
        LOG_CRITICAL("Failed to allocate windows array");
        free(wm->x11_ctx);
        wm->x11_ctx = NULL;
        exit(EXIT_FAILURE);
    }

    // Initialize all window pointers to NULL
    for (int i = 0; i < MAX_WINDOWS; i++) {
        wm->windows[i] = NULL;
    }

    wm->x11_ctx->display = XOpenDisplay(NULL);
    if (!wm->x11_ctx->display)
    {
        LOG_CRITICAL("Failed to open X display\n");
        free(wm->windows);
        wm->windows = NULL;
        free(wm->x11_ctx);
        wm->x11_ctx = NULL;
        exit(EXIT_FAILURE);
    }

    wm->x11_ctx->font = XLoadQueryFont(wm->x11_ctx->display, "fixed");
    if (!wm->x11_ctx->font)
    {
        LOG_WARNING("Failed to load fixed font, trying 6x13");
        wm->x11_ctx->font = XLoadQueryFont(wm->x11_ctx->display, "6x13");
        if (!wm->x11_ctx->font)
        {
            LOG_WARNING("Failed to load 6x13 font, using default");
            wm->x11_ctx->font = XLoadQueryFont(wm->x11_ctx->display, "fixed");
        }
    }

    wm->x11_ctx->wm_delete_window = XInternAtom(wm->x11_ctx->display, "WM_DELETE_WINDOW", False);
    
    LOG_INFO("X11 initialized successfully");
}

ssize_t glps_x11_window_create(glps_WindowManager *wm, const char *title,
                               int width, int height)
{
    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL)
    {
        LOG_CRITICAL("Failed to create X11 window. Window manager and/or Display NULL.");
        return -1;
    }

    if (wm->window_count >= MAX_WINDOWS)
    {
        LOG_ERROR("Maximum number of windows reached");
        return -1;
    }

    LOG_INFO("Creating window %zu: %s (%dx%d)", wm->window_count, title, width, height);

    int screen = DefaultScreen(wm->x11_ctx->display);
    wm->windows[wm->window_count] = (glps_X11Window *)calloc(1, sizeof(glps_X11Window));
    if (wm->windows[wm->window_count] == NULL)
    {
        LOG_ERROR("Failed to allocate window");
        return -1;
    }
    
    // Initialize window structure
    wm->windows[wm->window_count]->window = 0;
    wm->windows[wm->window_count]->egl_surface = EGL_NO_SURFACE;
    wm->windows[wm->window_count]->fps_start_time = (struct timespec){0};
    wm->windows[wm->window_count]->fps_is_init = false;

    // Create the X11 window
    Window window = XCreateSimpleWindow(
        wm->x11_ctx->display,
        RootWindow(wm->x11_ctx->display, screen),
        10, 10, width, height, 1,
        BlackPixel(wm->x11_ctx->display, screen),
        WhitePixel(wm->x11_ctx->display, screen));

    if (window == 0)
    {
        LOG_ERROR("Failed to create X11 window");
        free(wm->windows[wm->window_count]);
        wm->windows[wm->window_count] = NULL;
        return -1;
    }

    wm->windows[wm->window_count]->window = window;

    // Set window properties
    XSetWindowBackground(wm->x11_ctx->display, window, 0xFFFFFF);
    
    XSetWindowAttributes swa;
    swa.backing_store = WhenMapped;
    XChangeWindowAttributes(wm->x11_ctx->display, window, CWBackingStore, &swa);
    
    XStoreName(wm->x11_ctx->display, window, title);

    // Create GC for this specific window
    GC gc = XCreateGC(wm->x11_ctx->display, window, 0, NULL);
    if (gc == NULL)
    {
        LOG_ERROR("Failed to create graphics context");
        XDestroyWindow(wm->x11_ctx->display, window);
        free(wm->windows[wm->window_count]);
        wm->windows[wm->window_count] = NULL;
        return -1;
    }

    // Store GC in window-specific structure if available, or use shared one
    // For now, we'll use the shared GC but this could be improved
    if (wm->x11_ctx->gc == NULL) {
        wm->x11_ctx->gc = gc;
    } else {
        XFreeGC(wm->x11_ctx->display, gc); // Use existing GC
    }

    // Set WM protocols
    XSetWMProtocols(wm->x11_ctx->display, window,
                    &wm->x11_ctx->wm_delete_window, 1);

    // Select input events
    long event_mask =
        PointerMotionMask |
        ButtonPressMask |
        ButtonReleaseMask |
        KeyPressMask |
        KeyReleaseMask |
        StructureNotifyMask |
        ExposureMask;

    int result = XSelectInput(wm->x11_ctx->display, window, event_mask);
    if (result == BadWindow)
    {
        LOG_ERROR("Failed to select input events");
        XDestroyWindow(wm->x11_ctx->display, window);
        free(wm->windows[wm->window_count]);
        wm->windows[wm->window_count] = NULL;
        return -1;
    }

    // Create EGL context for first window
    if (wm->window_count == 0)
    {
        if (wm->egl_ctx == NULL) {
            LOG_INFO("Creating EGL context for first window");
            if (!glps_egl_create_ctx(wm)) {
                LOG_ERROR("Failed to create EGL context");
                XDestroyWindow(wm->x11_ctx->display, window);
                free(wm->windows[wm->window_count]);
                wm->windows[wm->window_count] = NULL;
                return -1;
            }
        }
    }

    // Create EGL surface
    if (wm->egl_ctx != NULL && wm->egl_ctx->dpy != EGL_NO_DISPLAY)
    {
        EGLSurface egl_surface = eglCreateWindowSurface(wm->egl_ctx->dpy, wm->egl_ctx->conf,
                                                       (NativeWindowType)window, NULL);
        if (egl_surface == EGL_NO_SURFACE)
        {
            LOG_ERROR("Failed to create EGL surface");
            XDestroyWindow(wm->x11_ctx->display, window);
            free(wm->windows[wm->window_count]);
            wm->windows[wm->window_count] = NULL;
            return -1;
        }
        wm->windows[wm->window_count]->egl_surface = egl_surface;
        
        // Make context current for this window
        if (!glps_egl_make_ctx_current(wm, wm->window_count)) {
            LOG_WARNING("Failed to make EGL context current for window %zu", wm->window_count);
        }
    }

    // Map the window
    XMapWindow(wm->x11_ctx->display, window);
    XFlush(wm->x11_ctx->display);

    LOG_INFO("Window %zu created successfully", wm->window_count);
    return wm->window_count++;
}

void glps_x11_toggle_window_decorations(glps_WindowManager *wm, bool state, size_t window_id)
{
    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL ||
        window_id >= wm->window_count || wm->windows[window_id] == NULL)
    {
        LOG_ERROR("Invalid parameters for toggle_window_decorations");
        return;
    }

    Atom motif_hints = XInternAtom(wm->x11_ctx->display, "_MOTIF_WM_HINTS", False);

    if (motif_hints != None)
    {
        typedef struct
        {
            unsigned long flags;
            unsigned long functions;
            unsigned long decorations;
            long input_mode;
            unsigned long status;
        } MotifWmHints;

        MotifWmHints hints;
        hints.flags = 2;                   // MWM_HINTS_DECORATIONS flag
        hints.decorations = state ? 1 : 0; // 1=enable, 0=disable
        hints.functions = 0;
        hints.input_mode = 0;
        hints.status = 0;

        XChangeProperty(wm->x11_ctx->display, wm->windows[window_id]->window, motif_hints, motif_hints, 32,
                        PropModeReplace, (unsigned char *)&hints, 5);
    }

    Atom net_wm_window_type = XInternAtom(wm->x11_ctx->display, "_NET_WM_WINDOW_TYPE", False);
    Atom window_type = state ? XInternAtom(wm->x11_ctx->display, "_NET_WM_WINDOW_TYPE_NORMAL", False) : XInternAtom(wm->x11_ctx->display, "_NET_WM_WINDOW_TYPE_DOCK", False);

    if (net_wm_window_type != None && window_type != None)
    {
        XChangeProperty(wm->x11_ctx->display, wm->windows[window_id]->window, net_wm_window_type, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)&window_type, 1);
    }

    XFlush(wm->x11_ctx->display);
    XSync(wm->x11_ctx->display, False);
}

bool glps_x11_should_close(glps_WindowManager *wm)
{
    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL)
    {
        LOG_CRITICAL("Invalid Window Manager state. Exiting...");
        return true;
    }

    Display *display = wm->x11_ctx->display;
    XEvent event;

    int events_processed = 0;
    while (XPending(display) > 0 && events_processed++ < MAX_EVENTS_PER_FRAME)
    {
        XNextEvent(display, &event);

        ssize_t window_id = __get_window_id_by_xid(wm, event.xany.window);
        if (window_id < 0)
        {
            // Event for untracked window - might be a system window, ignore
            continue;
        }

        switch (event.type)
        {
        case ClientMessage:
            if ((Atom)event.xclient.data.l[0] == wm->x11_ctx->wm_delete_window)
            {
                LOG_INFO("Window close request for window %zd", window_id);
                __remove_window(wm, event.xclient.window);
                return (wm->window_count == 0);
            }
            break;

        case DestroyNotify:
            LOG_INFO("Window %zd destroyed", window_id);
            __remove_window(wm, event.xdestroywindow.window);
            return (wm->window_count == 0);

        case ConfigureNotify:
            if (wm->callbacks.window_resize_callback)
            {
                wm->callbacks.window_resize_callback(
                    (size_t)window_id,
                    event.xconfigure.width,
                    event.xconfigure.height,
                    wm->callbacks.window_resize_data);
            }
            break;

        case MotionNotify:
            if (wm->callbacks.mouse_move_callback)
            {
                wm->callbacks.mouse_move_callback(
                    (size_t)window_id,
                    event.xmotion.x,
                    event.xmotion.y,
                    wm->callbacks.mouse_move_data);
            }
            if (wm->x11_ctx->cursor != None)
            {
                XDefineCursor(wm->x11_ctx->display, wm->windows[window_id]->window, wm->x11_ctx->cursor);
            }
            break;

        case ButtonPress:
            switch (event.xbutton.button)
            {
            case 4:
                if (wm->callbacks.mouse_scroll_callback)
                {
                    wm->callbacks.mouse_scroll_callback((size_t)window_id, GLPS_SCROLL_V_AXIS,
                                                        GLPS_SCROLL_SOURCE_WHEEL, 1.0f, 1.0f,
                                                        false, wm->callbacks.mouse_scroll_data);
                }
                break;
            case 5:
                if (wm->callbacks.mouse_scroll_callback)
                {
                    wm->callbacks.mouse_scroll_callback((size_t)window_id, GLPS_SCROLL_V_AXIS,
                                                        GLPS_SCROLL_SOURCE_WHEEL, -1.0f, -1.0f,
                                                        false, wm->callbacks.mouse_scroll_data);
                }
                break;
            case 6:
                if (wm->callbacks.mouse_scroll_callback)
                {
                    wm->callbacks.mouse_scroll_callback((size_t)window_id, GLPS_SCROLL_H_AXIS,
                                                        GLPS_SCROLL_SOURCE_WHEEL, -1.0f, -1.0f,
                                                        false, wm->callbacks.mouse_scroll_data);
                }
                break;
            case 7:
                if (wm->callbacks.mouse_scroll_callback)
                {
                    wm->callbacks.mouse_scroll_callback((size_t)window_id, GLPS_SCROLL_H_AXIS,
                                                        GLPS_SCROLL_SOURCE_WHEEL, 1.0f, 1.0f,
                                                        false, wm->callbacks.mouse_scroll_data);
                }
                break;
            default:
                if (wm->callbacks.mouse_click_callback)
                {
                    wm->callbacks.mouse_click_callback(
                        (size_t)window_id,
                        true,
                        wm->callbacks.mouse_click_data);
                }
                break;
            }
            break;

        case ButtonRelease:
            switch (event.xbutton.button)
            {
            case 4:
            case 5:
            case 6:
            case 7:
                break;
            default:
                if (wm->callbacks.mouse_click_callback)
                {
                    wm->callbacks.mouse_click_callback(
                        (size_t)window_id,
                        false,
                        wm->callbacks.mouse_click_data);
                }
                break;
            }
            break;

        case KeyPress:
            if (wm->callbacks.keyboard_callback)
            {
                char buf[32] = {0};
                KeySym keysym;
                XLookupString(&event.xkey, buf, sizeof(buf), &keysym, NULL);
                KeyCode keycode = XKeysymToKeycode(wm->x11_ctx->display, keysym);
                if (keycode == 0)
                {
                    LOG_ERROR("Keycode not found for keysym %lu", keysym);
                    break;
                }
                wm->callbacks.keyboard_callback(
                    (size_t)window_id,
                    true,
                    buf,
                    keycode,
                    wm->callbacks.keyboard_data);
            }
            break;

        case KeyRelease:
            if (wm->callbacks.keyboard_callback)
            {
                char buf[32] = {0};
                KeySym keysym;

                XLookupString(&event.xkey, buf, sizeof(buf), &keysym, NULL);
                KeyCode keycode = XKeysymToKeycode(wm->x11_ctx->display, keysym);
                if (keycode == 0)
                {
                    LOG_ERROR("Keycode not found for keysym %lu", keysym);
                    break;
                }

                wm->callbacks.keyboard_callback(
                    (size_t)window_id,
                    false,
                    buf,
                    keycode,
                    wm->callbacks.keyboard_data);
            }
            break;

        case Expose:
            if (wm->callbacks.window_frame_update_callback)
            {
                wm->callbacks.window_frame_update_callback(
                    (size_t)window_id,
                    wm->callbacks.window_frame_update_data);
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
    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL ||
        window_id >= wm->window_count || wm->windows[window_id] == NULL)
    {
        LOG_ERROR("Invalid parameters for window update");
        return;
    }

    if (!wm->callbacks.window_frame_update_callback)
    {
        return;
    }

    static struct timespec last_time = {0, 0};
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    if (last_time.tv_sec != 0 || last_time.tv_nsec != 0)
    {
        long elapsed_ns = (current_time.tv_sec - last_time.tv_sec) * 1000000000 +
                          (current_time.tv_nsec - last_time.tv_nsec);

        if (elapsed_ns < NS_PER_FRAME)
        {
            struct timespec sleep_time = {
                0,
                NS_PER_FRAME - elapsed_ns};
            nanosleep(&sleep_time, NULL);
        }
    }
    last_time = current_time;

    wm->callbacks.window_frame_update_callback(
        window_id,
        wm->callbacks.window_frame_update_data);

    XFlush(wm->x11_ctx->display);
}

void glps_x11_destroy(glps_WindowManager *wm)
{
    if (wm == NULL)
    {
        return;
    }

    if (wm->windows)
    {
        for (size_t i = 0; i < wm->window_count; ++i)
        {
            if (wm->windows[i] != NULL)
            {
                if (wm->windows[i]->egl_surface != EGL_NO_SURFACE && wm->egl_ctx != NULL && wm->egl_ctx->dpy != EGL_NO_DISPLAY)
                {
                    eglDestroySurface(wm->egl_ctx->dpy, wm->windows[i]->egl_surface);
                }
                if (wm->windows[i]->window && wm->x11_ctx != NULL && wm->x11_ctx->display != NULL)
                {
                    XDestroyWindow(wm->x11_ctx->display, wm->windows[i]->window);
                }
                free(wm->windows[i]);
                wm->windows[i] = NULL;
            }
        }
        free(wm->windows);
        wm->windows = NULL;
        wm->window_count = 0;
    }

    if (wm->x11_ctx)
    {
        if (wm->x11_ctx->font && wm->x11_ctx->display)
        {
            XFreeFont(wm->x11_ctx->display, wm->x11_ctx->font);
        }
        if (wm->x11_ctx->gc && wm->x11_ctx->display)
        {
            XFreeGC(wm->x11_ctx->display, wm->x11_ctx->gc);
        }
        if (wm->x11_ctx->cursor != None && wm->x11_ctx->display)
        {
            XFreeCursor(wm->x11_ctx->display, wm->x11_ctx->cursor);
        }
        if (wm->x11_ctx->display)
        {
            XCloseDisplay(wm->x11_ctx->display);
        }
        free(wm->x11_ctx);
        wm->x11_ctx = NULL;
    }

    if (wm->egl_ctx)
    {
        glps_egl_destroy(wm);
        wm->egl_ctx = NULL;
    }
}

void glps_x11_get_window_dimensions(glps_WindowManager *wm, size_t window_id,
                                    int *width, int *height)
{
    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL ||
        window_id >= wm->window_count || wm->windows[window_id] == NULL ||
        width == NULL || height == NULL)
    {
        LOG_ERROR("Invalid parameters for get_window_dimensions");
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
    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL ||
        window_id >= wm->window_count || wm->windows[window_id] == NULL)
    {
        LOG_ERROR("Invalid parameters for window_is_resizable");
        return;
    }

    Display *display = wm->x11_ctx->display;
    Window win = wm->windows[window_id]->window;

    Window root;
    int x, y;
    unsigned int width, height, border_width, depth;
    if (!XGetGeometry(display, win, &root, &x, &y, &width, &height, &border_width, &depth))
    {
        LOG_ERROR("Failed to get window geometry");
        return;
    }

    XSizeHints *size_hints = XAllocSizeHints();
    if (size_hints == NULL)
    {
        LOG_ERROR("Failed to allocate size hints");
        return;
    }

    long supplied_return;
    XGetWMNormalHints(display, win, size_hints, &supplied_return);

    if (state)
    {
        size_hints->flags &= ~(PMinSize | PMaxSize);
        size_hints->min_width = 1;
        size_hints->min_height = 1;
        size_hints->max_width = INT_MAX;
        size_hints->max_height = INT_MAX;
        size_hints->flags |= PResizeInc;
        size_hints->width_inc = 1;
        size_hints->height_inc = 1;
    }
    else
    {
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

void glps_x11_attach_to_clipboard(glps_WindowManager *wm, char *mime,
                                  char *data)
{
    (void)wm;
    (void)mime;
    (void)data;
}

void glps_x11_get_from_clipboard(glps_WindowManager *wm, char *data,
                                 size_t data_size)
{
    (void)wm;
    (void)data;
    (void)data_size;
}

void glps_x11_cursor_change(glps_WindowManager *wm, GLPS_CURSOR_TYPE user_cursor)
{
    if (!wm || !wm->x11_ctx || !wm->x11_ctx->display)
    {
        LOG_ERROR("Window manager invalid. Couldn't change cursor.");
        return;
    }

    int selected_cursor;

    switch (user_cursor)
    {
    case GLPS_CURSOR_ARROW:
    {
        selected_cursor = XC_arrow;
        break;
    }
    case GLPS_CURSOR_IBEAM:
        selected_cursor = XC_xterm;
        break;
    case GLPS_CURSOR_CROSSHAIR:
        selected_cursor = XC_crosshair;
        break;
    case GLPS_CURSOR_HAND:
        selected_cursor = XC_hand1;
        break;
    case GLPS_CURSOR_HRESIZE:
        selected_cursor = XC_right_side;
        break;
    case GLPS_CURSOR_VRESIZE:
        selected_cursor = XC_top_side;
        break;
    case GLPS_CURSOR_NOT_ALLOWED:
        selected_cursor = XC_X_cursor;
        break;
    default:
        selected_cursor = -1;
    }

    if (selected_cursor < 0)
    {
        LOG_ERROR("Unknown cursor type.");
        return;
    }

    // Free previous cursor if it exists
    if (wm->x11_ctx->cursor != None)
    {
        XFreeCursor(wm->x11_ctx->display, wm->x11_ctx->cursor);
    }

    wm->x11_ctx->cursor = XCreateFontCursor(wm->x11_ctx->display, (unsigned int)selected_cursor);

    LOG_INFO("Cursor updated.");
}

void glps_x11_set_window_blur(glps_WindowManager *wm, size_t window_id, bool enable, int blur_radius)
{
    if (wm == NULL || wm->x11_ctx == NULL || window_id >= wm->window_count || wm->windows[window_id] == NULL)
    {
        return;
    }

    Display *display = wm->x11_ctx->display;
    Window window = wm->windows[window_id]->window;

    Atom atom_blur = XInternAtom(display, "_KDE_NET_WM_BLUR_BEHIND_REGION", False);
    if (atom_blur != None)
    {
        if (enable)
        {
            unsigned long value = 1;
            XChangeProperty(display, window, atom_blur, XA_CARDINAL, 32,
                            PropModeReplace, (unsigned char *)&value, 1);
        }
        else
        {
            XDeleteProperty(display, window, atom_blur);
        }
    }

    Atom atom_mutter_blur = XInternAtom(display, "_MUFFIN_BLUR_REGION", False);
    if (atom_mutter_blur == None)
    {
        atom_mutter_blur = XInternAtom(display, "_MUTTER_BLUR_REGION", False);
    }

    if (atom_mutter_blur != None)
    {
        if (enable)
        {
            long blur_data[4] = {0, 0, 0, 0};
            int width, height;
            glps_x11_get_window_dimensions(wm, window_id, &width, &height);
            blur_data[2] = width;
            blur_data[3] = height;

            XChangeProperty(display, window, atom_mutter_blur, XA_CARDINAL, 32,
                            PropModeReplace, (unsigned char *)blur_data, 4);
        }
        else
        {
            XDeleteProperty(display, window, atom_mutter_blur);
        }
    }

    XFlush(display);
}

void glps_x11_set_window_opacity(glps_WindowManager *wm, size_t window_id, float opacity)
{
    if (wm == NULL || wm->x11_ctx == NULL || window_id >= wm->window_count || wm->windows[window_id] == NULL)
    {
        return;
    }

    Display *display = wm->x11_ctx->display;
    Window window = wm->windows[window_id]->window;

    Atom atom_opacity = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);
    if (atom_opacity != None)
    {
        if (opacity < 0.0f)
            opacity = 0.0f;
        if (opacity > 1.0f)
            opacity = 1.0f;

        unsigned long opacity_value = (unsigned long)(opacity * 0xFFFFFFFF);
        XChangeProperty(display, window, atom_opacity, XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char *)&opacity_value, 1);
    }

    XFlush(display);
}

void glps_x11_set_window_background_transparent(glps_WindowManager *wm, size_t window_id)
{
    if (wm == NULL || wm->x11_ctx == NULL || window_id >= wm->window_count || wm->windows[window_id] == NULL)
    {
        return;
    }

    Display *display = wm->x11_ctx->display;
    Window window = wm->windows[window_id]->window;

    XWindowAttributes window_attrs;
    if (!XGetWindowAttributes(display, window, &window_attrs))
    {
        return;
    }

    if (window_attrs.depth == 32)
    {
        XSetWindowAttributes attrs;
        attrs.background_pixmap = None;

        Status status = XChangeWindowAttributes(display, window, CWBackPixmap, &attrs);
        if (status == 0)
        {
            LOG_ERROR("Failed to set window background to transparent");
        }
    }
    else
    {
        LOG_WARNING("Window depth %d doesn't support transparency. Need 32-bit depth.", window_attrs.depth);
    }

    XFlush(display);
}

bool glps_x11_create_window_with_visual(glps_WindowManager *wm, const char *title,
                                        int width, int height, bool transparent)
{
    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL)
    {
        return false;
    }

    if (wm->window_count >= MAX_WINDOWS)
    {
        LOG_ERROR("Maximum number of windows reached");
        return false;
    }

    Display *display = wm->x11_ctx->display;
    int screen = DefaultScreen(display);

    XVisualInfo visual_template;
    visual_template.depth = 32;
    visual_template.class = TrueColor;

    int num_visuals;
    XVisualInfo *visual_list = XGetVisualInfo(display, VisualDepthMask | VisualClassMask,
                                              &visual_template, &num_visuals);

    Visual *visual = NULL;
    int depth = 0;
    Colormap colormap = None;

    if (visual_list != NULL && num_visuals > 0 && transparent)
    {
        visual = visual_list[0].visual;
        depth = visual_list[0].depth;
        colormap = XCreateColormap(display, RootWindow(display, screen), visual, AllocNone);
        XFree(visual_list);
    }
    else
    {
        visual = DefaultVisual(display, screen);
        depth = DefaultDepth(display, screen);
        colormap = DefaultColormap(display, screen);
        if (transparent)
        {
            LOG_WARNING("Transparent window requested but no 32-bit visual available");
        }
    }

    XSetWindowAttributes attrs;
    attrs.colormap = colormap;
    attrs.background_pixmap = None;
    attrs.border_pixel = 0;
    attrs.event_mask = PointerMotionMask | ButtonPressMask | ButtonReleaseMask |
                       KeyPressMask | KeyReleaseMask | StructureNotifyMask | ExposureMask;

    unsigned long attrs_mask = CWColormap | CWBackPixmap | CWBorderPixel | CWEventMask;

    if (!transparent)
    {
        attrs.background_pixel = WhitePixel(display, screen);
        attrs_mask |= CWBackPixel;
    }

    Window window = XCreateWindow(display, RootWindow(display, screen),
                                  10, 10, width, height, 1,
                                  depth, InputOutput, visual,
                                  attrs_mask, &attrs);

    if (window == 0)
    {
        LOG_ERROR("Failed to create X11 window");
        if (colormap != None && colormap != DefaultColormap(display, screen))
        {
            XFreeColormap(display, colormap);
        }
        return false;
    }

    size_t window_index = wm->window_count;
    wm->windows[window_index] = calloc(1, sizeof(glps_X11Window));
    if (wm->windows[window_index] == NULL)
    {
        LOG_ERROR("Failed to allocate window");
        XDestroyWindow(display, window);
        if (colormap != None && colormap != DefaultColormap(display, screen))
        {
            XFreeColormap(display, colormap);
        }
        return false;
    }

    wm->windows[window_index]->window = window;
    wm->windows[window_index]->fps_start_time = (struct timespec){0};
    wm->windows[window_index]->fps_is_init = false;
    wm->windows[window_index]->egl_surface = EGL_NO_SURFACE;

    XStoreName(display, window, title);
    XSetWMProtocols(display, window, &wm->x11_ctx->wm_delete_window, 1);

    if (wm->egl_ctx != NULL)
    {
        EGLSurface egl_surface = eglCreateWindowSurface(wm->egl_ctx->dpy, wm->egl_ctx->conf,
                                                        (NativeWindowType)window, NULL);
        if (egl_surface == EGL_NO_SURFACE)
        {
            LOG_ERROR("Failed to create EGL surface");
            XDestroyWindow(display, window);
            free(wm->windows[window_index]);
            wm->windows[window_index] = NULL;
            if (colormap != None && colormap != DefaultColormap(display, screen))
            {
                XFreeColormap(display, colormap);
            }
            return false;
        }
        wm->windows[window_index]->egl_surface = egl_surface;
    }

    if (window_index == 0 && wm->egl_ctx == NULL)
    {
        glps_egl_create_ctx(wm);
    }

    if (wm->egl_ctx != NULL)
    {
        glps_egl_make_ctx_current(wm, window_index);
    }

    XMapWindow(display, window);
    XFlush(display);

    wm->window_count++;

    return true;
}

Display *glps_x11_get_display(glps_WindowManager *wm)
{
    if (!wm || !wm->x11_ctx)
        return NULL;

    return wm->x11_ctx->display;
}

#ifdef GLPS_USE_VULKAN

void glps_x11_vk_create_surface(glps_WindowManager *wm, size_t window_id, VkInstance *instance, VkSurfaceKHR *surface)
{
    if (!wm || !wm->x11_ctx || window_id >= wm->window_count || !instance || !surface)
    {
        LOG_ERROR("Invalid parameters for Vulkan surface creation");
        return;
    }

    Display *xdisplay = wm->x11_ctx->display;
    Window xwindow = wm->windows[window_id]->window;

    VkXlibSurfaceCreateInfoKHR surface_info = {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .pNext = NULL,
        .flags = 0,
        .dpy = xdisplay,
        .window = xwindow};

    VkResult result = vkCreateXlibSurfaceKHR(*instance, &surface_info, NULL, surface);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create Vulkan Xlib surface: %d", result);
    }
}

#endif