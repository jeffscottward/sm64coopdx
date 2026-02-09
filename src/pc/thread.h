#ifndef THREADING_H
#define THREADING_H

#include "cliopts.h"
#include "types.h"

// Types
enum ThreadState {
    INVALID = 0,
    STOPPED = 1,
    RUNNING = 2
};

#ifdef TARGET_WEB

// Web build: single-threaded stubs.
// No pthreads — thread entry functions are called directly on the main thread,
// and mutex operations are no-ops. This avoids the SharedArrayBuffer requirement.

struct ThreadHandle {
    int dummy;  // placeholder so sizeof(struct ThreadHandle) > 0
    enum ThreadState state;
};

// Mutex macros are no-ops on web since everything runs on the main thread.
#define MUTEX_LOCK(handle)   ((void)0)
#define MUTEX_UNLOCK(handle) ((void)0)

#else /* !TARGET_WEB */

#include <pthread.h>

// Macros
#define MUTEX_LOCK(handle) if (handle.state == RUNNING) { lock_mutex(&handle); }
#define MUTEX_UNLOCK(handle) if (handle.state == RUNNING) { unlock_mutex(&handle); }

struct ThreadHandle {
    pthread_t thread;
    pthread_mutex_t mutex;
    enum ThreadState state;
};

#endif /* TARGET_WEB */

// Functions
//// Thread Handle
int init_thread_handle(struct ThreadHandle *handle, void *(*entry)(void *), void *arg, void *sp, size_t sp_size);
void cleanup_thread_handle(struct ThreadHandle *handle);

//// Thread
int init_thread(struct ThreadHandle *handle, void *(*entry)(void *), void *arg, void *sp, size_t sp_size);
int join_thread(struct ThreadHandle *handle);
int detach_thread(struct ThreadHandle *handle);
void exit_thread();
int stop_thread(struct ThreadHandle *handle);

//// Mutex
int init_mutex(struct ThreadHandle *handle);
int destroy_mutex(struct ThreadHandle *handle);
int lock_mutex(struct ThreadHandle *handle);
int trylock_mutex(struct ThreadHandle *handle);
int unlock_mutex(struct ThreadHandle *handle);

#endif // THREADING_H
