/**
 * web_mod_loader.c -- HTTP-based mod fetching for Emscripten web builds.
 *
 * Downloads mod files (.lua or .zip) from URLs via the browser Fetch API,
 * writes them to the Emscripten virtual filesystem mods directory, and
 * persists them to IndexedDB via web_storage_save().
 *
 * Build requirements:
 *   - Compile with -DTARGET_WEB=1 (set by Makefile.web)
 *   - Link with -sASYNCIFY for emscripten_sleep() support
 *   - Link with -sFORCE_FILESYSTEM=1 for FS API access
 *   - Link with -lidbfs.js for IDBFS persistence
 */

#ifdef TARGET_WEB

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <emscripten.h>

#include "web_mod_loader.h"
#include "web_storage.h"
#include "web_compat.h"
#include "pc/platform.h"
#include "pc/fs/fs.h"
#include "pc/mods/mods.h"

/*
 * Shared state for async download operations.
 *
 * States:
 *   0 = download in progress
 *   1 = download completed successfully
 *  -1 = fetch error (network, CORS)
 *  -2 = filesystem write error
 *  -3 = bad URL
 *  -4 = file too large
 */
static volatile int sDownloadResult = 0;

/* Size of the last downloaded file (set by JS callback). */
static volatile int sDownloadSize = 0;

/*
 * Extract filename from a URL.
 *
 * Given "https://example.com/path/to/mymod.lua?v=1", returns "mymod.lua".
 * If no filename can be extracted, returns "downloaded_mod.lua".
 *
 * The result is written into the provided buffer.
 */
static void extract_filename_from_url(const char* url, char* buf, int bufsize) {
    if (!url || !buf || bufsize <= 0) return;

    /* Find the last '/' in the URL */
    const char* lastSlash = strrchr(url, '/');
    const char* filename = lastSlash ? (lastSlash + 1) : url;

    /* Strip query string and fragment */
    int len = 0;
    while (filename[len] != '\0' && filename[len] != '?' && filename[len] != '#') {
        len++;
    }

    if (len == 0 || len >= bufsize) {
        snprintf(buf, bufsize, "downloaded_mod.lua");
        return;
    }

    memcpy(buf, filename, len);
    buf[len] = '\0';
}

/*
 * Simple string hash (djb2) for cache manifest.
 * Returns a 32-bit hash of the input string.
 */
static unsigned int simple_hash(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

/*
 * Hash file contents on disk.
 * Reads the file at the given path and computes a djb2 hash of its bytes.
 * Returns 0 if the file cannot be read.
 */
static unsigned int hash_file_contents(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return 0;

    unsigned int hash = 5381;
    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        for (size_t i = 0; i < n; i++) {
            hash = ((hash << 5) + hash) + buf[i];
        }
    }
    fclose(fp);
    return hash;
}

/**
 * Start an async fetch of a mod file from a URL.
 *
 * Uses the browser Fetch API via EM_ASM. The JavaScript code:
 *   1. Calls fetch() on the URL
 *   2. Reads the response as an ArrayBuffer
 *   3. Validates the size against the limit
 *   4. Writes the data to the VFS via FS.writeFile()
 *   5. Sets the result flag and size via setValue()
 *
 * We avoid EM_JS here to keep the pattern consistent with other web_*.c files.
 * Commas in the JS are safe here because they are inside parentheses (function
 * calls) which the C preprocessor tracks correctly.
 */
static void start_fetch(const char* url, const char* destPath, int maxSize) {
    sDownloadResult = 0;
    sDownloadSize = 0;

    EM_ASM({
        var urlStr = UTF8ToString($0);
        var destStr = UTF8ToString($1);
        var maxSz = $2;
        var resultPtr = $3;
        var sizePtr = $4;

        /* Ensure parent directory exists */
        var parentDir = destStr.substring(0, destStr.lastIndexOf('/'));
        try {
            FS.mkdirTree(parentDir);
        } catch (e) {
            /* Directory may already exist */
        }

        fetch(urlStr).then(function(response) {
            if (!response.ok) {
                console.error('web_mod_loader: HTTP ' + response.status + ' for ' + urlStr);
                setValue(resultPtr, -1, 'i32');
                return null;
            }
            return response.arrayBuffer();
        }).then(function(buffer) {
            if (buffer === null) return;

            if (buffer.byteLength > maxSz) {
                console.error('web_mod_loader: File too large (' + buffer.byteLength + ' > ' + maxSz + ')');
                setValue(resultPtr, -4, 'i32');
                return;
            }

            var data = new Uint8Array(buffer);
            try {
                FS.writeFile(destStr, data);
                setValue(sizePtr, buffer.byteLength, 'i32');
                setValue(resultPtr, 1, 'i32');
                console.log('web_mod_loader: Downloaded ' + buffer.byteLength + ' bytes to ' + destStr);
            } catch (writeErr) {
                console.error('web_mod_loader: Write failed:', writeErr);
                setValue(resultPtr, -2, 'i32');
            }
        })['catch'](function(err) {
            console.error('web_mod_loader: Fetch error:', err);
            setValue(resultPtr, -1, 'i32');
        });
    }, url, destPath, maxSize, &sDownloadResult, &sDownloadSize);
}

/**
 * Download a mod file from a URL (blocking).
 */
int web_mod_download(const char* url, const char* dest_filename) {
    if (!url || url[0] == '\0') {
        return WEB_MOD_ERR_BADURL;
    }

    /* Resolve destination path */
    const char* modsPath = fs_get_write_path("mods");
    if (!modsPath) {
        printf("web_mod_loader: Could not resolve mods directory\n");
        return WEB_MOD_ERR_WRITE;
    }

    /* Determine filename */
    char filename[256];
    if (dest_filename && dest_filename[0] != '\0') {
        snprintf(filename, sizeof(filename), "%s", dest_filename);
    } else {
        extract_filename_from_url(url, filename, sizeof(filename));
    }

    /* Build full destination path */
    char destPath[SYS_MAX_PATH];
    snprintf(destPath, sizeof(destPath), "%s/%s", modsPath, filename);

    printf("web_mod_loader: Downloading %s -> %s\n", url, destPath);

    /* Start the async fetch */
    start_fetch(url, destPath, WEB_MOD_MAX_SIZE);

    /* Poll until complete (ASYNCIFY yields to browser event loop) */
    while (sDownloadResult == 0) {
        emscripten_sleep(50);
    }

    if (sDownloadResult == 1) {
        printf("web_mod_loader: Download complete (%d bytes)\n", (int)sDownloadSize);

        /* Update cache manifest */
        web_mod_cache_update(url, filename, (int)sDownloadSize);

        /* Persist to IndexedDB */
        web_storage_save();

        return WEB_MOD_OK;
    }

    printf("web_mod_loader: Download failed (status %d)\n", (int)sDownloadResult);

    /* Map JS error codes to our error codes */
    switch ((int)sDownloadResult) {
        case -1: return WEB_MOD_ERR_FETCH;
        case -2: return WEB_MOD_ERR_WRITE;
        case -4: return WEB_MOD_ERR_TOOLARGE;
        default: return WEB_MOD_ERR_FETCH;
    }
}

/*
 * State for async download callback.
 * We store the callback and URL/filename so the polling function can
 * invoke the callback on completion.
 */
static web_mod_download_callback sAsyncCallback = NULL;
static char sAsyncUrl[1024];
static char sAsyncFilename[256];
static volatile int sAsyncPending = 0;

/**
 * Download a mod file from a URL (non-blocking).
 */
void web_mod_download_async(const char* url, const char* dest_filename,
                            web_mod_download_callback callback) {
    if (!url || url[0] == '\0') {
        if (callback) callback(WEB_MOD_ERR_BADURL);
        return;
    }

    if (sAsyncPending) {
        /* A download is already in progress */
        if (callback) callback(WEB_MOD_ERR_FETCH);
        return;
    }

    /* Store callback state */
    sAsyncCallback = callback;
    snprintf(sAsyncUrl, sizeof(sAsyncUrl), "%s", url);

    if (dest_filename && dest_filename[0] != '\0') {
        snprintf(sAsyncFilename, sizeof(sAsyncFilename), "%s", dest_filename);
    } else {
        extract_filename_from_url(url, sAsyncFilename, sizeof(sAsyncFilename));
    }

    /* Resolve destination path */
    const char* modsPath = fs_get_write_path("mods");
    if (!modsPath) {
        if (callback) callback(WEB_MOD_ERR_WRITE);
        return;
    }

    char destPath[SYS_MAX_PATH];
    snprintf(destPath, sizeof(destPath), "%s/%s", modsPath, sAsyncFilename);

    sAsyncPending = 1;

    /* Start fetch — completion is checked by the game loop or a timer */
    start_fetch(url, destPath, WEB_MOD_MAX_SIZE);

    /*
     * Set up a polling check via emscripten_async_call.
     * We use a simple EM_ASM timer to poll sDownloadResult and invoke
     * the callback when done.
     */
    EM_ASM({
        var resultPtr = $0;
        var sizePtr = $1;
        var pendingPtr = $2;

        function checkDone() {
            var result = getValue(resultPtr, 'i32');
            if (result === 0) {
                setTimeout(checkDone, 50);
                return;
            }
            /* Download finished — the C callback will be invoked from
             * web_mod_check_async_complete() on the next game loop tick */
        }
        setTimeout(checkDone, 50);
    }, &sDownloadResult, &sDownloadSize, &sAsyncPending);
}

/**
 * Check if an async download has completed and invoke the callback.
 *
 * Call this from the game loop or mod panel update to poll for async
 * download completion. Safe to call even when no download is pending.
 */
void web_mod_check_async_complete(void) {
    if (!sAsyncPending) return;
    if (sDownloadResult == 0) return; /* Still in progress */

    sAsyncPending = 0;

    int status;
    if (sDownloadResult == 1) {
        web_mod_cache_update(sAsyncUrl, sAsyncFilename, (int)sDownloadSize);
        web_storage_save();
        status = WEB_MOD_OK;
    } else {
        switch ((int)sDownloadResult) {
            case -2: status = WEB_MOD_ERR_WRITE; break;
            case -4: status = WEB_MOD_ERR_TOOLARGE; break;
            default: status = WEB_MOD_ERR_FETCH; break;
        }
    }

    if (sAsyncCallback) {
        sAsyncCallback(status);
        sAsyncCallback = NULL;
    }
}

/**
 * Check if a mod URL is already cached.
 *
 * Reads the cache manifest file and checks for a matching URL entry.
 * Uses simple line-by-line parsing of the JSON-like manifest.
 */
int web_mod_is_cached(const char* url) {
    if (!url || url[0] == '\0') return 0;

    const char* modsPath = fs_get_write_path("mods");
    if (!modsPath) return 0;

    char manifestPath[SYS_MAX_PATH];
    snprintf(manifestPath, sizeof(manifestPath), "%s/.web_cache.json", modsPath);

    FILE* f = fopen(manifestPath, "r");
    if (!f) return 0;

    /* Simple search: look for the URL string in the manifest */
    char line[1024];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, url)) {
            found = 1;
            break;
        }
    }

    fclose(f);
    return found;
}

/**
 * Update the cache manifest after a successful download.
 *
 * The manifest is a simple JSON array of entries. Each entry records
 * the URL, local filename, download timestamp, file hash, and size.
 *
 * We write the entire manifest by reading existing entries and appending
 * the new one, or updating an existing entry for the same URL.
 */
void web_mod_cache_update(const char* url, const char* filename, int size) {
    if (!url || !filename) return;

    const char* modsPath = fs_get_write_path("mods");
    if (!modsPath) return;

    char manifestPath[SYS_MAX_PATH];
    snprintf(manifestPath, sizeof(manifestPath), "%s/.web_cache.json", modsPath);

    /* Read existing manifest entries (up to 64 entries) */
    #define MAX_CACHE_ENTRIES 64

    struct CacheEntry {
        char url[1024];
        char filename[256];
        unsigned int hash;
        int size;
        long timestamp;
    };

    struct CacheEntry entries[MAX_CACHE_ENTRIES];
    int entryCount = 0;

    FILE* f = fopen(manifestPath, "r");
    if (f) {
        /* Parse existing entries — look for "url": "..." lines */
        char line[1024];
        struct CacheEntry* cur = NULL;

        while (fgets(line, sizeof(line), f) && entryCount < MAX_CACHE_ENTRIES) {
            char* urlField = strstr(line, "\"url\":");
            if (urlField) {
                cur = &entries[entryCount];
                memset(cur, 0, sizeof(*cur));

                /* Extract URL value */
                char* quote1 = strchr(urlField + 6, '"');
                if (quote1) {
                    char* quote2 = strchr(quote1 + 1, '"');
                    if (quote2) {
                        int len = (int)(quote2 - quote1 - 1);
                        if (len > 0 && len < (int)sizeof(cur->url)) {
                            memcpy(cur->url, quote1 + 1, len);
                            cur->url[len] = '\0';
                        }
                    }
                }
            }

            if (cur) {
                char* fnField = strstr(line, "\"filename\":");
                if (fnField) {
                    char* q1 = strchr(fnField + 11, '"');
                    if (q1) {
                        char* q2 = strchr(q1 + 1, '"');
                        if (q2) {
                            int len = (int)(q2 - q1 - 1);
                            if (len > 0 && len < (int)sizeof(cur->filename)) {
                                memcpy(cur->filename, q1 + 1, len);
                                cur->filename[len] = '\0';
                            }
                        }
                    }
                }

                char* sizeField = strstr(line, "\"size\":");
                if (sizeField) {
                    cur->size = atoi(sizeField + 7);
                }

                char* tsField = strstr(line, "\"timestamp\":");
                if (tsField) {
                    cur->timestamp = atol(tsField + 12);
                }

                char* hashField = strstr(line, "\"hash\":");
                if (hashField) {
                    cur->hash = (unsigned int)strtoul(hashField + 7, NULL, 10);
                }

                /* End of entry — look for closing brace */
                if (strchr(line, '}') && cur->url[0] != '\0') {
                    entryCount++;
                    cur = NULL;
                }
            }
        }
        fclose(f);
    }

    /* Find existing entry for this URL, or use a new slot */
    int idx = -1;
    for (int i = 0; i < entryCount; i++) {
        if (strcmp(entries[i].url, url) == 0) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        if (entryCount >= MAX_CACHE_ENTRIES) {
            /* Evict oldest entry */
            idx = 0;
            for (int i = 1; i < entryCount; i++) {
                if (entries[i].timestamp < entries[idx].timestamp) {
                    idx = i;
                }
            }
        } else {
            idx = entryCount;
            entryCount++;
        }
    }

    /* Update the entry — hash file contents for cache invalidation */
    snprintf(entries[idx].url, sizeof(entries[idx].url), "%s", url);
    snprintf(entries[idx].filename, sizeof(entries[idx].filename), "%s", filename);
    entries[idx].size = size;
    entries[idx].timestamp = (long)time(NULL);

    /* Compute content hash from the actual file on disk */
    char filePath[SYS_MAX_PATH];
    snprintf(filePath, sizeof(filePath), "%s/%s", modsPath, filename);
    entries[idx].hash = hash_file_contents(filePath);
    if (entries[idx].hash == 0) {
        /* Fallback to URL hash if file read failed */
        entries[idx].hash = simple_hash(url);
    }

    /* Write the manifest */
    f = fopen(manifestPath, "w");
    if (!f) {
        printf("web_mod_loader: Could not write cache manifest\n");
        return;
    }

    fprintf(f, "[\n");
    for (int i = 0; i < entryCount; i++) {
        fprintf(f, "  {\n");
        fprintf(f, "    \"url\": \"%s\",\n", entries[i].url);
        fprintf(f, "    \"filename\": \"%s\",\n", entries[i].filename);
        fprintf(f, "    \"hash\": %u,\n", entries[i].hash);
        fprintf(f, "    \"size\": %d,\n", entries[i].size);
        fprintf(f, "    \"timestamp\": %ld\n", entries[i].timestamp);
        fprintf(f, "  }%s\n", (i < entryCount - 1) ? "," : "");
    }
    fprintf(f, "]\n");
    fclose(f);
}

/**
 * Get the cached filename for a given URL.
 *
 * Looks up the URL in the cache manifest and copies the associated
 * filename into the provided buffer. Returns 1 if found, 0 if not.
 */
int web_mod_cache_get_filename(const char* url, char* buf, int bufsize) {
    if (!url || url[0] == '\0' || !buf || bufsize <= 0) return 0;

    const char* modsPath = fs_get_write_path("mods");
    if (!modsPath) return 0;

    char manifestPath[SYS_MAX_PATH];
    snprintf(manifestPath, sizeof(manifestPath), "%s/.web_cache.json", modsPath);

    FILE* f = fopen(manifestPath, "r");
    if (!f) return 0;

    char line[1024];
    int foundUrl = 0;
    while (fgets(line, sizeof(line), f)) {
        char* urlField = strstr(line, "\"url\":");
        if (urlField) {
            char* q1 = strchr(urlField + 6, '"');
            if (q1) {
                char* q2 = strchr(q1 + 1, '"');
                if (q2) {
                    int len = (int)(q2 - q1 - 1);
                    if (len > 0 && len < 1024) {
                        char tmpUrl[1024];
                        memcpy(tmpUrl, q1 + 1, len);
                        tmpUrl[len] = '\0';
                        foundUrl = (strcmp(tmpUrl, url) == 0);
                    }
                }
            }
        }

        if (foundUrl) {
            char* fnField = strstr(line, "\"filename\":");
            if (fnField) {
                char* q1 = strchr(fnField + 11, '"');
                if (q1) {
                    char* q2 = strchr(q1 + 1, '"');
                    if (q2) {
                        int len = (int)(q2 - q1 - 1);
                        if (len > 0 && len < bufsize) {
                            memcpy(buf, q1 + 1, len);
                            buf[len] = '\0';
                            fclose(f);
                            return 1;
                        }
                    }
                }
            }
            /* Entry ends at closing brace */
            if (strchr(line, '}')) {
                foundUrl = 0;
            }
        }
    }
    fclose(f);
    return 0;
}

/**
 * Check if a cached mod file is fresh (content hash matches).
 *
 * Reads the cache manifest for the given URL, finds the stored hash,
 * then hashes the actual file on disk and compares. Returns 1 if the
 * file exists and its content hash matches the manifest, 0 otherwise.
 *
 * This allows detecting if a cached mod was modified externally or if
 * the on-disk file has been deleted.
 */
int web_mod_cache_is_fresh(const char* url) {
    if (!url || url[0] == '\0') return 0;

    const char* modsPath = fs_get_write_path("mods");
    if (!modsPath) return 0;

    char manifestPath[SYS_MAX_PATH];
    snprintf(manifestPath, sizeof(manifestPath), "%s/.web_cache.json", modsPath);

    FILE* f = fopen(manifestPath, "r");
    if (!f) return 0;

    /* Parse manifest to find the entry for this URL */
    char line[1024];
    int foundUrl = 0;
    char filename[256] = {0};
    unsigned int storedHash = 0;

    while (fgets(line, sizeof(line), f)) {
        char* urlField = strstr(line, "\"url\":");
        if (urlField) {
            char* q1 = strchr(urlField + 6, '"');
            if (q1) {
                char* q2 = strchr(q1 + 1, '"');
                if (q2) {
                    int len = (int)(q2 - q1 - 1);
                    if (len > 0 && len < 1024) {
                        char tmpUrl[1024];
                        memcpy(tmpUrl, q1 + 1, len);
                        tmpUrl[len] = '\0';
                        foundUrl = (strcmp(tmpUrl, url) == 0);
                    }
                }
            }
        }

        if (foundUrl) {
            char* fnField = strstr(line, "\"filename\":");
            if (fnField) {
                char* q1 = strchr(fnField + 11, '"');
                if (q1) {
                    char* q2 = strchr(q1 + 1, '"');
                    if (q2) {
                        int len = (int)(q2 - q1 - 1);
                        if (len > 0 && len < (int)sizeof(filename)) {
                            memcpy(filename, q1 + 1, len);
                            filename[len] = '\0';
                        }
                    }
                }
            }

            char* hashField = strstr(line, "\"hash\":");
            if (hashField) {
                storedHash = (unsigned int)strtoul(hashField + 7, NULL, 10);
            }

            if (strchr(line, '}')) {
                break; /* End of this entry */
            }
        }
    }
    fclose(f);

    if (!foundUrl || filename[0] == '\0' || storedHash == 0) return 0;

    /* Hash the actual file on disk and compare */
    char filePath[SYS_MAX_PATH];
    snprintf(filePath, sizeof(filePath), "%s/%s", modsPath, filename);

    unsigned int diskHash = hash_file_contents(filePath);
    if (diskHash == 0) return 0; /* File doesn't exist or is empty */

    return (diskHash == storedHash) ? 1 : 0;
}

/**
 * Remove a URL's entry from the cache manifest.
 *
 * Rewrites the manifest file excluding the entry for the given URL.
 * Does not delete the mod file itself — only removes the cache tracking.
 */
void web_mod_cache_remove(const char* url) {
    if (!url || url[0] == '\0') return;

    const char* modsPath = fs_get_write_path("mods");
    if (!modsPath) return;

    char manifestPath[SYS_MAX_PATH];
    snprintf(manifestPath, sizeof(manifestPath), "%s/.web_cache.json", modsPath);

    /* Reuse the CacheEntry struct pattern from web_mod_cache_update */
    #ifndef MAX_CACHE_ENTRIES
    #define MAX_CACHE_ENTRIES 64
    #endif

    struct CacheEntry {
        char url[1024];
        char filename[256];
        unsigned int hash;
        int size;
        long timestamp;
    };

    struct CacheEntry entries[MAX_CACHE_ENTRIES];
    int entryCount = 0;

    FILE* f = fopen(manifestPath, "r");
    if (!f) return;

    char line[1024];
    struct CacheEntry* cur = NULL;

    while (fgets(line, sizeof(line), f) && entryCount < MAX_CACHE_ENTRIES) {
        char* urlField = strstr(line, "\"url\":");
        if (urlField) {
            cur = &entries[entryCount];
            memset(cur, 0, sizeof(*cur));

            char* quote1 = strchr(urlField + 6, '"');
            if (quote1) {
                char* quote2 = strchr(quote1 + 1, '"');
                if (quote2) {
                    int len = (int)(quote2 - quote1 - 1);
                    if (len > 0 && len < (int)sizeof(cur->url)) {
                        memcpy(cur->url, quote1 + 1, len);
                        cur->url[len] = '\0';
                    }
                }
            }
        }

        if (cur) {
            char* fnField = strstr(line, "\"filename\":");
            if (fnField) {
                char* q1 = strchr(fnField + 11, '"');
                if (q1) {
                    char* q2 = strchr(q1 + 1, '"');
                    if (q2) {
                        int len = (int)(q2 - q1 - 1);
                        if (len > 0 && len < (int)sizeof(cur->filename)) {
                            memcpy(cur->filename, q1 + 1, len);
                            cur->filename[len] = '\0';
                        }
                    }
                }
            }

            char* sizeField = strstr(line, "\"size\":");
            if (sizeField) cur->size = atoi(sizeField + 7);

            char* tsField = strstr(line, "\"timestamp\":");
            if (tsField) cur->timestamp = atol(tsField + 12);

            char* hashField = strstr(line, "\"hash\":");
            if (hashField) cur->hash = (unsigned int)strtoul(hashField + 7, NULL, 10);

            if (strchr(line, '}') && cur->url[0] != '\0') {
                entryCount++;
                cur = NULL;
            }
        }
    }
    fclose(f);

    /* Rewrite the manifest, excluding the entry matching this URL */
    f = fopen(manifestPath, "w");
    if (!f) return;

    int writeCount = 0;
    /* Count how many entries we'll keep */
    int keepCount = 0;
    for (int i = 0; i < entryCount; i++) {
        if (strcmp(entries[i].url, url) != 0) keepCount++;
    }

    fprintf(f, "[\n");
    for (int i = 0; i < entryCount; i++) {
        if (strcmp(entries[i].url, url) == 0) continue;

        fprintf(f, "  {\n");
        fprintf(f, "    \"url\": \"%s\",\n", entries[i].url);
        fprintf(f, "    \"filename\": \"%s\",\n", entries[i].filename);
        fprintf(f, "    \"hash\": %u,\n", entries[i].hash);
        fprintf(f, "    \"size\": %d,\n", entries[i].size);
        fprintf(f, "    \"timestamp\": %ld\n", entries[i].timestamp);
        writeCount++;
        fprintf(f, "  }%s\n", (writeCount < keepCount) ? "," : "");
    }
    fprintf(f, "]\n");
    fclose(f);
}

#endif /* TARGET_WEB */
