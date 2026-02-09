#ifndef WEB_MOD_LOADER_H
#define WEB_MOD_LOADER_H

/**
 * web_mod_loader.h -- HTTP-based mod fetching for Emscripten web builds.
 *
 * On native platforms, users manually place mod files (.lua or directories
 * containing main.lua) into the mods/ directory. In the browser, this module
 * provides functions to download mods from URLs via HTTP, write them to the
 * Emscripten virtual filesystem, and persist them via IDBFS so they survive
 * page reloads.
 *
 * Supports both single .lua files and .zip mod packs. Downloaded mods are
 * written to the user's mods directory (fs_get_write_path("mods")) and can
 * be loaded by the existing mod pipeline via mods_refresh_local().
 *
 * A cache manifest (mods/.web_cache.json) tracks downloaded mods with URL,
 * timestamp, file hash, and size for cache invalidation.
 *
 * Usage:
 *   1. Call web_mod_download("https://example.com/mod.lua", "mod.lua") to
 *      download a mod synchronously (blocks via ASYNCIFY).
 *   2. Call web_mod_download_async() for non-blocking downloads with callback.
 *   3. After downloading, call mods_refresh_local() to pick up new mods.
 *   4. web_storage_save() is called automatically to persist to IndexedDB.
 *
 * On native builds, functions compile to no-ops / error returns so callers
 * do not need additional #ifdef guards.
 */

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

/** Status codes for mod download operations. */
#define WEB_MOD_OK             0   /* Download succeeded */
#define WEB_MOD_ERR_FETCH     -1   /* HTTP fetch failed (network error, CORS) */
#define WEB_MOD_ERR_WRITE     -2   /* Failed to write file to VFS */
#define WEB_MOD_ERR_BADURL    -3   /* URL is NULL, empty, or malformed */
#define WEB_MOD_ERR_TOOLARGE  -4   /* File exceeds size limit */

/** Maximum allowed mod file size (16 MB). */
#define WEB_MOD_MAX_SIZE (16 * 1024 * 1024)

/**
 * Download a mod file from a URL (blocking).
 *
 * Fetches the file at `url` via the browser Fetch API, writes the response
 * data to the mods directory in the virtual filesystem using `dest_filename`
 * as the filename. Handles both single .lua files and .zip mod packs.
 *
 * Blocks via emscripten_sleep() / ASYNCIFY until the download completes.
 * After a successful download, automatically calls web_storage_save() to
 * persist the mod to IndexedDB.
 *
 * @param url            HTTP(S) URL of the mod file to download.
 * @param dest_filename  Filename to save as in the mods directory (e.g., "mymod.lua").
 *                       If NULL, the filename is extracted from the URL.
 * @return WEB_MOD_OK on success, or a WEB_MOD_ERR_* code on failure.
 */
int web_mod_download(const char* url, const char* dest_filename);

/**
 * Callback type for async mod downloads.
 *
 * @param status  WEB_MOD_OK on success, or a WEB_MOD_ERR_* code on failure.
 */
typedef void (*web_mod_download_callback)(int status);

/**
 * Download a mod file from a URL (non-blocking).
 *
 * Same as web_mod_download() but returns immediately. The provided callback
 * is invoked when the download completes or fails.
 *
 * @param url            HTTP(S) URL of the mod file to download.
 * @param dest_filename  Filename to save as in the mods directory.
 *                       If NULL, the filename is extracted from the URL.
 * @param callback       Function called on completion with the status code.
 */
void web_mod_download_async(const char* url, const char* dest_filename,
                            web_mod_download_callback callback);

/**
 * Check if a mod URL is already cached and up-to-date.
 *
 * Reads the cache manifest (mods/.web_cache.json) and checks if the given
 * URL has been previously downloaded. Returns 1 if cached, 0 if not.
 *
 * @param url  URL to check in the cache.
 * @return 1 if the mod is cached, 0 otherwise.
 */
int web_mod_is_cached(const char* url);

/**
 * Update the cache manifest after a mod download.
 *
 * Adds or updates an entry in the cache manifest with the URL, filename,
 * current timestamp, file size, and a simple hash.
 *
 * @param url       URL the mod was downloaded from.
 * @param filename  Local filename in the mods directory.
 * @param size      File size in bytes.
 */
void web_mod_cache_update(const char* url, const char* filename, int size);

/**
 * Poll for async download completion.
 *
 * Call from the game loop to check if a pending async download has
 * finished. If complete, invokes the stored callback and cleans up.
 * Safe to call when no download is pending (returns immediately).
 */
void web_mod_check_async_complete(void);

/**
 * Get the cached filename for a URL.
 *
 * Looks up the URL in the cache manifest and copies the associated
 * local filename into buf. Returns 1 if found, 0 if not cached.
 *
 * @param url      URL to look up.
 * @param buf      Buffer to receive the filename.
 * @param bufsize  Size of buf.
 * @return 1 if found, 0 otherwise.
 */
int web_mod_cache_get_filename(const char* url, char* buf, int bufsize);

/**
 * Check if a cached mod is still fresh (file content matches manifest hash).
 *
 * Reads the cache manifest for the URL, finds the stored content hash,
 * then hashes the actual file on disk and compares. Returns 1 if the
 * cached mod is valid and matches, 0 if stale or missing.
 *
 * Use this before re-downloading: if fresh, the mod is already available.
 *
 * @param url  URL to check freshness for.
 * @return 1 if cached and fresh, 0 otherwise.
 */
int web_mod_cache_is_fresh(const char* url);

/**
 * Remove a URL's entry from the cache manifest.
 *
 * Rewrites the manifest excluding the entry for the given URL.
 * Does not delete the mod file itself from the VFS.
 *
 * @param url  URL whose cache entry should be removed.
 */
void web_mod_cache_remove(const char* url);

#else /* !__EMSCRIPTEN__ */

/* Native builds: no-op stubs. */
#define WEB_MOD_OK           0
#define WEB_MOD_ERR_FETCH   -1
#define WEB_MOD_ERR_WRITE   -2
#define WEB_MOD_ERR_BADURL  -3
#define WEB_MOD_ERR_TOOLARGE -4

typedef void (*web_mod_download_callback)(int status);

static inline int web_mod_download(const char* url, const char* dest_filename) {
    (void)url; (void)dest_filename;
    return WEB_MOD_ERR_FETCH;
}

static inline void web_mod_download_async(const char* url, const char* dest_filename,
                                          web_mod_download_callback callback) {
    (void)url; (void)dest_filename;
    if (callback) callback(WEB_MOD_ERR_FETCH);
}

static inline int web_mod_is_cached(const char* url) {
    (void)url;
    return 0;
}

static inline void web_mod_cache_update(const char* url, const char* filename, int size) {
    (void)url; (void)filename; (void)size;
}

static inline void web_mod_check_async_complete(void) { (void)0; }

static inline int web_mod_cache_get_filename(const char* url, char* buf, int bufsize) {
    (void)url; (void)buf; (void)bufsize;
    return 0;
}

static inline int web_mod_cache_is_fresh(const char* url) {
    (void)url;
    return 0;
}

static inline void web_mod_cache_remove(const char* url) {
    (void)url;
}

#endif /* __EMSCRIPTEN__ */

#endif /* WEB_MOD_LOADER_H */
