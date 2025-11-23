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

    glps_X11Window *window_to_remove = wm->windows[window_id];

    if (window_to_remove->egl_surface != EGL_NO_SURFACE && wm->egl_ctx != NULL)
    {
        eglDestroySurface(wm->egl_ctx->dpy, window_to_remove->egl_surface);
        window_to_remove->egl_surface = EGL_NO_SURFACE;
    }

    if (wm->x11_ctx != NULL && wm->x11_ctx->display != NULL)
    {
        XDestroyWindow(wm->x11_ctx->display, window_to_remove->window);
    }

    free(window_to_remove);

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

    wm->x11_ctx->font = XLoadQueryFont(wm->x11_ctx->display, "fixed");
    if (!wm->x11_ctx->font)
    {
        LOG_CRITICAL("Failed to load system font\n");
        XCloseDisplay(wm->x11_ctx->display);
        free(wm->windows);
        free(wm->x11_ctx);
        exit(EXIT_FAILURE);
    }

    wm->x11_ctx->wm_delete_window = XInternAtom(wm->x11_ctx->display, "WM_DELETE_WINDOW", False);
    wm->window_count = 0;
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

    int screen = DefaultScreen(wm->x11_ctx->display);
    size_t window_index = wm->window_count;
    
    wm->windows[window_index] = (glps_X11Window *)calloc(1, sizeof(glps_X11Window));
    if (wm->windows[window_index] == NULL)
    {
        LOG_ERROR("Failed to allocate window");
        return -1;
    }
    
    wm->windows[window_index]->fps_start_time = (struct timespec){0};
    wm->windows[window_index]->fps_is_init = false;

    wm->windows[window_index]->window = XCreateSimpleWindow(
        wm->x11_ctx->display,
        RootWindow(wm->x11_ctx->display, screen),
        10, 10, width, height, 1,
        BlackPixel(wm->x11_ctx->display, screen),
        WhitePixel(wm->x11_ctx->display, screen));

    if (wm->windows[window_index]->window == 0)
    {
        LOG_ERROR("Failed to create X11 window");
        free(wm->windows[window_index]);
        wm->windows[window_index] = NULL;
        return -1;
    }

    XSetWindowBackground(wm->x11_ctx->display, wm->windows[window_index]->window, 0xFFFFFF);
    XSetWindowAttributes swa;
    swa.backing_store = WhenMapped;
    XChangeWindowAttributes(wm->x11_ctx->display, wm->windows[window_index]->window, CWBackingStore, &swa);
    XStoreName(wm->x11_ctx->display, wm->windows[window_index]->window, title);

    if (wm->x11_ctx->gc == NULL)
    {
        wm->x11_ctx->gc = XCreateGC(wm->x11_ctx->display, wm->windows[window_index]->window, 0, NULL);
        if (wm->x11_ctx->gc == NULL)
        {
            LOG_ERROR("Failed to create graphics context");
            XDestroyWindow(wm->x11_ctx->display, wm->windows[window_index]->window);
            free(wm->windows[window_index]);
            wm->windows[window_index] = NULL;
            return -1;
        }
    }

    XSetWMProtocols(wm->x11_ctx->display, wm->windows[window_index]->window,
                    &wm->x11_ctx->wm_delete_window, 1);

    long event_mask =
        PointerMotionMask |
        ButtonPressMask |
        ButtonReleaseMask |
        KeyPressMask |
        KeyReleaseMask |
        StructureNotifyMask |
        ExposureMask;

    int result = XSelectInput(wm->x11_ctx->display, wm->windows[window_index]->window,
                              event_mask);
    if (result == BadWindow)
    {
        LOG_ERROR("Failed to select input events");
        XDestroyWindow(wm->x11_ctx->display, wm->windows[window_index]->window);
        free(wm->windows[window_index]);
        wm->windows[window_index] = NULL;
        return -1;
    }

    if (wm->egl_ctx != NULL)
    {
        wm->windows[window_index]->egl_surface =
            eglCreateWindowSurface(wm->egl_ctx->dpy, wm->egl_ctx->conf,
                                   (NativeWindowType)wm->windows[window_index]->window, NULL);
        if (wm->windows[window_index]->egl_surface == EGL_NO_SURFACE)
        {
            LOG_ERROR("Failed to create EGL surface");
            XDestroyWindow(wm->x11_ctx->display, wm->windows[window_index]->window);
            free(wm->windows[window_index]);
            wm->windows[window_index] = NULL;
            return -1;
        }
    }
    else
    {
        wm->windows[window_index]->egl_surface = EGL_NO_SURFACE;
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
                break;
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
            break;

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
                char buf[32];
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
                char buf[32];
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
                if (wm->windows[i]->egl_surface != EGL_NO_SURFACE && wm->egl_ctx != NULL)
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

    if (wm->egl_ctx)
    {
        glps_egl_destroy(wm);
    }
}