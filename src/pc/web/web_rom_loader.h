#ifndef WEB_ROM_LOADER_H
#define WEB_ROM_LOADER_H

/**
 * web_rom_loader.h -- Browser-based ROM file loading for Emscripten web builds.
 *
 * On native platforms, the user provides a ROM file by placing it in the game
 * directory or drag-and-dropping it onto the window. In the browser, neither
 * mechanism is available. Instead, this module uses the HTML5 File API to
 * present a file picker, reads the selected file into memory via FileReader,
 * and writes it into Emscripten's virtual filesystem (MEMFS) at the path
 * expected by rom_checker.cpp.
 *
 * The file picker is asynchronous by nature (the user must interact with the
 * browser dialog), so web_load_rom_from_picker() uses emscripten_sleep() with
 * ASYNCIFY to yield the C main thread while waiting for the JavaScript
 * callback to complete.
 *
 * On native builds, both functions compile to simple stubs (always-false /
 * always-fail) so callers do not need additional #ifdef guards.
 */

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

/**
 * Check if a ROM file already exists in the virtual filesystem.
 *
 * Looks for known ROM filenames (baserom.us.z64, etc.) at the path returned
 * by fs_get_write_path(""). This is used on startup to skip the file picker
 * when the ROM has been persisted to IDBFS from a previous session.
 *
 * @return 1 if a ROM file exists, 0 otherwise.
 */
int web_check_rom_exists(void);

/**
 * Try to auto-fetch ROM from the web server (e.g. /baserom.us.z64).
 * Returns 1 if ROM was fetched and written to VFS, 0 on failure.
 * This avoids requiring user interaction when ROM is served alongside game.
 */
int web_fetch_rom_from_server(void);

/**
 * Show a browser file picker and load the selected ROM into the VFS.
 *
 * Creates an HTML <input type="file"> element, waits for the user to select
 * a .z64/.n64/.v64 file, reads it with the FileReader API, and writes it to
 * the Emscripten virtual filesystem at the save directory path.
 *
 * This function blocks (via emscripten_sleep / ASYNCIFY) until the user
 * selects a file or cancels the dialog.
 *
 * @return 1 if the ROM was successfully loaded into the VFS, 0 on cancel or error.
 */
int web_load_rom_from_picker(void);

#else /* !__EMSCRIPTEN__ */

/* Native builds: stubs that compile to nothing useful. */
static inline int web_check_rom_exists(void) { return 0; }
static inline int web_fetch_rom_from_server(void) { return 0; }
static inline int web_load_rom_from_picker(void) { return 0; }

#endif /* __EMSCRIPTEN__ */

#endif /* WEB_ROM_LOADER_H */
