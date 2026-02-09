/* Stub emscripten/html5.h for syntax checking */
#ifndef _EMSCRIPTEN_HTML5_STUB_H_
#define _EMSCRIPTEN_HTML5_STUB_H_

typedef int EM_BOOL;
#define EM_TRUE 1
#define EM_FALSE 0

typedef EM_BOOL (*em_webgl_context_callback)(int, const void*, void*);

static inline EM_BOOL emscripten_set_webglcontextlost_callback(
    const char *target, void *userData, EM_BOOL useCapture,
    em_webgl_context_callback callback) {
    (void)target; (void)userData; (void)useCapture; (void)callback;
    return EM_TRUE;
}

static inline EM_BOOL emscripten_set_webglcontextrestored_callback(
    const char *target, void *userData, EM_BOOL useCapture,
    em_webgl_context_callback callback) {
    (void)target; (void)userData; (void)useCapture; (void)callback;
    return EM_TRUE;
}

#endif
