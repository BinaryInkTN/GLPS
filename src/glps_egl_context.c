#include <glps_egl_context.h>
#include "utils/logger/pico_logger.h"

void glps_egl_init(glps_WindowManager *wm, EGLNativeDisplayType display) {
    wm->egl_ctx = calloc(1, sizeof(glps_EGLContext));

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_CONFIG_CAVEAT, EGL_NONE,
        EGL_NONE
    };

    EGLint major, minor, n;
    EGLint num_configs = 0;

    wm->egl_ctx->dpy = eglGetDisplay((EGLNativeDisplayType)display);
    assert(wm->egl_ctx->dpy);

    if (!eglInitialize(wm->egl_ctx->dpy, &major, &minor)) {
        LOG_ERROR("Failed to initialize EGL");
        exit(EXIT_FAILURE);
    }

    // Get number of configs first
    if (!eglChooseConfig(wm->egl_ctx->dpy, config_attribs, NULL, 0, &num_configs)) {
        LOG_ERROR("Failed to get EGL config count");
        exit(EXIT_FAILURE);
    }

    if (num_configs == 0) {
        LOG_ERROR("No EGL configs found");
        exit(EXIT_FAILURE);
    }

    EGLConfig *configs = malloc(num_configs * sizeof(EGLConfig));
    if (!configs) {
        LOG_ERROR("Failed to allocate memory for EGL configs");
        exit(EXIT_FAILURE);
    }

    if (!eglChooseConfig(wm->egl_ctx->dpy, config_attribs, configs, num_configs, &n)) {
        LOG_ERROR("Failed to choose EGL config");
        free(configs);
        exit(EXIT_FAILURE);
    }

    // Select hardware accelerated config
    EGLint selected = -1;
    for (int i = 0; i < n; i++) {
        EGLint caveat;
        EGLint renderable;
        EGLint surface_type;
        EGLint red_size, green_size, blue_size, alpha_size, depth_size, stencil_size;

        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_CONFIG_CAVEAT, &caveat);
        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_RENDERABLE_TYPE, &renderable);
        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_SURFACE_TYPE, &surface_type);
        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_RED_SIZE, &red_size);
        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_GREEN_SIZE, &green_size);
        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_BLUE_SIZE, &blue_size);
        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_ALPHA_SIZE, &alpha_size);
        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_DEPTH_SIZE, &depth_size);
        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_STENCIL_SIZE, &stencil_size);

        // Prefer hardware accelerated configs with good properties
        if (caveat == EGL_NONE && 
            (renderable & EGL_OPENGL_ES3_BIT) &&
            (surface_type & EGL_WINDOW_BIT) &&
            red_size >= 8 && green_size >= 8 && blue_size >= 8) {
            selected = i;
            LOG_DEBUG("Found hardware accelerated config: red=%d, green=%d, blue=%d, depth=%d, stencil=%d",
                      red_size, green_size, blue_size, depth_size, stencil_size);
            break;
        }
    }

    // Fallback: use first config if no hardware config found
    if (selected == -1) {
        selected = 0;
        LOG_WARNING("No hardware accelerated config found, using first available");
    }

    wm->egl_ctx->conf = configs[selected];
    free(configs);

    // FIX: Bind OpenGL ES API, NOT desktop OpenGL
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        LOG_ERROR("Failed to bind OpenGL ES API");
        exit(EXIT_FAILURE);
    }

    eglSwapInterval(wm->egl_ctx->dpy, 1);

    EGLint error = eglGetError();
    if (error != EGL_SUCCESS) {
        LOG_ERROR("EGL error after initialization: 0x%x", error);
    }

    LOG_INFO("EGL initialized successfully (version %d.%d), using hardware acceleration", major, minor);
}

void glps_egl_create_ctx(glps_WindowManager *wm) {
    // Try OpenGL ES 3.0 first
    static const EGLint context_attribs_es3[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    wm->egl_ctx->ctx = eglCreateContext(wm->egl_ctx->dpy, wm->egl_ctx->conf,
                                        EGL_NO_CONTEXT, context_attribs_es3);

    if (wm->egl_ctx->ctx == EGL_NO_CONTEXT) {
        EGLint error = eglGetError();
        LOG_WARNING("Failed to create OpenGL ES 3.0 context: 0x%X, trying ES 2.0", error);

        // Fallback to OpenGL ES 2.0
        static const EGLint context_attribs_es2[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };

        wm->egl_ctx->ctx = eglCreateContext(wm->egl_ctx->dpy, wm->egl_ctx->conf,
                                            EGL_NO_CONTEXT, context_attribs_es2);

        if (wm->egl_ctx->ctx == EGL_NO_CONTEXT) {
            error = eglGetError();
            LOG_ERROR("Failed to create EGL context (ES 2.0 fallback also failed): 0x%X", error);
            exit(EXIT_FAILURE);
        }

        LOG_INFO("Created OpenGL ES 2.0 context (fallback)");
    } else {
        LOG_INFO("Created OpenGL ES 3.0 context");
    }
}

void glps_egl_make_ctx_current(glps_WindowManager *wm, size_t window_id) {
    if (!wm || !wm->egl_ctx || window_id >= wm->window_count) {
        LOG_ERROR("Invalid parameters for eglMakeCurrent");
        return;
    }

    if (!eglMakeCurrent(wm->egl_ctx->dpy, wm->windows[window_id]->egl_surface,
                        wm->windows[window_id]->egl_surface, wm->egl_ctx->ctx)) {
        EGLint error = eglGetError();
        LOG_ERROR("eglMakeCurrent failed: 0x%x", error);
        if (error == EGL_BAD_DISPLAY)
            LOG_ERROR("Invalid EGL display");
        else if (error == EGL_BAD_SURFACE)
            LOG_ERROR("Invalid draw or read surface");
        else if (error == EGL_BAD_CONTEXT)
            LOG_ERROR("Invalid EGL context");
        else if (error == EGL_BAD_MATCH)
            LOG_ERROR("Context or surface attributes mismatch");
        exit(EXIT_FAILURE);
    }
}

void *glps_egl_get_proc_addr(const char* name) {
    return (void*)eglGetProcAddress;
}

void glps_egl_destroy(glps_WindowManager *wm) {
    if (wm == NULL || wm->egl_ctx == NULL) return;

    if (wm->egl_ctx->ctx) {
        eglDestroyContext(wm->egl_ctx->dpy, wm->egl_ctx->ctx);
        wm->egl_ctx->ctx = EGL_NO_CONTEXT;
    }
    if (wm->egl_ctx->dpy) {
        eglTerminate(wm->egl_ctx->dpy);
        wm->egl_ctx->dpy = EGL_NO_DISPLAY;
    }
    free(wm->egl_ctx);
    wm->egl_ctx = NULL;
}

void glps_egl_swap_buffers(glps_WindowManager *wm, size_t window_id) {
    if (!wm || !wm->egl_ctx || window_id >= wm->window_count) {
        return;
    }

    if (!eglSwapBuffers(wm->egl_ctx->dpy, wm->windows[window_id]->egl_surface)) {
        EGLint error = eglGetError();
        if (error != EGL_SUCCESS) {
            LOG_ERROR("eglSwapBuffers failed: 0x%x", error);
        }
    }
}