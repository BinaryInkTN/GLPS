#include "glps_egl_context.h"
#include "utils/logger/pico_logger.h"
#include <EGL/egl.h>
#include <assert.h>

void glps_egl_init(glps_WindowManager *wm, EGLNativeDisplayType display) {
    wm->egl_ctx = calloc(1, sizeof(glps_EGLContext));
    if (!wm->egl_ctx) {
        LOG_CRITICAL("Failed to allocate EGL context");
        exit(EXIT_FAILURE);
    }

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_DEPTH_SIZE, 16,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };

    EGLint major, minor, n;

    wm->egl_ctx->dpy = eglGetDisplay(display);
    if (wm->egl_ctx->dpy == EGL_NO_DISPLAY) {
        LOG_CRITICAL("Failed to get EGL display");
        exit(EXIT_FAILURE);
    }

    if (!eglInitialize(wm->egl_ctx->dpy, &major, &minor)) {
        LOG_CRITICAL("Failed to initialize EGL: 0x%x", eglGetError());
        exit(EXIT_FAILURE);
    }
    eglSwapInterval(wm->egl_ctx->dpy, 1);
    check_egl_error("glps_egl_init", "eglSwapInterval");

    if (!eglChooseConfig(wm->egl_ctx->dpy, config_attribs, &wm->egl_ctx->conf, 1, &n) || n != 1) {
        LOG_CRITICAL("Failed to choose a valid EGL config: 0x%x", eglGetError());
        eglTerminate(wm->egl_ctx->dpy);
        exit(EXIT_FAILURE);
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        LOG_CRITICAL("Failed to bind OpenGL ES API: 0x%x", eglGetError());
        eglTerminate(wm->egl_ctx->dpy);
        exit(EXIT_FAILURE);
    }
    LOG_INFO("EGL initialized successfully (version %d.%d)", major, minor);
}

void glps_egl_create_ctx(glps_WindowManager *wm) {
    if (wm->egl_ctx->ctx != EGL_NO_CONTEXT) {
        return; // Reuse existing context for sharing
    }
    static const EGLint context_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 0,
        EGL_NONE
    };
    wm->egl_ctx->ctx = eglCreateContext(wm->egl_ctx->dpy, wm->egl_ctx->conf,
                                        EGL_NO_CONTEXT, context_attribs);
    if (wm->egl_ctx->ctx == EGL_NO_CONTEXT) {
        LOG_CRITICAL("Failed to create EGL context: 0x%x", eglGetError());
        eglTerminate(wm->egl_ctx->dpy);
        exit(EXIT_FAILURE);
    }
}

void glps_egl_make_ctx_current(glps_WindowManager *wm, size_t window_id) {
    if (!wm->egl_ctx || wm->egl_ctx->ctx == EGL_NO_CONTEXT || wm->windows[window_id]->egl_surface == EGL_NO_SURFACE) {
        LOG_ERROR("Cannot make context current: invalid context or surface");
        return;
    }
    if (!eglMakeCurrent(wm->egl_ctx->dpy, wm->windows[window_id]->egl_surface,
                        wm->windows[window_id]->egl_surface, wm->egl_ctx->ctx)) {
        EGLint error = eglGetError();
        LOG_ERROR("eglMakeCurrent failed: 0x%x", error);
        if (error == EGL_BAD_DISPLAY) LOG_ERROR("Invalid EGL display");
        if (error == EGL_BAD_SURFACE) LOG_ERROR("Invalid draw or read surface");
        if (error == EGL_BAD_CONTEXT) LOG_ERROR("Invalid EGL context");
        if (error == EGL_BAD_MATCH) LOG_ERROR("Context or surface attributes mismatch");
        exit(EXIT_FAILURE);
    }
    check_egl_error("glps_egl_make_ctx_current", "eglMakeCurrent");
}

void *glps_egl_get_proc_addr(const char *name) {
    return (void *)eglGetProcAddress(name);
}

void glps_egl_destroy(glps_WindowManager *wm) {
    if (wm->egl_ctx) {
        if (wm->egl_ctx->ctx != EGL_NO_CONTEXT) {
            eglMakeCurrent(wm->egl_ctx->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(wm->egl_ctx->dpy, wm->egl_ctx->ctx);
            check_egl_error("glps_egl_destroy", "eglDestroyContext");
        }
        if (wm->egl_ctx->dpy != EGL_NO_DISPLAY) {
            eglTerminate(wm->egl_ctx->dpy);
            check_egl_error("glps_egl_destroy", "eglTerminate");
        }
        free(wm->egl_ctx);
        wm->egl_ctx = NULL;
    }
}

void glps_egl_swap_buffers(glps_WindowManager *wm, size_t window_id) {
    if (wm->windows[window_id]->egl_surface != EGL_NO_SURFACE) {
        eglSwapBuffers(wm->egl_ctx->dpy, wm->windows[window_id]->egl_surface);
        check_egl_error("glps_egl_swap_buffers", "eglSwapBuffers");
    }
}

// Helper function to log EGL errors (moved here for consistency)
static void check_egl_error(const char *func, const char *call) {
    EGLint error = eglGetError();
    if (error != EGL_SUCCESS) {
        LOG_ERROR("%s: EGL error in %s: 0x%x", func, call, error);
    }
}