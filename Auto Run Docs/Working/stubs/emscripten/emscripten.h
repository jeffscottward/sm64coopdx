/* Stub emscripten.h for syntax checking outside Emscripten SDK */
#ifndef _EMSCRIPTEN_STUB_H_
#define _EMSCRIPTEN_STUB_H_

#define EM_ASM(...)
#define EM_ASM_INT(...) 0
#define EM_ASM_DOUBLE(...) 0.0
#define EM_JS(ret, name, params, body)
#define EMSCRIPTEN_KEEPALIVE

static inline void emscripten_sleep(unsigned int ms) { (void)ms; }
static inline void emscripten_set_main_loop(void (*func)(void), int fps, int simulate_infinite_loop) {
    (void)func; (void)fps; (void)simulate_infinite_loop;
}

#endif
