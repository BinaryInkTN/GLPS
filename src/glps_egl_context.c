#include <glps_egl_context.h>
#include "utils/logger/pico_logger.h"

void glps_egl_init(glps_WindowManager *wm, EGLNativeDisplayType display) {
    wm->egl_ctx = calloc(1, sizeof(glps_EGLContext));

    // Try with fewer requirements first
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,              // Reduced from 24
        EGL_STENCIL_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,  // Use ES2 first
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

    // Try to get configs
    if (!eglChooseConfig(wm->egl_ctx->dpy, config_attribs, NULL, 0, &num_configs)) {
        LOG_ERROR("Failed to get EGL config count");
        exit(EXIT_FAILURE);
    }

    if (num_configs == 0) {
        LOG_ERROR("No EGL configs found with ES2, trying ES1");
        
        // Try with ES1 as last resort
        EGLint fallback_attribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,
            EGL_CONFIG_CAVEAT, EGL_NONE,
            EGL_NONE
        };
        
        if (!eglChooseConfig(wm->egl_ctx->dpy, fallback_attribs, NULL, 0, &num_configs) || num_configs == 0) {
            LOG_ERROR("No EGL configs found at all");
            exit(EXIT_FAILURE);
        }
        
        // Use fallback configs
        memcpy(config_attribs, fallback_attribs, sizeof(fallback_attribs));
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

    // Select best config
    EGLint selected = 0;
    EGLint best_score = -1;
    
    for (int i = 0; i < n; i++) {
        EGLint caveat, renderable, depth, stencil, red, green, blue, alpha;
        EGLint surface_type;
        
        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_CONFIG_CAVEAT, &caveat);
        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_RENDERABLE_TYPE, &renderable);
        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_DEPTH_SIZE, &depth);
        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_STENCIL_SIZE, &stencil);
        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_RED_SIZE, &red);
        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_GREEN_SIZE, &green);
        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_BLUE_SIZE, &blue);
        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_ALPHA_SIZE, &alpha);
        eglGetConfigAttrib(wm->egl_ctx->dpy, configs[i], EGL_SURFACE_TYPE, &surface_type);
        
        int score = 0;
        if (caveat == EGL_NONE) score += 100;
        if (renderable & EGL_OPENGL_ES3_BIT) score += 50;
        else if (renderable & EGL_OPENGL_ES2_BIT) score += 25;
        if (depth >= 24) score += 10;
        if (surface_type & EGL_WINDOW_BIT) score += 5;
        if (red >= 8 && green >= 8 && blue >= 8) score += 5;
        
        if (score > best_score) {
            best_score = score;
            selected = i;
        }
    }

    wm->egl_ctx->conf = configs[selected];
    free(configs);

    // Bind OpenGL ES API
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        LOG_ERROR("Failed to bind OpenGL ES API");
        exit(EXIT_FAILURE);
    }

    eglSwapInterval(wm->egl_ctx->dpy, 1);

    EGLint error = eglGetError();
    if (error != EGL_SUCCESS) {
        // Log but don't exit - some errors are benign
        LOG_WARNING("EGL warning after initialization: 0x%x", error);
    }

    LOG_INFO("EGL initialized successfully (version %d.%d)", major, minor);
}

void glps_egl_create_ctx(glps_WindowManager *wm) {
    EGLint error;
    
    // Try OpenGL ES 3.0 first
    static const EGLint context_attribs_es3[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    wm->egl_ctx->ctx = eglCreateContext(wm->egl_ctx->dpy, wm->egl_ctx->conf,
                                        EGL_NO_CONTEXT, context_attribs_es3);

    if (wm->egl_ctx->ctx == EGL_NO_CONTEXT) {
        error = eglGetError();
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
    if (!wm || !wm->egl_ctx) {
        LOG_ERROR("Invalid window manager or EGL context");
        return;
    }
    
    if (window_id >= wm->window_count) {
        LOG_ERROR("Invalid window_id: %zu (max: %d)", window_id, wm->window_count);
        return;
    }
    
    if (!wm->windows[window_id] || !wm->windows[window_id]->egl_surface) {
        LOG_ERROR("Invalid surface for window %zu", window_id);
        return;
    }

    if (!eglMakeCurrent(wm->egl_ctx->dpy, wm->windows[window_id]->egl_surface,
                        wm->windows[window_id]->egl_surface, wm->egl_ctx->ctx)) {
        EGLint error = eglGetError();
        LOG_ERROR("eglMakeCurrent failed: 0x%x", error);
        
        // Try to recover - sometimes surfaces need recreation
        if (error == EGL_BAD_SURFACE) {
            LOG_ERROR("Bad surface - surface may need recreation");
        } else if (error == EGL_BAD_CONTEXT) {
            LOG_ERROR("Bad context - context may need recreation");
        }
        // Don't exit - allow caller to handle
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

    if (!wm->windows[window_id] || !wm->windows[window_id]->egl_surface) {
        return;
    }

    if (!eglSwapBuffers(wm->egl_ctx->dpy, wm->windows[window_id]->egl_surface)) {
        EGLint error = eglGetError();
        if (error != EGL_SUCCESS) {
            LOG_WARNING("eglSwapBuffers failed: 0x%x", error);
        }
    }
}