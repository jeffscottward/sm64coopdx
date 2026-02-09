/**
 * web_storage.c -- Persistent storage via Emscripten IDBFS for web builds.
 *
 * Mounts IDBFS at the /save directory and provides synchronization functions
 * to persist game data (ROM, config, saves) to IndexedDB across browser sessions.
 *
 * Build requirements:
 *   - Compile with -DTARGET_WEB=1 (set by Makefile.web)
 *   - Link with -sASYNCIFY for emscripten_sleep() support
 *   - Link with -lidbfs.js for IDBFS support
 *   - Link with -sFORCE_FILESYSTEM=1 for FS API access
 */

#ifdef TARGET_WEB

#include <stdio.h>
#include <emscripten.h>

#include "web_storage.h"
#include "web_compat.h"

/*
 * Flag set by the JavaScript FS.syncfs() callback to signal completion.
 * Used by web_storage_init() to poll until the initial load finishes.
 *
 * States:
 *   0 = sync in progress
 *   1 = sync completed successfully
 *  -1 = sync failed
 */
static volatile int sSyncInitDone = 0;

/*
 * Guard flag to prevent concurrent FS.syncfs() calls.
 * Emscripten warns "2 FS.syncfs operations in flight at once" when
 * two syncs overlap — this flag prevents that race.
 */
static volatile int sSyncInFlight = 0;

/**
 * Initialize IDBFS persistent storage.
 *
 * Implementation:
 *   1. Mount IDBFS at the save directory (/save).
 *   2. Call FS.syncfs(true, ...) to populate MEMFS from IndexedDB.
 *   3. Poll with emscripten_sleep() until the async callback completes.
 *
 * The 'true' parameter to FS.syncfs means "populate" — read from the
 * persistent store (IndexedDB) into the in-memory filesystem (MEMFS).
 */
void web_storage_init(void) {
    sSyncInitDone = 0;

    EM_ASM({
        var mountPoint = UTF8ToString($0);
        var donePtr = $1;

        // Mount IDBFS at the save directory.
        // If already mounted (e.g., from a previous call), unmount first.
        try {
            FS.mount(IDBFS, {}, mountPoint);
        } catch (e) {
            // IDBFS may already be mounted — this is not fatal.
            // Emscripten throws if a filesystem is already mounted at the path.
            console.warn('web_storage: IDBFS mount warning:', e.message);
        }

        // Sync FROM IndexedDB INTO MEMFS (populate = true).
        FS.syncfs(true, function(err) {
            if (err) {
                console.error('web_storage: Failed to load from IndexedDB:', err);
                setValue(donePtr, -1, 'i32');
            } else {
                console.log('web_storage: Loaded persistent data from IndexedDB');
                setValue(donePtr, 1, 'i32');
            }
        });
    }, WEB_USER_PATH, &sSyncInitDone);

    /* Poll until the async sync callback fires. */
    while (sSyncInitDone == 0) {
        emscripten_sleep(50);
    }

    if (sSyncInitDone < 0) {
        printf("web_storage: WARNING — failed to load persistent data from IndexedDB\n");
    } else {
        printf("web_storage: Persistent storage initialized at %s\n", WEB_USER_PATH);
    }
}

/**
 * Persist current MEMFS data to IndexedDB.
 *
 * Calls FS.syncfs(false, ...) which means "persist" — write from the
 * in-memory filesystem (MEMFS) to the persistent store (IndexedDB).
 *
 * This is fire-and-forget: the function returns immediately while the
 * sync happens asynchronously. Any errors are logged to the console.
 */
void web_storage_save(void) {
    if (sSyncInFlight) {
        /* A sync is already in progress — skip to avoid the Emscripten
           "2 FS.syncfs operations in flight at once" warning. */
        return;
    }
    sSyncInFlight = 1;

    EM_ASM({
        var guardPtr = $0;
        FS.syncfs(false, function(err) {
            if (err) {
                console.error('web_storage: Failed to persist to IndexedDB:', err);
            }
            setValue(guardPtr, 0, 'i32');
        });
    }, &sSyncInFlight);
}

#endif /* TARGET_WEB */
