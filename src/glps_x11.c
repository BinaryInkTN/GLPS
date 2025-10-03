#include "glps_x11.h"
#include "glps_egl_context.h"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
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
    ssize_t window_id = __get_window_id_by_xid(wm, xid);
    if (window_id < 0)
    {
        return;
    }

    if (wm->windows[window_id]->egl_surface != EGL_NO_SURFACE && wm->egl_ctx != NULL)
    {
        eglDestroySurface(wm->egl_ctx->dpy, wm->windows[window_id]->egl_surface);
        wm->windows[window_id]->egl_surface = EGL_NO_SURFACE;
    }

    if (wm->x11_ctx != NULL && wm->x11_ctx->display != NULL)
    {
        XDestroyWindow(wm->x11_ctx->display, wm->windows[window_id]->window);
    }

    free(wm->windows[window_id]);
    wm->windows[window_id] = NULL;

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

    wm->x11_ctx = (glps_X11Context *)calloc(1, sizeof(glps_X11Context));
    if (wm->x11_ctx == NULL)
    {
        LOG_CRITICAL("Failed to allocate X11 context");
        exit(EXIT_FAILURE);
    }

    wm->windows = (glps_X11Window **)calloc(MAX_WINDOWS, sizeof(glps_X11Window *));
    if (wm->windows == NULL)
    {
        LOG_CRITICAL("Failed to allocate windows array");
        free(wm->x11_ctx);
        exit(EXIT_FAILURE);
    }

    wm->x11_ctx->display = XOpenDisplay(NULL);
    if (!wm->x11_ctx->display)
    {
        LOG_CRITICAL("Failed to open X display\n");
        free(wm->windows);
        free(wm->x11_ctx);
        exit(EXIT_FAILURE);
    }

    wm->x11_ctx->root = RootWindow(wm->x11_ctx->display, DefaultScreen(wm->x11_ctx->display));

    wm->x11_ctx->frame = 0;

    XSelectInput(wm->x11_ctx->display, wm->x11_ctx->root,
                 SubstructureRedirectMask | SubstructureNotifyMask |
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                     KeyPressMask | KeyReleaseMask | StructureNotifyMask);

    wm->x11_ctx->font = XLoadQueryFont(wm->x11_ctx->display, "fixed");
    if (!wm->x11_ctx->font)
    {
        LOG_WARNING("Failed to load fixed font, trying alternatives");
        wm->x11_ctx->font = XLoadQueryFont(wm->x11_ctx->display, "9x15");
        if (!wm->x11_ctx->font)
        {
            LOG_WARNING("Failed to load any font, continuing without font");
        }
    }

    wm->x11_ctx->wm_delete_window = XInternAtom(wm->x11_ctx->display, "WM_DELETE_WINDOW", False);

    LOG_INFO("X11 backend initialized successfully - Desktop Environment mode");
}

Window glps_x11_window_get_root(glps_WindowManager *wm)
{
    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL)
    {
        LOG_CRITICAL("Invalid Window Manager state. Exiting...");
        exit(EXIT_FAILURE);
    }
    return wm->x11_ctx->root;
}
ssize_t glps_x11_window_create(glps_WindowManager *wm, const char *title,
                               int width, int height)
{
    if (!wm || !wm->x11_ctx || !wm->x11_ctx->display)
        return -1;

    if (wm->window_count >= MAX_WINDOWS)
        return -1;

    int screen = DefaultScreen(wm->x11_ctx->display);

    wm->windows[wm->window_count] = (glps_X11Window*)calloc(1, sizeof(glps_X11Window));
    if (!wm->windows[wm->window_count])
        return -1;

    // Get native visual from EGL config
    EGLint native_visual_id;
    eglGetConfigAttrib(wm->egl_ctx->dpy, wm->egl_ctx->conf, EGL_NATIVE_VISUAL_ID, &native_visual_id);

    XSetWindowAttributes attrs;
    attrs.override_redirect = True; // important for custom DE
    attrs.event_mask = StructureNotifyMask | ExposureMask | KeyPressMask | KeyReleaseMask |
                       ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask;
    attrs.background_pixel = WhitePixel(wm->x11_ctx->display, screen);
    attrs.border_pixel = BlackPixel(wm->x11_ctx->display, screen);
    attrs.colormap = XCreateColormap(wm->x11_ctx->display, wm->x11_ctx->root,
                                     (Visual*)native_visual_id, AllocNone);

    wm->windows[wm->window_count]->window = XCreateWindow(
        wm->x11_ctx->display,
        wm->x11_ctx->root,
        100, 100, width, height, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWEventMask | CWBackPixel | CWBorderPixel | CWColormap,
        &attrs
    );

    if (!wm->windows[wm->window_count]->window)
        return -1;

    XStoreName(wm->x11_ctx->display, wm->windows[wm->window_count]->window, title);

    // Create EGL surface
    wm->windows[wm->window_count]->egl_surface =
        eglCreateWindowSurface(wm->egl_ctx->dpy, wm->egl_ctx->conf,
                               (NativeWindowType)wm->windows[wm->window_count]->window, NULL);

    if (wm->windows[wm->window_count]->egl_surface == EGL_NO_SURFACE)
    {
        EGLint err = eglGetError();
        fprintf(stderr, "Failed to create EGL surface: 0x%X\n", err);
        return -1;
    }

    if (!eglMakeCurrent(wm->egl_ctx->dpy,
                        wm->windows[wm->window_count]->egl_surface,
                        wm->windows[wm->window_count]->egl_surface,
                        wm->egl_ctx->ctx))
    {
        EGLint err = eglGetError();
        fprintf(stderr, "Failed to make EGL context current: 0x%X\n", err);
        return -1;
    }

    XMapWindow(wm->x11_ctx->display, wm->windows[wm->window_count]->window);
    XFlush(wm->x11_ctx->display);

    return wm->window_count++;
}
void glps_x11_window_update(glps_WindowManager *wm, size_t window_id)
{
    if (!wm || !wm->x11_ctx || window_id >= wm->window_count || !wm->windows[window_id])
        return;

    if (wm->windows[window_id]->egl_surface != EGL_NO_SURFACE)
    {
        eglMakeCurrent(wm->egl_ctx->dpy,
                       wm->windows[window_id]->egl_surface,
                       wm->windows[window_id]->egl_surface,
                       wm->egl_ctx->ctx);

        // Example: clear screen white
        glViewport(0, 0, 800, 600); // replace with actual window size
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        eglSwapBuffers(wm->egl_ctx->dpy, wm->windows[window_id]->egl_surface);
    }

    XFlush(wm->x11_ctx->display);
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

        if (event.type == MapRequest && event.xmaprequest.parent == wm->x11_ctx->root)
        {
            Window window_to_map = event.xmaprequest.window;
            LOG_INFO("MapRequest for window %lu", window_to_map);

            XReparentWindow(display, window_to_map, wm->x11_ctx->frame, 10, 30);
            XMapWindow(display, window_to_map);
            XMapWindow(display, wm->x11_ctx->frame);

            continue;
        }

        ssize_t window_id = __get_window_id_by_xid(wm, event.xany.window);
        if (window_id < 0)
        {

            if (event.xany.window == wm->x11_ctx->frame)
            {

                continue;
            }

            LOG_ERROR("Event for untracked window %lu", event.xany.window);
            continue;
        }

        switch (event.type)
        {
        case ClientMessage:
            if ((Atom)event.xclient.data.l[0] == wm->x11_ctx->wm_delete_window)
            {
                LOG_INFO("Window close request for window %zd", window_id);
                if (wm->callbacks.window_close_callback)
                {
                    wm->callbacks.window_close_callback(
                        (size_t)window_id,
                        wm->callbacks.window_close_data);
                }
                __remove_window(wm, event.xclient.window);
                return (wm->window_count == 0);
            }
            break;

        case DestroyNotify:
            LOG_INFO("Window %zd destroyed", window_id);
            if (wm->callbacks.window_close_callback)
            {
                wm->callbacks.window_close_callback(
                    (size_t)window_id,
                    wm->callbacks.window_close_data);
            }
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
                int count = XLookupString(&event.xkey, buf, sizeof(buf) - 1, &keysym, NULL);
                buf[count] = '\0';

                KeyCode keycode = event.xkey.keycode;
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
                int count = XLookupString(&event.xkey, buf, sizeof(buf) - 1, &keysym, NULL);
                buf[count] = '\0';

                KeyCode keycode = event.xkey.keycode;
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

void glps_x11_destroy(glps_WindowManager *wm)
{
    if (wm == NULL)
    {
        return;
    }

    if (wm->egl_ctx)
    {
        glps_egl_destroy(wm);
    }

    if (wm->windows)
    {
        for (size_t i = 0; i < wm->window_count; ++i)
        {
            if (wm->windows[i] != NULL)
            {

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

        if (wm->x11_ctx->frame && wm->x11_ctx->display)
        {
            XDestroyWindow(wm->x11_ctx->display, wm->x11_ctx->frame);
        }

        if (wm->x11_ctx->font && wm->x11_ctx->display)
        {
            XFreeFont(wm->x11_ctx->display, wm->x11_ctx->font);
        }
        if (wm->x11_ctx->gc && wm->x11_ctx->display)
        {
            XFreeGC(wm->x11_ctx->display, wm->x11_ctx->gc);
        }
        if (wm->x11_ctx->cursor && wm->x11_ctx->display)
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

    LOG_INFO("X11 backend destroyed");
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
        size_hints->max_width = 10000;
        size_hints->max_height = 10000;
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
        selected_cursor = XC_arrow;
        break;
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

    if (wm->x11_ctx->cursor)
    {
        XFreeCursor(wm->x11_ctx->display, wm->x11_ctx->cursor);
    }

    wm->x11_ctx->cursor = XCreateFontCursor(wm->x11_ctx->display, (unsigned int)selected_cursor);

    for (size_t i = 0; i < wm->window_count; i++)
    {
        if (wm->windows[i] && wm->windows[i]->window)
        {
            XDefineCursor(wm->x11_ctx->display, wm->windows[i]->window, wm->x11_ctx->cursor);
        }
    }

    LOG_INFO("Cursor updated to type: %d", user_cursor);
}