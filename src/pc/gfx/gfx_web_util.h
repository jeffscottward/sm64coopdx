#ifndef GFX_WEB_UTIL_H
#define GFX_WEB_UTIL_H

/**
 * gfx_web_util.h — Helper macros and functions for web/Emscripten rendering.
 *
 * Provides:
 *   - Canvas resize utility (sync HTML5 canvas with CSS display size)
 *   - Device pixel ratio query for HiDPI/Retina displays
 *   - WebGL context loss/restore event handling
 *
 * This header is only active when building with Emscripten (__EMSCRIPTEN__).
 * On native builds it compiles to nothing.
 */

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/html5.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------- */
/*  Device Pixel Ratio                                                        */
/* -------------------------------------------------------------------------- */

/**
 * Returns the current device pixel ratio (window.devicePixelRatio).
 * Falls back to 1.0 if unavailable.
 */
static inline double gfx_web_get_device_pixel_ratio(void) {
    return EM_ASM_DOUBLE({ return window.devicePixelRatio || 1.0; });
}

/* -------------------------------------------------------------------------- */
/*  Canvas Resize                                                             */
/* -------------------------------------------------------------------------- */

/**
 * Syncs the HTML5 canvas drawing-buffer size with the actual CSS display size,
 * accounting for devicePixelRatio. This prevents blurry rendering on HiDPI
 * displays where the CSS size and the canvas backing-store size can diverge.
 *
 * Call this once per frame (e.g. in the start_frame path) or after a resize
 * event. Pass the canvas CSS selector (usually "#canvas").
 *
 * Returns true if the canvas size was actually changed.
 */
static inline bool gfx_web_sync_canvas_size(const char *canvas_selector) {
    /* Ask JS to measure the CSS layout size, multiply by DPR, and resize
       the canvas element's width/height attributes if they differ.
       The (void) cast silences -Wunused-parameter in stub/test builds;
       real Emscripten uses the parameter via $0 in the JS block. */
    (void)canvas_selector;
    return (bool)EM_ASM_INT({
        var selector = UTF8ToString($0);
        var canvas = document.querySelector(selector);
        if (!canvas) return 0;

        var dpr = window.devicePixelRatio || 1.0;
        var displayW = (canvas.clientWidth  * dpr) | 0;
        var displayH = (canvas.clientHeight * dpr) | 0;

        if (canvas.width !== displayW || canvas.height !== displayH) {
            canvas.width  = displayW;
            canvas.height = displayH;
            return 1;
        }
        return 0;
    }, canvas_selector);
}

/* -------------------------------------------------------------------------- */
/*  WebGL Context Loss Handling                                               */
/* -------------------------------------------------------------------------- */

/* Tracks whether the WebGL context is currently lost. */
static bool gfx_web_context_is_lost = false;

/**
 * Returns true if the WebGL context is currently lost.
 * Callers can use this to skip rendering while the context is unavailable.
 */
static inline bool gfx_web_is_context_lost(void) {
    return gfx_web_context_is_lost;
}

/**
 * Emscripten callback for the "webglcontextlost" event.
 * Prevents the default browser behavior (which would destroy the context
 * permanently) and sets the lost flag so the game loop can skip rendering.
 */
static EM_BOOL gfx_web_on_context_lost(int event_type, const void *reserved, void *user_data) {
    (void)event_type;
    (void)reserved;
    (void)user_data;
    gfx_web_context_is_lost = true;
    /* Returning EM_TRUE calls preventDefault(), giving the browser a chance
       to restore the context automatically. */
    return EM_TRUE;
}

/**
 * Emscripten callback for the "webglcontextrestored" event.
 * Clears the lost flag so rendering can resume.
 *
 * NOTE: After a context restore, all GL resources (textures, shaders, buffers)
 * must be recreated. A full re-init of the rendering backend should be
 * triggered by the caller when this flag transitions from lost -> restored.
 */
static EM_BOOL gfx_web_on_context_restored(int event_type, const void *reserved, void *user_data) {
    (void)event_type;
    (void)reserved;
    (void)user_data;
    gfx_web_context_is_lost = false;
    return EM_TRUE;
}

/**
 * Registers WebGL context loss and restore event handlers on the given canvas.
 * Call once during graphics initialization (e.g. after SDL_GL_CreateContext).
 *
 * @param canvas_selector  CSS selector for the canvas, e.g. "#canvas" or NULL
 *                         for the default Emscripten canvas.
 */
static inline void gfx_web_register_context_handlers(const char *canvas_selector) {
    emscripten_set_webglcontextlost_callback(
        canvas_selector, /* target */
        NULL,            /* userData */
        EM_TRUE,         /* useCapture */
        gfx_web_on_context_lost
    );
    emscripten_set_webglcontextrestored_callback(
        canvas_selector, /* target */
        NULL,            /* userData */
        EM_TRUE,         /* useCapture */
        gfx_web_on_context_restored
    );
}

#endif /* __EMSCRIPTEN__ */

#endif /* GFX_WEB_UTIL_H */
