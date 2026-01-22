
#include <glps_egl_context.h>
#include "utils/logger/pico_logger.h"
void glps_egl_init(glps_WindowManager *wm, EGLNativeDisplayType display)
{
    assert(wm);
    assert(!wm->egl_ctx);

    wm->egl_ctx = calloc(1, sizeof(glps_EGLContext));
    assert(wm->egl_ctx);

    wm->egl_ctx->dpy = eglGetDisplay(display);
    if (wm->egl_ctx->dpy == EGL_NO_DISPLAY) {
        LOG_ERROR("eglGetDisplay failed");
        exit(EXIT_FAILURE);
    }

    EGLint major, minor;
    if (!eglInitialize(wm->egl_ctx->dpy, &major, &minor)) {
        LOG_ERROR("eglInitialize failed: 0x%x", eglGetError());
        exit(EXIT_FAILURE);
    }

    LOG_INFO("EGL initialized (%d.%d)", major, minor);

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_NONE
    };

    EGLint num;
    if (!eglChooseConfig(
            wm->egl_ctx->dpy,
            config_attribs,
            &wm->egl_ctx->conf,
            1,
            &num) || num != 1)
    {
        LOG_ERROR("eglChooseConfig failed");
        exit(EXIT_FAILURE);
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        LOG_ERROR("eglBindAPI(EGL_OPENGL_ES_API) failed: 0x%x",
                  eglGetError());
        exit(EXIT_FAILURE);
    }

    EGLint visual_id;
    if (!eglGetConfigAttrib(
            wm->egl_ctx->dpy,
            wm->egl_ctx->conf,
            EGL_NATIVE_VISUAL_ID,
            &visual_id))
    {
        LOG_ERROR("eglGetConfigAttrib(EGL_NATIVE_VISUAL_ID) failed");
        exit(EXIT_FAILURE);
    }

    wm->egl_ctx->x11_visual_id = (VisualID)visual_id;

    eglSwapInterval(wm->egl_ctx->dpy, 1);

    LOG_INFO("EGL config chosen (X11 visual id = 0x%lx)",
             wm->egl_ctx->x11_visual_id);
}


void glps_egl_create_ctx(glps_WindowManager *wm)
{
    assert(wm && wm->egl_ctx);
    assert(wm->egl_ctx->ctx == EGL_NO_CONTEXT);

    const EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    wm->egl_ctx->ctx = eglCreateContext(
        wm->egl_ctx->dpy,
        wm->egl_ctx->conf,
        EGL_NO_CONTEXT,
        ctx_attribs
    );

    if (wm->egl_ctx->ctx == EGL_NO_CONTEXT) {
        LOG_ERROR("eglCreateContext failed: 0x%x", eglGetError());
        exit(EXIT_FAILURE);
    }
}

void glps_egl_make_ctx_current(glps_WindowManager *wm, size_t window_id)
{
    if(wm == NULL || wm->egl_ctx == NULL) {
        LOG_ERROR("Window Manager or EGL context is NULL");
        exit(EXIT_FAILURE);
    }

    if (!wm->windows[window_id] ||  wm->windows[window_id]->egl_surface == EGL_NO_SURFACE) {
        LOG_ERROR("Invalid EGL surface");
        exit(EXIT_FAILURE);
    }

    if (!eglMakeCurrent(
            wm->egl_ctx->dpy,
            wm->windows[window_id]->egl_surface,
            wm->windows[window_id]->egl_surface,
            wm->egl_ctx->ctx))
    {
        EGLint error = eglGetError();
        LOG_ERROR("eglMakeCurrent failed: 0x%x", error);

        if (error == EGL_BAD_MATCH)
            LOG_ERROR("EGL_BAD_MATCH (visual / config mismatch)");

        exit(EXIT_FAILURE);
    }
}


void *glps_egl_get_proc_addr(const char *name)
{
    return (void *)eglGetProcAddress(name);
}

void glps_egl_destroy(glps_WindowManager *wm)
{
    if (!wm || !wm->egl_ctx)
        return;

    eglMakeCurrent(
        wm->egl_ctx->dpy,
        EGL_NO_SURFACE,
        EGL_NO_SURFACE,
        EGL_NO_CONTEXT
    );

    if (wm->egl_ctx->ctx != EGL_NO_CONTEXT)
        eglDestroyContext(wm->egl_ctx->dpy, wm->egl_ctx->ctx);

    if (wm->egl_ctx->dpy != EGL_NO_DISPLAY)
        eglTerminate(wm->egl_ctx->dpy);

    free(wm->egl_ctx);
    wm->egl_ctx = NULL;
}

void glps_egl_swap_buffers(glps_WindowManager *wm, size_t window_id)
{
    assert(wm && wm->egl_ctx);
    assert(window_id < wm->window_count);

    eglSwapBuffers(
        wm->egl_ctx->dpy,
        wm->windows[window_id]->egl_surface
    );
}
