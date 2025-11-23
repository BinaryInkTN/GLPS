#include "glps_x11.h"
#include "glps_egl_context.h"
#include <X11/Xatom.h>
#include "utils/logger/pico_logger.h"

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
    for (size_t i = 0; i < wm->window_count; ++i)
    {
        if (wm->windows[i]->window == xid)
        {
            return i;
        }
    }
    return -1;
}

void __remove_window(glps_WindowManager *wm, Window xid)
{
    ssize_t window_id = __get_window_id_by_xid(wm, xid);
    if (window_id < 0) return;

    if (wm->windows[window_id]->egl_surface != EGL_NO_SURFACE)
    {
        eglDestroySurface(wm->egl_ctx->dpy, wm->windows[window_id]->egl_surface);
    }

    XDestroyWindow(wm->x11_ctx->display, wm->windows[window_id]->window);
    free(wm->windows[window_id]);

    for (size_t i = window_id; i < wm->window_count - 1; i++)
    {
        wm->windows[i] = wm->windows[i + 1];
    }
    wm->windows[wm->window_count - 1] = NULL;
    wm->window_count--;
}

void glps_x11_init(glps_WindowManager *wm)
{
    wm->x11_ctx = calloc(1, sizeof(glps_X11Context));
    wm->windows = calloc(MAX_WINDOWS, sizeof(glps_X11Window *));

    wm->x11_ctx->display = XOpenDisplay(NULL);
    wm->x11_ctx->font = XLoadQueryFont(wm->x11_ctx->display, "fixed");
    wm->x11_ctx->wm_delete_window = XInternAtom(wm->x11_ctx->display, "WM_DELETE_WINDOW", False);
    wm->window_count = 0;
}

ssize_t glps_x11_window_create(glps_WindowManager *wm, const char *title,
                               int width, int height)
{
    if (wm->window_count >= MAX_WINDOWS) return -1;

    int screen = DefaultScreen(wm->x11_ctx->display);
    size_t window_index = wm->window_count;
    
    wm->windows[window_index] = calloc(1, sizeof(glps_X11Window));
    wm->windows[window_index]->fps_start_time = (struct timespec){0};
    wm->windows[window_index]->fps_is_init = false;

    wm->windows[window_index]->window = XCreateSimpleWindow(
        wm->x11_ctx->display,
        RootWindow(wm->x11_ctx->display, screen),
        10, 10, width, height, 1,
        BlackPixel(wm->x11_ctx->display, screen),
        WhitePixel(wm->x11_ctx->display, screen));

    XSetWindowBackground(wm->x11_ctx->display, wm->windows[window_index]->window, 0xFFFFFF);
    XStoreName(wm->x11_ctx->display, wm->windows[window_index]->window, title);

    if (wm->x11_ctx->gc == NULL)
    {
        wm->x11_ctx->gc = XCreateGC(wm->x11_ctx->display, wm->windows[window_index]->window, 0, NULL);
    }

    XSetWMProtocols(wm->x11_ctx->display, wm->windows[window_index]->window,
                    &wm->x11_ctx->wm_delete_window, 1);

    long event_mask = PointerMotionMask | ButtonPressMask | ButtonReleaseMask |
                      KeyPressMask | KeyReleaseMask | StructureNotifyMask | ExposureMask;

    XSelectInput(wm->x11_ctx->display, wm->windows[window_index]->window, event_mask);

    if (wm->egl_ctx != NULL)
    {
        wm->windows[window_index]->egl_surface =
            eglCreateWindowSurface(wm->egl_ctx->dpy, wm->egl_ctx->conf,
                                   (NativeWindowType)wm->windows[window_index]->window, NULL);
    }

    if (window_index == 0 && wm->egl_ctx == NULL)
    {
        glps_egl_create_ctx(wm);
    }

    if (wm->egl_ctx != NULL)
    {
        glps_egl_make_ctx_current(wm, window_index);
    }

    XMapWindow(wm->x11_ctx->display, wm->windows[window_index]->window);
    XFlush(wm->x11_ctx->display);

    wm->window_count++;
    return window_index;
}

void glps_x11_toggle_window_decorations(glps_WindowManager *wm, bool state, size_t window_id)
{
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
        hints.flags = 2;
        hints.decorations = state ? 1 : 0;
        hints.functions = 0;
        hints.input_mode = 0;
        hints.status = 0;

        XChangeProperty(wm->x11_ctx->display, wm->windows[window_id]->window, motif_hints, motif_hints, 32,
                        PropModeReplace, (unsigned char *)&hints, 5);
    }

    XFlush(wm->x11_ctx->display);
}

bool glps_x11_should_close(glps_WindowManager *wm)
{
    Display *display = wm->x11_ctx->display;
    XEvent event;

    int events_processed = 0;
    while (XPending(display) > 0 && events_processed++ < MAX_EVENTS_PER_FRAME)
    {
        XNextEvent(display, &event);

        ssize_t window_id = __get_window_id_by_xid(wm, event.xany.window);
        if (window_id < 0) continue;

        switch (event.type)
        {
        case ClientMessage:
            if ((Atom)event.xclient.data.l[0] == wm->x11_ctx->wm_delete_window)
            {
                if (wm->callbacks.window_close_callback)
                {
                    wm->callbacks.window_close_callback((size_t)window_id, wm->callbacks.window_close_data);
                }
                __remove_window(wm, event.xclient.window);
                break;
            }
            break;

        case DestroyNotify:
            if (wm->callbacks.window_close_callback)
            {
                wm->callbacks.window_close_callback((size_t)window_id, wm->callbacks.window_close_data);
            }
            __remove_window(wm, event.xdestroywindow.window);
            break;

        case ConfigureNotify:
            if (wm->callbacks.window_resize_callback)
            {
                wm->callbacks.window_resize_callback((size_t)window_id, event.xconfigure.width, event.xconfigure.height, wm->callbacks.window_resize_data);
            }
            break;

        case MotionNotify:
            if (wm->callbacks.mouse_move_callback)
            {
                wm->callbacks.mouse_move_callback((size_t)window_id, event.xmotion.x, event.xmotion.y, wm->callbacks.mouse_move_data);
            }
            if (wm->x11_ctx->cursor)
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
                    wm->callbacks.mouse_scroll_callback((size_t)window_id, GLPS_SCROLL_V_AXIS, GLPS_SCROLL_SOURCE_WHEEL, 1.0f, 1.0f, false, wm->callbacks.mouse_scroll_data);
                }
                break;
            case 5:
                if (wm->callbacks.mouse_scroll_callback)
                {
                    wm->callbacks.mouse_scroll_callback((size_t)window_id, GLPS_SCROLL_V_AXIS, GLPS_SCROLL_SOURCE_WHEEL, -1.0f, -1.0f, false, wm->callbacks.mouse_scroll_data);
                }
                break;
            case 6:
                if (wm->callbacks.mouse_scroll_callback)
                {
                    wm->callbacks.mouse_scroll_callback((size_t)window_id, GLPS_SCROLL_H_AXIS, GLPS_SCROLL_SOURCE_WHEEL, -1.0f, -1.0f, false, wm->callbacks.mouse_scroll_data);
                }
                break;
            case 7:
                if (wm->callbacks.mouse_scroll_callback)
                {
                    wm->callbacks.mouse_scroll_callback((size_t)window_id, GLPS_SCROLL_H_AXIS, GLPS_SCROLL_SOURCE_WHEEL, 1.0f, 1.0f, false, wm->callbacks.mouse_scroll_data);
                }
                break;
            default:
                if (wm->callbacks.mouse_click_callback)
                {
                    wm->callbacks.mouse_click_callback((size_t)window_id, true, wm->callbacks.mouse_click_data);
                }
                break;
            }
            break;

        case ButtonRelease:
            switch (event.xbutton.button)
            {
            case 4: case 5: case 6: case 7:
                break;
            default:
                if (wm->callbacks.mouse_click_callback)
                {
                    wm->callbacks.mouse_click_callback((size_t)window_id, false, wm->callbacks.mouse_click_data);
                }
                break;
            }
            break;

        case KeyPress:
            if (wm->callbacks.keyboard_callback)
            {
                char buf[32];
                KeySym keysym;
                XLookupString(&event.xkey, buf, sizeof(buf), &keysym, NULL);
                KeyCode keycode = XKeysymToKeycode(wm->x11_ctx->display, keysym);
                wm->callbacks.keyboard_callback((size_t)window_id, true, buf, keycode, wm->callbacks.keyboard_data);
            }
            break;

        case KeyRelease:
            if (wm->callbacks.keyboard_callback)
            {
                char buf[32];
                KeySym keysym;
                XLookupString(&event.xkey, buf, sizeof(buf), &keysym, NULL);
                KeyCode keycode = XKeysymToKeycode(wm->x11_ctx->display, keysym);
                wm->callbacks.keyboard_callback((size_t)window_id, false, buf, keycode, wm->callbacks.keyboard_data);
            }
            break;

        case Expose:
            if (wm->callbacks.window_frame_update_callback)
            {
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
    if (!wm->callbacks.window_frame_update_callback) return;

    static struct timespec last_time;
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    if (last_time.tv_sec != 0 || last_time.tv_nsec != 0)
    {
        long elapsed_ns = (current_time.tv_sec - last_time.tv_sec) * 1000000000 + (current_time.tv_nsec - last_time.tv_nsec);
        if (elapsed_ns < NS_PER_FRAME)
        {
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
    for (size_t i = 0; i < wm->window_count; ++i)
    {
        if (wm->windows[i]->egl_surface != EGL_NO_SURFACE)
        {
            eglDestroySurface(wm->egl_ctx->dpy, wm->windows[i]->egl_surface);
        }
        XDestroyWindow(wm->x11_ctx->display, wm->windows[i]->window);
        free(wm->windows[i]);
    }
    free(wm->windows);

    if (wm->x11_ctx->font) XFreeFont(wm->x11_ctx->display, wm->x11_ctx->font);
    if (wm->x11_ctx->gc) XFreeGC(wm->x11_ctx->display, wm->x11_ctx->gc);
    if (wm->x11_ctx->cursor) XFreeCursor(wm->x11_ctx->display, wm->x11_ctx->cursor);
    XCloseDisplay(wm->x11_ctx->display);
    free(wm->x11_ctx);

    if (wm->egl_ctx) glps_egl_destroy(wm);
}

void glps_x11_get_window_dimensions(glps_WindowManager *wm, size_t window_id, int *width, int *height)
{
    Window root;
    int x, y;
    unsigned int border_width, depth;
    XGetGeometry(wm->x11_ctx->display, wm->windows[window_id]->window, &root,
                 &x, &y, (unsigned int *)width, (unsigned int *)height,
                 &border_width, &depth);
}

void glps_x11_window_is_resizable(glps_WindowManager *wm, bool state, size_t window_id)
{
    Display *display = wm->x11_ctx->display;
    Window win = wm->windows[window_id]->window;

    XSizeHints *size_hints = XAllocSizeHints();
    long supplied_return;
    XGetWMNormalHints(display, win, size_hints, &supplied_return);

    if (state)
    {
        size_hints->flags &= ~(PMinSize | PMaxSize);
        size_hints->min_width = 1;
        size_hints->min_height = 1;
        size_hints->max_width = INT_MAX;
        size_hints->max_height = INT_MAX;
    }
    else
    {
        size_hints->flags |= PMinSize | PMaxSize;
        Window root;
        int x, y;
        unsigned int width, height, border_width, depth;
        XGetGeometry(display, win, &root, &x, &y, &width, &height, &border_width, &depth);
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
    (void)wm; (void)mime; (void)data;
}

void glps_x11_get_from_clipboard(glps_WindowManager *wm, char *data, size_t data_size)
{
    (void)wm; (void)data; (void)data_size;
}

void glps_x11_cursor_change(glps_WindowManager *wm, GLPS_CURSOR_TYPE user_cursor)
{
    int selected_cursor;

    switch (user_cursor)
    {
    case GLPS_CURSOR_ARROW: selected_cursor = XC_arrow; break;
    case GLPS_CURSOR_IBEAM: selected_cursor = XC_xterm; break;
    case GLPS_CURSOR_CROSSHAIR: selected_cursor = XC_crosshair; break;
    case GLPS_CURSOR_HAND: selected_cursor = XC_hand1; break;
    case GLPS_CURSOR_HRESIZE: selected_cursor = XC_right_side; break;
    case GLPS_CURSOR_VRESIZE: selected_cursor = XC_top_side; break;
    case GLPS_CURSOR_NOT_ALLOWED: selected_cursor = XC_X_cursor; break;
    default: selected_cursor = -1;
    }

    if (selected_cursor >= 0)
    {
        wm->x11_ctx->cursor = XCreateFontCursor(wm->x11_ctx->display, (unsigned int)selected_cursor);
    }
}

void glps_x11_set_window_blur(glps_WindowManager *wm, size_t window_id, bool enable, int blur_radius)
{
    Display *display = wm->x11_ctx->display;
    Window window = wm->windows[window_id]->window;

    Atom atom_blur = XInternAtom(display, "_KDE_NET_WM_BLUR_BEHIND_REGION", False);
    if (atom_blur != None)
    {
        if (enable)
        {
            unsigned long value = 1;
            XChangeProperty(display, window, atom_blur, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&value, 1);
        }
        else
        {
            XDeleteProperty(display, window, atom_blur);
        }
    }
    XFlush(display);
}

void glps_x11_set_window_opacity(glps_WindowManager *wm, size_t window_id, float opacity)
{
    Display *display = wm->x11_ctx->display;
    Window window = wm->windows[window_id]->window;

    Atom atom_opacity = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);
    if (atom_opacity != None)
    {
        if (opacity < 0.0f) opacity = 0.0f;
        if (opacity > 1.0f) opacity = 1.0f;
        unsigned long opacity_value = (unsigned long)(opacity * 0xFFFFFFFF);
        XChangeProperty(display, window, atom_opacity, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&opacity_value, 1);
    }
    XFlush(display);
}

void glps_x11_set_window_background_transparent(glps_WindowManager *wm, size_t window_id)
{
    Display *display = wm->x11_ctx->display;
    Window window = wm->windows[window_id]->window;

    XWindowAttributes window_attrs;
    XGetWindowAttributes(display, window, &window_attrs);

    if (window_attrs.depth == 32)
    {
        XSetWindowAttributes attrs;
        attrs.background_pixmap = None;
        XChangeWindowAttributes(display, window, CWBackPixmap, &attrs);
    }
    XFlush(display);
}

bool glps_x11_create_window_with_visual(glps_WindowManager *wm, const char *title, int width, int height, bool transparent)
{
    if (wm->window_count >= MAX_WINDOWS) return false;

    Display *display = wm->x11_ctx->display;
    int screen = DefaultScreen(display);

    XVisualInfo visual_template;
    visual_template.depth = 32;
    visual_template.class = TrueColor;

    int num_visuals;
    XVisualInfo *visual_list = XGetVisualInfo(display, VisualDepthMask | VisualClassMask, &visual_template, &num_visuals);

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
    }

    XSetWindowAttributes attrs;
    attrs.colormap = colormap;
    attrs.background_pixmap = None;
    attrs.border_pixel = 0;
    attrs.event_mask = PointerMotionMask | ButtonPressMask | ButtonReleaseMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask | ExposureMask;

    unsigned long attrs_mask = CWColormap | CWBackPixmap | CWBorderPixel | CWEventMask;

    if (!transparent)
    {
        attrs.background_pixel = WhitePixel(display, screen);
        attrs_mask |= CWBackPixel;
    }

    Window window = XCreateWindow(display, RootWindow(display, screen), 10, 10, width, height, 1, depth, InputOutput, visual, attrs_mask, &attrs);

    size_t window_index = wm->window_count;
    wm->windows[window_index] = calloc(1, sizeof(glps_X11Window));
    wm->windows[window_index]->window = window;
    wm->windows[window_index]->fps_start_time = (struct timespec){0};
    wm->windows[window_index]->fps_is_init = false;

    XStoreName(display, window, title);
    XSetWMProtocols(display, window, &wm->x11_ctx->wm_delete_window, 1);

    if (wm->egl_ctx != NULL)
    {
        wm->windows[window_index]->egl_surface = eglCreateWindowSurface(wm->egl_ctx->dpy, wm->egl_ctx->conf, (NativeWindowType)window, NULL);
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
    return wm->x11_ctx->display;
}

#ifdef GLPS_USE_VULKAN
void glps_x11_vk_create_surface(glps_WindowManager *wm, size_t window_id, VkInstance *instance, VkSurfaceKHR *surface)
{
    Display *xdisplay = wm->x11_ctx->display;
    Window xwindow = wm->windows[window_id]->window;

    VkXlibSurfaceCreateInfoKHR surface_info = {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = xdisplay,
        .window = xwindow};

    vkCreateXlibSurfaceKHR(*instance, &surface_info, NULL, surface);
}
#endif