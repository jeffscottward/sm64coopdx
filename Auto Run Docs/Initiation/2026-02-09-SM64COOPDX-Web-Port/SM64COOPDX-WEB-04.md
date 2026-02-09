# Phase 04: ROM Loading and Browser File Integration

This phase implements the browser-based ROM loading workflow. Since SM64 ROM distribution is legally restricted, the web port must prompt users to provide their own `baserom.us.z64` file via a browser file picker. The ROM data is loaded into Emscripten's virtual filesystem (MEMFS), validated, and then the game proceeds with asset extraction as normal. This phase also sets up persistent storage via IndexedDB (IDBFS) so that the ROM and save data persist across browser sessions, eliminating the need to re-upload the ROM every time.

## Tasks

- [x] Create `src/pc/web/web_rom_loader.c` — the browser ROM loading implementation:
  - Use Emscripten's `EM_ASM` / `EM_JS` macros to create a JavaScript file input element
  - When the user selects a file, read it via the FileReader API into a Uint8Array
  - Write the ROM data to Emscripten's virtual filesystem at the path expected by `rom_checker.cpp` (likely `/save/baserom.us.z64` or wherever `fs_get_write_path("")` resolves to for web)
  - Create a C function `int web_load_rom_from_picker(void)` that:
    - Creates an HTML file input element with `accept=".z64,.n64,.v64"`
    - Returns 1 if ROM was successfully loaded, 0 if user cancelled
    - The function must handle the asynchronous nature of file picking — use `emscripten_sleep()` in a loop or use `ASYNCIFY` to yield while waiting for the file
  - Also create `int web_check_rom_exists(void)` that checks if a ROM already exists in IDBFS persistent storage
  - Create the corresponding header `src/pc/web/web_rom_loader.h`
  > **Completed:** Created `web_rom_loader.h` (header with native stubs and Emscripten declarations) and `web_rom_loader.c` (implementation using EM_ASM for HTML5 File API, FileReader, FS.writeFile into VFS at `/save/baserom.us.z64`, with emscripten_sleep() polling for async bridging). Added `src/pc/web` to Makefile SRC_DIRS and `-sASYNCIFY` + `-sASYNCIFY_STACK_SIZE=65536` to Makefile.web linker flags. Guarded existing `web_thread.c` with `WEB_USE_STANDALONE_THREAD` to prevent duplicate symbols. Tests pass for both native and web-simulated builds.

- [ ] Set up Emscripten IDBFS for persistent storage:
  - Create `src/pc/web/web_storage.c` with functions:
    - `void web_storage_init(void)` — mount IDBFS at the save directory, call `EM_ASM({ FS.syncfs(true, function(err) { ... }); })` to load persisted data
    - `void web_storage_save(void)` — call `EM_ASM({ FS.syncfs(false, function(err) { ... }); })` to persist data to IndexedDB
  - Call `web_storage_init()` early in `main()` before ROM checking (after `fs_init()`)
  - Call `web_storage_save()` after the ROM is loaded and after any config/save file changes
  - The IDBFS mount point should match what `sys_user_path()` returns for web builds
  - Create the corresponding header `src/pc/web/web_storage.h`

- [ ] Integrate ROM loading into the game startup flow in `src/pc/pc_main.c`:
  - After `fs_init()` but before `main_rom_handler()`, add web-specific ROM loading logic:
    - First check if ROM already exists in persistent storage (IDBFS)
    - If not, show a file picker overlay and wait for the user to provide one
    - Once loaded, validate with existing `is_rom_valid()` / `main_rom_handler()` logic
  - The `render_rom_setup_screen()` function (used when no ROM is found) could be adapted for web, OR replaced with a simpler HTML-based UI overlay
  - For the initial MVP, a simple approach: use `EM_ASM` to show a JavaScript `alert()` or HTML overlay prompting for the ROM, then use the file picker
  - Guard all of this with `#ifdef TARGET_WEB`

- [ ] Handle the `extract_assets.py` and ROM asset pipeline for web:
  - The ROM asset extraction (`rom_assets_load()` at line 456 of pc_main.c) reads the ROM file and extracts textures, models, sounds, etc. at runtime
  - Verify this works with the ROM in Emscripten's virtual filesystem — it should, since `fopen`/`fread` are emulated by Emscripten
  - The `rom_assets_queue()` macro uses `__attribute__((constructor))` for auto-registration — verify this works with Emscripten (it should, as Emscripten supports constructor attributes)
  - Check if the `tools/` build step (which compiles native tools like `n64graphics`, `textconv`, etc.) needs adaptation — these tools run on the HOST during build time (not in WASM), so they should work normally since the Makefile builds them with the host compiler
  - Verify that the Python scripts (`copy_extended_sounds.py`, `mario_anims_converter.py`, `demo_data_converter.py`) run correctly during the build step

- [ ] Create a minimal HTML shell template `src/pc/web/shell.html` for the Emscripten output:
  - This replaces Emscripten's default shell template with a styled page
  - Include a centered canvas element with id "canvas" for the game rendering
  - Include a ROM upload button/area that is visible before the game starts
  - Include basic CSS: dark background, centered canvas, loading indicator
  - Add a "Loading..." progress bar that hooks into Emscripten's `Module.setStatus`
  - Add the `Module` object configuration: `{ canvas: document.getElementById('canvas'), ... }`
  - Include `<meta>` tags for mobile viewport and PWA basics
  - Add COOP/COEP headers hint in a comment (needed if using SharedArrayBuffer for threads):
    `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`
  - Reference this shell template in `Makefile.web` via `--shell-file src/pc/web/shell.html`
