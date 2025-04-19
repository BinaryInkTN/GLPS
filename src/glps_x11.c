/*
 Copyright 2025 Google LLC

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      https://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

#include "glps_x11.h"
#include "glps_egl_context.h"
#include "utils/logger/pico_logger.h"

static ssize_t __get_window_id_by_xid(glps_WindowManager *wm, Window xid)
{
    if (wm == NULL || wm->windows == NULL)
    {
        return -1;
    }

    for (size_t i = 0; i < wm->window_count; i++)
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
        LOG_ERROR("Window ID is invalid.");
        return;
    }

    XDestroyWindow(wm->x11_ctx->display, wm->windows[window_id]->window);
    free(wm->windows[window_id]);
    wm->windows[window_id] = NULL;

    for (size_t i = window_id; i < wm->window_count - 1; i++)
    {
        wm->windows[i] = wm->windows[i + 1];
    }
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
}

ssize_t glps_x11_window_create(glps_WindowManager *wm, const char *title,
                               int width, int height)
{
    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL)
    {
        LOG_CRITICAL("Failed to create X11 window. Window manager and/or Display NULL.");
        exit(EXIT_FAILURE);
    }

    if (wm->window_count >= MAX_WINDOWS)
    {
        LOG_ERROR("Maximum number of windows reached");
        return -1;
    }

    int screen = DefaultScreen(wm->x11_ctx->display);
    wm->windows[wm->window_count] = (glps_X11Window *)calloc(1, sizeof(glps_X11Window));
    if (wm->windows[wm->window_count] == NULL)
    {
        LOG_ERROR("Failed to allocate window");
        return -1;
    }

    wm->windows[wm->window_count]->window = XCreateSimpleWindow(
        wm->x11_ctx->display,
        RootWindow(wm->x11_ctx->display, screen),
        10, 10, width, height, 1,
        BlackPixel(wm->x11_ctx->display, screen),
        WhitePixel(wm->x11_ctx->display, screen));

    if (wm->windows[wm->window_count]->window == 0)
    {
        LOG_ERROR("Failed to create X11 window");
        free(wm->windows[wm->window_count]);
        return -1;
    }

    XSetWindowBackground(wm->x11_ctx->display, wm->windows[wm->window_count]->window, 0xFFFFFF);

    XStoreName(wm->x11_ctx->display, wm->windows[wm->window_count]->window, title);

    wm->x11_ctx->gc = XCreateGC(wm->x11_ctx->display, wm->windows[wm->window_count]->window, 0, NULL);
    if (wm->x11_ctx->gc == NULL)
    {
        LOG_ERROR("Failed to create graphics context");
        XDestroyWindow(wm->x11_ctx->display, wm->windows[wm->window_count]->window);
        free(wm->windows[wm->window_count]);
        return -1;
    }

    wm->x11_ctx->wm_delete_window = XInternAtom(wm->x11_ctx->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(wm->x11_ctx->display, wm->windows[wm->window_count]->window,
                    &wm->x11_ctx->wm_delete_window, 1);
    long event_mask =
        PointerMotionMask |   // Mouse movement
        ButtonPressMask |     // Mouse button presses
        ButtonReleaseMask |   // Mouse button releases
        KeyPressMask |        // Keyboard presses
        KeyReleaseMask |      // Keyboard releases
        StructureNotifyMask | // Resize, move, etc.
        ExposureMask;         // Expose events

    int result = XSelectInput(wm->x11_ctx->display, wm->windows[wm->window_count]->window,
                              event_mask);
    if (result == BadWindow)
    {
        LOG_ERROR("Failed to select input events");
        XDestroyWindow(wm->x11_ctx->display, wm->windows[wm->window_count]->window);
        free(wm->windows[wm->window_count]);
        return -1;
    }

    XMapWindow(wm->x11_ctx->display, wm->windows[wm->window_count]->window);

    if (wm->egl_ctx != NULL)
    {
        wm->windows[wm->window_count]->egl_surface =
            eglCreateWindowSurface(wm->egl_ctx->dpy, wm->egl_ctx->conf,
                                   (NativeWindowType)wm->windows[wm->window_count]->window, NULL);
        if (wm->windows[wm->window_count]->egl_surface == EGL_NO_SURFACE)
        {
            LOG_ERROR("Failed to create EGL surface");
            XDestroyWindow(wm->x11_ctx->display, wm->windows[wm->window_count]->window);
            free(wm->windows[wm->window_count]);
            return -1;
        }
    }

    if (wm->window_count == 0)
    {
        glps_egl_create_ctx(wm);
        glps_egl_make_ctx_current(wm, 0);
    }

    return wm->window_count++;
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

    while (XPending(display) > 0)
    {
        XNextEvent(display, &event);

        ssize_t window_id = __get_window_id_by_xid(wm, event.xany.window);
        if (window_id < 0)
        {
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

                return true;
            }
            break;

        case DestroyNotify:
            LOG_ERROR("DestroyNotify for window %lu", event.xdestroywindow.window);
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

            if (wm->callbacks.mouse_enter_callback)
            {
                wm->callbacks.mouse_enter_callback(
                    (size_t)window_id,
                    event.xmotion.x,
                    event.xmotion.y,
                    wm->callbacks.mouse_move_data);
            }

            break;

        case ButtonPress:
            if (wm->callbacks.mouse_click_callback)
            {
                wm->callbacks.mouse_click_callback(
                    (size_t)window_id,
                    true,
                    wm->callbacks.mouse_click_data);
            }
            break;

        case ButtonRelease:
            if (wm->callbacks.mouse_click_callback)
            {
                wm->callbacks.mouse_click_callback(
                    (size_t)window_id,
                    false,
                    wm->callbacks.mouse_click_data);
            }
            break;

        case KeyPress:
            if (wm->callbacks.keyboard_callback)
            {
                char buf[32];
                KeySym keysym;
                XLookupString(&event.xkey, buf, sizeof(buf), &keysym, NULL);
                wm->callbacks.keyboard_callback(
                    (size_t)window_id,
                    true,
                    buf,
                    wm->callbacks.keyboard_data);
            }
            break;

        case KeyRelease:
            if (wm->callbacks.keyboard_callback)
            {
                char buf[32];
                KeySym keysym;
                XLookupString(&event.xkey, buf, sizeof(buf), &keysym, NULL);
                wm->callbacks.keyboard_callback(
                    (size_t)window_id,
                    false,
                    buf,
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

        case MapNotify:
            LOG_INFO("Window %zd mapped", window_id);
            break;

        case UnmapNotify:
            LOG_INFO("Window %zd unmapped", window_id);
            break;

        default:

            LOG_WARNING("Unhandled event type: %d", event.type);

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

    XExposeEvent expose_event = {
        .type = Expose,
        .display = wm->x11_ctx->display,
        .window = wm->windows[window_id]->window,
        .x = 0,
        .y = 0,
        .width = 0,
        .height = 0,
        .count = 0};

    XSendEvent(wm->x11_ctx->display, wm->windows[window_id]->window, False, ExposureMask, (XEvent *)&expose_event);
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

void glps_x11_attach_to_clipboard(glps_WindowManager *wm, char *mime,
                                  char *data)
{
    // TODO: Implement clipboard functionality
    (void)wm;
    (void)mime;
    (void)data;
}

void glps_x11_get_from_clipboard(glps_WindowManager *wm, char *data,
                                 size_t data_size)
{
    // TODO: Implement clipboard functionality
    (void)wm;
    (void)data;
    (void)data_size;
}