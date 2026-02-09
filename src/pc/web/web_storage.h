#ifndef WEB_STORAGE_H
#define WEB_STORAGE_H

/**
 * web_storage.h -- Persistent storage via Emscripten IDBFS for web builds.
 *
 * On native platforms, save files and config are written directly to disk via
 * standard fopen/fwrite/fclose. In the browser, Emscripten's virtual filesystem
 * (MEMFS) is volatile — all data is lost on page refresh. To persist data across
 * sessions, this module mounts IDBFS (IndexedDB-backed filesystem) at the save
 * directory (/save) and provides functions to synchronize between MEMFS and
 * IndexedDB.
 *
 * Usage:
 *   1. Call web_storage_init() once early in main(), after fs_init() but before
 *      any ROM checking or config loading. This mounts IDBFS and loads persisted
 *      data from IndexedDB into MEMFS.
 *   2. Call web_storage_save() after any file write that should persist (config
 *      saves, game saves, ROM loads). This flushes MEMFS changes to IndexedDB.
 *
 * On native builds, both functions compile to no-ops so callers do not need
 * additional #ifdef guards.
 */

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

/**
 * Initialize IDBFS persistent storage.
 *
 * Mounts IDBFS at the save directory path (/save) and synchronizes from
 * IndexedDB into MEMFS. This loads any previously persisted ROM, config,
 * and save data so the game can find them on startup.
 *
 * Must be called after fs_init() (which creates the /save directory) but
 * before ROM checking or config loading.
 *
 * Uses emscripten_sleep() with ASYNCIFY to wait for the sync to complete.
 */
void web_storage_init(void);

/**
 * Persist current MEMFS data to IndexedDB.
 *
 * Flushes all changes in the IDBFS-mounted directory to IndexedDB for
 * persistence across browser sessions. Call this after writing config files,
 * game saves, or loading a ROM from the file picker.
 *
 * This is an asynchronous operation internally — it fires the sync and
 * returns without waiting. Data loss is unlikely but possible if the
 * browser tab is closed immediately after calling.
 */
void web_storage_save(void);

#else /* !__EMSCRIPTEN__ */

/* Native builds: no-op stubs. */
static inline void web_storage_init(void) { (void)0; }
static inline void web_storage_save(void) { (void)0; }

#endif /* __EMSCRIPTEN__ */

#endif /* WEB_STORAGE_H */
