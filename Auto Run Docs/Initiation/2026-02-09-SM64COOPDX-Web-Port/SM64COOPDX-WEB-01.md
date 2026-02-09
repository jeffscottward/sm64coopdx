# Phase 01: Emscripten Build System Foundation

This phase establishes the core Emscripten/WASM build infrastructure for sm64coopdx. It creates a new `Makefile.web` that wraps the existing Makefile with Emscripten-specific compiler settings, disables native-only features (Discord SDK, CoopNet, update checker, threading), and configures the build to use `emcc`/`em++` with SDL2 and OpenGL ES via Emscripten ports. By the end of this phase, the project should compile (possibly with some errors to fix in later phases) using the Emscripten toolchain targeting WebAssembly. This is the critical foundation that everything else builds upon.

## Tasks

- [x] Research the existing Makefile and create `Makefile.web` — a wrapper Makefile that invokes the main Makefile with Emscripten overrides. The wrapper must:
  - Set `CC=emcc`, `CXX=em++`, `LD=em++`, `AR=emar`, `CPP=emcc -E -P`
  - Set `CROSS=` (empty, no cross-compilation prefix)
  - Set `DISCORD_SDK=0`, `COOPNET=0` to disable native-only libraries
  - Set `RENDER_API=GL`, `WINDOW_API=SDL2`, `AUDIO_API=SDL2`, `CONTROLLER_API=SDL2`
  - Define `TARGET_WEB=1` and add `-DTARGET_WEB=1` to `EXTRA_CFLAGS`
  - Define `USE_GLES=1` via `EXTRA_CFLAGS += -DUSE_GLES=1` (activates existing GLES code paths in gfx_opengl.c)
  - Add Emscripten port flags: `-s USE_SDL=2 -s USE_ZLIB=1 -s FULL_ES2=1`
  - Set `EXTRA_CFLAGS += -s USE_SDL=2 -s USE_ZLIB=1 -s USE_PTHREADS=1 -pthread`
  - Set `HEADLESS=0`
  - Override `OPT_FLAGS` to use `-O2 -g` for debugging
  - Override `BUILD_DIR` to `build/us_web`
  - Override `EXE` to `$(BUILD_DIR)/sm64coopdx.html`
  - Add linker flags: `-s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s TOTAL_MEMORY=256MB -s USE_SDL=2 -s FULL_ES2=1 -s USE_ZLIB=1 -s USE_PTHREADS=1 -pthread -s PTHREAD_POOL_SIZE=4 -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap'] -s FORCE_FILESYSTEM=1 --preload-file $(BUILD_DIR)/lang@/lang --preload-file $(BUILD_DIR)/dynos@/dynos -lidbfs.js`
  - The output format should be `.html` (Emscripten generates `.html`, `.js`, and `.wasm` files)
  - Include a `web` make target and a `clean-web` target
  - Place `Makefile.web` in the project root alongside the existing `Makefile`
  - Add a comment block at the top explaining usage: `make -f Makefile.web` and prerequisites (emsdk, baserom)

  > **Completed 2026-02-09:** Created `Makefile.web` in project root. The wrapper passes all Emscripten overrides to the main Makefile via command-line variables. Also includes `-Wno-format-security -Wno-trigraphs` in EXTRA_CFLAGS since command-line overrides prevent the main Makefile's `+=` from appending them. Dry-run verified successfully — all overrides propagate correctly. Note: LDFLAGS is fully overridden (not appended), so native libraries like `-lcurl`, `-lz`, and Lua are replaced by Emscripten equivalents (`-s USE_ZLIB=1`, etc.). Lua linking will need to be addressed in a later phase when the full build is attempted.

- [ ] Create `src/pc/web/web_compat.h` — a compatibility header for web-specific preprocessor guards and stubs. This file should:
  - Provide `#ifdef TARGET_WEB` guards
  - Stub out `curl` usage by defining `NO_UPDATE_CHECKER` when `TARGET_WEB` is defined
  - Provide `#include <emscripten.h>` and `#include <emscripten/html5.h>` when `__EMSCRIPTEN__` is defined
  - Define `LOADING_SCREEN_SUPPORTED` behavior for web (disable threaded loading screen since it needs special handling)
  - Stub out `sys_exe_path_dir()` and `sys_exe_path_file()` for web (return empty string or "/")
  - Create the directory `src/pc/web/` if it doesn't exist

- [ ] Modify the threading system to support Emscripten. Read `src/pc/thread.h` and `src/pc/thread.c` (find exact filename) and create a web-compatible version:
  - When `TARGET_WEB` is defined, the threading implementation should use Emscripten's pthread support (which requires SharedArrayBuffer)
  - Alternatively, provide a single-threaded fallback: make `init_thread_handle()` simply call the entry function directly and return, make mutex operations no-ops
  - The safest initial approach is single-threaded: stub out threading so audio runs inline on the main thread
  - Create `src/pc/web/web_thread.c` with the single-threaded stubs that get compiled instead of the normal thread.c when `TARGET_WEB=1`

- [ ] Modify `src/pc/update_checker.c` to be disabled under `TARGET_WEB`. Read the file and:
  - Wrap the curl-dependent code in `#ifndef TARGET_WEB` / `#endif` blocks
  - Make `check_for_updates()` a no-op when `TARGET_WEB` is defined
  - Make `show_update_popup()` a no-op when `TARGET_WEB` is defined
  - This avoids linking against libcurl which is unavailable in Emscripten

- [ ] Handle the `rom_checker.cpp` file for web builds. Read `src/pc/rom_checker.cpp` fully and:
  - The `<filesystem>` header and `std::filesystem` may not be fully supported in Emscripten
  - Wrap filesystem-dependent code paths with `#ifndef TARGET_WEB` guards
  - For web builds, provide a simplified ROM validation path that checks the ROM data from an in-memory buffer (the ROM will be loaded via browser file picker into Emscripten's virtual filesystem)
  - The `main_rom_handler()` function should still work — it just needs the ROM file to exist in the virtual filesystem at the expected path

- [ ] Update `src/pc/pc_main.c` for Emscripten main loop compatibility:
  - Add `#ifdef __EMSCRIPTEN__` / `#include <emscripten.h>` at the top
  - The main game loop at line 606 (`while (true) { ... }`) must be replaced with `emscripten_set_main_loop()` for web builds, since infinite loops block the browser
  - Create a static callback function `static void web_main_loop_iteration(void)` that contains the body of the while loop
  - In `main()`, after all initialization, call `emscripten_set_main_loop(web_main_loop_iteration, 0, 1)` instead of the while loop when `__EMSCRIPTEN__` is defined
  - The `0` argument means "use requestAnimationFrame timing" and `1` means "simulate infinite loop"
  - Also wrap the `#include <unistd.h>` in `#ifndef __EMSCRIPTEN__` if it causes issues (usually fine with Emscripten)
  - Ensure `game_exit()` calls `emscripten_cancel_main_loop()` before `exit()` under `__EMSCRIPTEN__`

- [ ] Handle platform-specific code in `src/pc/platform.c` (or wherever `sys_user_path` etc. are implemented). Read the file and:
  - For `TARGET_WEB`, `sys_user_path()` should return `"/save"` or `"/user"` — a path in Emscripten's virtual filesystem that will be persisted via IndexedDB (IDBFS)
  - `sys_exe_path_dir()` should return `"/"`
  - `sys_resource_path()` should return `"/"`
  - Add `#ifdef TARGET_WEB` blocks for these overrides
  - Also check `src/pc/fs/fs.c` for any POSIX-specific filesystem calls that need web alternatives

- [ ] Create a build verification script `build_web.sh` in the project root that:
  - Checks if `emsdk` is installed and activated (checks for `emcc` in PATH)
  - Checks if `baserom.us.z64` exists in the expected location
  - Checks if Python 3 is available
  - Runs `make -f Makefile.web clean-web` then `make -f Makefile.web -j$(nproc)`
  - Prints helpful error messages if prerequisites are missing
  - Make it executable with `chmod +x`
  - This script is for developer convenience and CI purposes
