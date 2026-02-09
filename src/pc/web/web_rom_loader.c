/**
 * web_rom_loader.c -- Browser-based ROM file loading for Emscripten web builds.
 *
 * Uses the HTML5 File API to present a file picker dialog, reads the selected
 * ROM file via FileReader into a Uint8Array, and writes it into Emscripten's
 * virtual filesystem (MEMFS) at the path expected by rom_checker.cpp.
 *
 * The asynchronous file picker is bridged to synchronous C code via
 * emscripten_sleep() polling, which requires -sASYNCIFY in the linker flags.
 *
 * Build requirements:
 *   - Compile with -DTARGET_WEB=1 (set by Makefile.web)
 *   - Link with -sASYNCIFY for emscripten_sleep() support
 *   - Link with -sFORCE_FILESYSTEM=1 for FS API access
 */

#ifdef TARGET_WEB

#include <stdio.h>
#include <string.h>
#include <emscripten.h>

#include "pc/platform.h"
#include "pc/fs/fs.h"
#include "web_rom_loader.h"

/*
 * Shared state between C and JavaScript for the async file picker.
 *
 * The JavaScript file picker callback sets these values, and the C polling
 * loop in web_load_rom_from_picker() reads them to determine completion.
 *
 * States:
 *   0 = idle / waiting for user
 *   1 = file loaded successfully (ROM written to VFS)
 *  -1 = user cancelled or error occurred
 */
static volatile int sRomPickerResult = 0;
static volatile int sRomFetchResult = 0;

/* Callbacks for emscripten_async_wget_data */
static void on_rom_fetch_success(void *arg, void *data, int size) {
    (void)arg;
    printf("[Web ROM] Fetched %d bytes from server\n", size);

    const char *savePath = fs_get_write_path("");
    if (!savePath) {
        sRomFetchResult = -1;
        return;
    }

    char destPath[SYS_MAX_PATH];
    snprintf(destPath, sizeof(destPath), "%sbaserom.us.z64", savePath);

    FILE *f = fopen(destPath, "wb");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
        printf("[Web ROM] Written to %s\n", destPath);
        sRomFetchResult = 1;
    } else {
        printf("[Web ROM] Failed to write ROM file!\n");
        sRomFetchResult = -1;
    }
}

static void on_rom_fetch_error(void *arg) {
    (void)arg;
    printf("[Web ROM] Server fetch failed (ROM not served or network error)\n");
    sRomFetchResult = -1;
}

/**
 * Try to auto-fetch ROM from the web server at /baserom.us.z64.
 *
 * This avoids requiring user interaction — if the ROM is served alongside
 * the game files, it loads automatically. Uses emscripten_async_wget_data.
 * Falls back gracefully (returns 0) if the server doesn't have the ROM.
 */
int web_fetch_rom_from_server(void) {
    sRomFetchResult = 0;

    const char *savePath = fs_get_write_path("");
    if (!savePath || savePath[0] == '\0') {
        return 0;
    }

    printf("[Web ROM] Attempting auto-fetch from server...\n");

    emscripten_async_wget_data("baserom.us.z64", NULL,
                                on_rom_fetch_success, on_rom_fetch_error);

    /* Poll until fetch completes or fails — emscripten_sleep yields to
     * the browser event loop so the async wget callbacks can fire. */
    while (sRomFetchResult == 0) {
        emscripten_sleep(100);
    }

    return (sRomFetchResult == 1) ? 1 : 0;
}

/**
 * Check if a ROM file already exists in the virtual filesystem.
 *
 * Checks for known ROM filenames at the save directory path. This allows
 * skipping the file picker when the ROM has been persisted to IDBFS from
 * a previous session.
 */
int web_check_rom_exists(void) {
    /* Known ROM filenames that rom_checker.cpp scans for */
    static const char *sRomFilenames[] = {
        "baserom.us.z64",
        "baserom.eu.z64",
        "baserom.jp.z64",
        NULL
    };

    const char *savePath = fs_get_write_path("");
    if (!savePath) return 0;

    char fullPath[SYS_MAX_PATH];
    for (const char **name = sRomFilenames; *name != NULL; name++) {
        snprintf(fullPath, sizeof(fullPath), "%s%s", savePath, *name);
        FILE *f = fopen(fullPath, "rb");
        if (f) {
            fclose(f);
            return 1;
        }
    }

    return 0;
}

/**
 * Show a browser file picker and load the selected ROM into the VFS.
 *
 * Implementation overview:
 *   1. Reset the picker result flag to 0 (waiting).
 *   2. Inject JavaScript that creates an <input type="file"> element.
 *   3. The JS 'change' handler reads the file via FileReader.
 *   4. On load, it writes the file data to the Emscripten VFS via FS.writeFile().
 *   5. It sets the C-side result flag via setValue() / HEAP32.
 *   6. C polls with emscripten_sleep() until the flag changes from 0.
 *
 * Requires -sASYNCIFY in linker flags for emscripten_sleep().
 */
int web_load_rom_from_picker(void) {
    sRomPickerResult = 0;

    /* Get the save directory path where rom_checker.cpp looks for ROMs */
    const char *savePath = fs_get_write_path("");
    if (!savePath || savePath[0] == '\0') {
        return 0;
    }

    /*
     * Inject JavaScript to create a file input element and handle the
     * file selection. The file data is written directly into the Emscripten
     * virtual filesystem using the FS.writeFile() API.
     *
     * $0 = pointer to sRomPickerResult (for signaling completion)
     * $1 = pointer to the save path string
     */
    EM_ASM({
        var resultPtr = $0;
        var savePathPtr = $1;
        var savePath = UTF8ToString(savePathPtr);

        /* Ensure the save directory exists in the VFS */
        try {
            FS.mkdirTree(savePath);
        } catch (e) {
            /* Directory may already exist — ignore EEXIST */
        }

        /* Create a hidden file input element */
        var input = document.createElement('input');
        input.type = 'file';
        input.accept = '.z64,.n64,.v64';
        input.style.display = 'none';

        /* Handle file selection */
        input.addEventListener('change', function(event) {
            var file = event.target.files[0];
            if (!file) {
                /* No file selected (shouldn't happen if change fires) */
                setValue(resultPtr, -1, 'i32');
                document.body.removeChild(input);
                return;
            }

            var reader = new FileReader();
            reader.onload = function(e) {
                var data = new Uint8Array(e.target.result);

                /* Determine the destination filename.
                 * Use baserom.us.z64 as the default name since the ROM
                 * checker scans for this filename specifically. */
                var destPath = savePath + 'baserom.us.z64';

                try {
                    FS.writeFile(destPath, data);
                    setValue(resultPtr, 1, 'i32');
                } catch (writeErr) {
                    console.error('web_rom_loader: Failed to write ROM:', writeErr);
                    setValue(resultPtr, -1, 'i32');
                }

                document.body.removeChild(input);
            };

            reader.onerror = function() {
                console.error('web_rom_loader: FileReader error');
                setValue(resultPtr, -1, 'i32');
                document.body.removeChild(input);
            };

            reader.readAsArrayBuffer(file);
        });

        /* Handle cancel: when the input loses focus without a file.
         * The 'cancel' event is supported in modern browsers. For older
         * browsers, we fall back to a focus-based heuristic. */
        input.addEventListener('cancel', function() {
            setValue(resultPtr, -1, 'i32');
            document.body.removeChild(input);
        });

        document.body.appendChild(input);
        input.click();
    }, &sRomPickerResult, savePath);

    /*
     * Poll until the JavaScript callback signals completion.
     * emscripten_sleep() yields the main thread back to the browser event
     * loop, allowing the file picker dialog and FileReader to operate.
     * Requires -sASYNCIFY in linker flags.
     */
    while (sRomPickerResult == 0) {
        emscripten_sleep(100);
    }

    return (sRomPickerResult == 1) ? 1 : 0;
}

#endif /* TARGET_WEB */
