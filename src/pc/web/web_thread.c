/**
 * web_thread.c -- Single-threaded fallback for Emscripten/WebAssembly builds.
 *
 * This file provides stub implementations of the threading API defined in
 * src/pc/thread.h. In the web build, all "threaded" work is executed
 * synchronously on the main thread:
 *
 *   - init_thread_handle() / init_thread() call the entry function directly
 *     and return immediately (the "thread" has already finished).
 *   - Mutex operations are no-ops (single-threaded, no contention possible).
 *   - join_thread() / stop_thread() simply mark the handle as STOPPED.
 *
 * This avoids the SharedArrayBuffer requirement for Emscripten pthreads,
 * which needs special server headers (Cross-Origin-Opener-Policy and
 * Cross-Origin-Embedder-Policy) and has limited browser support for some
 * features like pthread_cancel.
 *
 * Build integration:
 *   This file is a standalone reference/alternative that can be compiled
 *   *instead of* src/pc/thread.c for web builds. However, thread.c already
 *   contains #ifdef TARGET_WEB guards that compile the same stubs inline,
 *   so this file is excluded by default to avoid duplicate symbol errors.
 *   Define WEB_USE_STANDALONE_THREAD to use this file instead of thread.c.
 */

/* Guard: thread.c already provides web stubs via TARGET_WEB guards.
 * This file is only compiled if explicitly opted in. */
#ifdef WEB_USE_STANDALONE_THREAD

#include "pc/thread.h"

#include <assert.h>
#include <string.h>

int init_thread_handle(struct ThreadHandle *handle, void *(*entry)(void *), void *arg, void *sp, size_t sp_size) {
    (void)sp;
    (void)sp_size;
    assert(handle != NULL);

    handle->state = RUNNING;

    // Call the entry function directly on the main thread (synchronous).
    if (entry != NULL) {
        entry(arg);
    }

    handle->state = STOPPED;
    return 0;
}

void free_thread_handle(struct ThreadHandle *handle) {
    assert(handle != NULL);
    memset((void *)handle, 0, sizeof(struct ThreadHandle));
}

int init_thread(struct ThreadHandle *handle, void *(*entry)(void *), void *arg, void *sp, size_t sp_size) {
    (void)sp;
    (void)sp_size;
    assert(handle != NULL);

    handle->state = RUNNING;

    if (entry != NULL) {
        entry(arg);
    }

    handle->state = STOPPED;
    return 0;
}

int join_thread(struct ThreadHandle *handle) {
    assert(handle != NULL);
    handle->state = STOPPED;
    return 0;  // Already finished (ran synchronously)
}

int detach_thread(struct ThreadHandle *handle) {
    assert(handle != NULL);
    handle->state = STOPPED;
    return 0;
}

void exit_thread() {
    // No-op in single-threaded mode
}

int stop_thread(struct ThreadHandle *handle) {
    assert(handle != NULL);
    handle->state = STOPPED;
    return 0;
}

int init_mutex(struct ThreadHandle *handle) {
    assert(handle != NULL);
    return 0;  // No-op
}

int destroy_mutex(struct ThreadHandle *handle) {
    assert(handle != NULL);
    return 0;  // No-op
}

int lock_mutex(struct ThreadHandle *handle) {
    assert(handle != NULL);
    return 0;  // No-op
}

int trylock_mutex(struct ThreadHandle *handle) {
    assert(handle != NULL);
    return 0;  // No-op (always succeeds)
}

int unlock_mutex(struct ThreadHandle *handle) {
    assert(handle != NULL);
    return 0;  // No-op
}

#endif /* WEB_USE_STANDALONE_THREAD */
