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

- [x] Create `src/pc/web/web_compat.h` — a compatibility header for web-specific preprocessor guards and stubs. This file should:
  - Provide `#ifdef TARGET_WEB` guards
  - Stub out `curl` usage by defining `NO_UPDATE_CHECKER` when `TARGET_WEB` is defined
  - Provide `#include <emscripten.h>` and `#include <emscripten/html5.h>` when `__EMSCRIPTEN__` is defined
  - Define `LOADING_SCREEN_SUPPORTED` behavior for web (disable threaded loading screen since it needs special handling)
  - Stub out `sys_exe_path_dir()` and `sys_exe_path_file()` for web (return empty string or "/")
  - Create the directory `src/pc/web/` if it doesn't exist

  > **Completed 2026-02-09:** Created `src/pc/web/` directory and `src/pc/web/web_compat.h`. The header provides:
  > - `#ifdef TARGET_WEB` outer guard wrapping all web-specific definitions
  > - `NO_UPDATE_CHECKER` define to disable curl-dependent update checker
  > - `WEB_LOADING_SCREEN_DISABLED` flag for disabling the threaded loading screen (the loading screen uses pthreads with mutex locking in `loading.h` gated by `LOADING_SCREEN_SUPPORTED`; full threading support requires SharedArrayBuffer which needs special handling in later phases)
  > - Conditional `#include <emscripten.h>` and `#include <emscripten/html5.h>` under `__EMSCRIPTEN__`
  > - Path constant macros (`WEB_EXE_PATH_DIR="/"`、`WEB_EXE_PATH_FILE="/sm64coopdx"`, `WEB_USER_PATH="/save"`, `WEB_RESOURCE_PATH="/"`) for use when `platform.c` is modified in a later task
  > - Verified compiles cleanly as both C and C++ with and without `TARGET_WEB` defined. Note: `<emscripten.h>` includes are guarded by `__EMSCRIPTEN__` so native builds are unaffected.

- [x] Modify the threading system to support Emscripten. Read `src/pc/thread.h` and `src/pc/thread.c` (find exact filename) and create a web-compatible version:
  - When `TARGET_WEB` is defined, the threading implementation should use Emscripten's pthread support (which requires SharedArrayBuffer)
  - Alternatively, provide a single-threaded fallback: make `init_thread_handle()` simply call the entry function directly and return, make mutex operations no-ops
  - The safest initial approach is single-threaded: stub out threading so audio runs inline on the main thread
  - Create `src/pc/web/web_thread.c` with the single-threaded stubs that get compiled instead of the normal thread.c when `TARGET_WEB=1`

  > **Completed 2026-02-09:** Implemented the single-threaded fallback approach (safest initial strategy). Three files modified/created:
  >
  > **`src/pc/thread.h`** — Added `#ifdef TARGET_WEB` conditional compilation:
  > - Web builds use a simplified `ThreadHandle` struct with just `int dummy` + `enum ThreadState state` (no pthread members)
  > - `MUTEX_LOCK()` and `MUTEX_UNLOCK()` macros become `((void)0)` no-ops for web (eliminates all mutex overhead in audio/loading code)
  > - `#include <pthread.h>` is only included for native builds
  > - Function declarations remain shared across both paths (same API surface)
  >
  > **`src/pc/thread.c`** — Added `#ifdef TARGET_WEB` / `#else` blocks:
  > - Web path: `init_thread_handle()` and `init_thread()` call the entry function **directly on the main thread** (synchronous execution), then immediately set state to STOPPED
  > - All mutex functions (`init_mutex`, `destroy_mutex`, `lock_mutex`, `trylock_mutex`, `unlock_mutex`) return 0 as no-ops
  > - `join_thread()`, `detach_thread()`, `stop_thread()` simply mark state as STOPPED
  > - Native path: original pthread implementation preserved unchanged
  >
  > **`src/pc/web/web_thread.c`** — Standalone alternative file with identical single-threaded stubs. Can be used in place of `thread.c` if the build system is configured to exclude `thread.c` for web builds and include `src/pc/web/` in SRC_DIRS. Includes `"pc/thread.h"` for standalone compilation.
  >
  > Threading usage in the codebase (all handled by this change):
  > - `gAudioThread` (src/audio/data.c) — MUTEX_LOCK/UNLOCK used extensively in audio code → now no-ops
  > - `gLoadingThread` (src/pc/loading.c) — background loading → will run synchronously on main thread
  > - `gModRefreshThread` (src/pc/djui/djui_panel_host_mods.c) — mod refresh → will run synchronously
  >
  > Verified: Both native (without TARGET_WEB) and web (with TARGET_WEB=1) paths compile cleanly with gcc. Macro behavior confirmed correct in both modes.

- [x] Modify `src/pc/update_checker.c` to be disabled under `TARGET_WEB`. Read the file and:
  - Wrap the curl-dependent code in `#ifndef TARGET_WEB` / `#endif` blocks
  - Make `check_for_updates()` a no-op when `TARGET_WEB` is defined
  - Make `show_update_popup()` a no-op when `TARGET_WEB` is defined
  - This avoids linking against libcurl which is unavailable in Emscripten

  > **Completed 2026-02-09:** Wrapped the entire file in `#ifdef TARGET_WEB` / `#else` / `#endif` guards. Structure:
  > - **Web path (`TARGET_WEB` defined):** Only includes `update_checker.h`. Provides `gUpdateMessage = false` (always), and empty no-op implementations of `show_update_popup()` and `check_for_updates()`. No curl, WinINet, djui, or loading headers are included — zero native dependencies.
  > - **Native path (`TARGET_WEB` not defined):** Original implementation preserved unchanged with all curl/WinINet code, version parsing, and update popup logic.
  > - The `#include "update_checker.h"` is placed before the `#ifdef` so `stdbool.h` (for the `bool` type) is available in both paths.
  > - Callers (`pc_main.c`, `djui_panel_host.c`, `djui_panel_main.c`, `djui_panel_join.c`) need no changes — they check `gUpdateMessage` which is always `false` for web, and call functions that are valid no-ops.
  > - Verified: Compiles cleanly with `gcc -DTARGET_WEB -I src -I src/pc`.

- [x] Handle the `rom_checker.cpp` file for web builds. Read `src/pc/rom_checker.cpp` fully and:
  - The `<filesystem>` header and `std::filesystem` may not be fully supported in Emscripten
  - Wrap filesystem-dependent code paths with `#ifndef TARGET_WEB` guards
  - For web builds, provide a simplified ROM validation path that checks the ROM data from an in-memory buffer (the ROM will be loaded via browser file picker into Emscripten's virtual filesystem)
  - The `main_rom_handler()` function should still work — it just needs the ROM file to exist in the virtual filesystem at the expected path

  > **Completed 2026-02-09:** Wrapped all `std::filesystem`-dependent code in `#ifndef TARGET_WEB` / `#endif` guards. Structure:
  > - **`<filesystem>` include and `namespace fs` alias**: Only included for native builds. Web builds use `<cstdio>` and `<cstring>` instead.
  > - **Web `is_rom_valid()`**: Same MD5 validation logic (via `mod_cache_md5`) but replaces `std::filesystem::exists()` with a `web_file_exists()` helper using `fopen`, and replaces `std::filesystem::copy_file()` with C file I/O (`fopen`/`fread`/`fwrite`). ROM validation works identically — only the file operations differ.
  > - **Web `scan_path_for_rom()`**: Instead of `std::filesystem::directory_iterator` (unreliable on Emscripten VFS), directly checks expected ROM filenames from the `sVanillaMD5` lookup table (e.g., `baserom.us.z64`). This is sufficient since the ROM naming convention is fixed.
  > - **`legacy_folder_handler()`**: No-op for web builds (no legacy `tmp` → `.tmp` folder migration needed in virtual filesystem).
  > - **`main_rom_handler()`**: Shared across both paths — calls `scan_path_for_rom()` and `sys_exe_path_dir()` which work correctly for both web and native.
  > - **`rom_on_drop_file()`**: Unchanged — uses `is_rom_valid()` which works correctly in both modes (guarded by `LOADING_SCREEN_SUPPORTED`).
  > - Native path: original `std::filesystem` implementation preserved unchanged.
  > - Verified: Both web (with `TARGET_WEB=1`) and native (without) code paths compile cleanly with `g++ -std=c++17`.

- [x] Update `src/pc/pc_main.c` for Emscripten main loop compatibility:
  - Add `#ifdef __EMSCRIPTEN__` / `#include <emscripten.h>` at the top
  - The main game loop at line 606 (`while (true) { ... }`) must be replaced with `emscripten_set_main_loop()` for web builds, since infinite loops block the browser
  - Create a static callback function `static void web_main_loop_iteration(void)` that contains the body of the while loop
  - In `main()`, after all initialization, call `emscripten_set_main_loop(web_main_loop_iteration, 0, 1)` instead of the while loop when `__EMSCRIPTEN__` is defined
  - The `0` argument means "use requestAnimationFrame timing" and `1` means "simulate infinite loop"
  - Also wrap the `#include <unistd.h>` in `#ifndef __EMSCRIPTEN__` if it causes issues (usually fine with Emscripten)
  - Ensure `game_exit()` calls `emscripten_cancel_main_loop()` before `exit()` under `__EMSCRIPTEN__`

  > **Completed 2026-02-09:** Modified `src/pc/pc_main.c` with three changes for Emscripten main loop compatibility:
  >
  > **1. Added Emscripten include** (line 8-10): `#ifdef __EMSCRIPTEN__` / `#include <emscripten.h>` / `#endif` at the top of the file, after the standard library includes. The `<unistd.h>` include was left as-is since Emscripten provides a compatible `<unistd.h>` header.
  >
  > **2. Created `web_main_loop_iteration()` callback** (before `main()`): A `static void` function wrapped in `#ifdef __EMSCRIPTEN__` containing the exact body of the original `while (true)` loop — `debug_context_reset()`, `CTX_BEGIN/END`, `WAPI.main_loop(produce_one_frame)`, `discord_update()` (under `DISCORD_SDK`), `mumble_update()`, debug flushes (under `DEBUG`), `djui_ctx_display_update()` (under `DEVELOPMENT`), and `djui_lua_profiler_update()`. This function is called once per browser frame by Emscripten's requestAnimationFrame scheduler.
  >
  > **3. Replaced main loop** (in `main()`): The `while (true)` loop is now wrapped in `#ifdef __EMSCRIPTEN__` / `#else`. For web builds, `emscripten_set_main_loop(web_main_loop_iteration, 0, 1)` is called instead — the `0` fps argument lets the browser use requestAnimationFrame timing, and `1` simulates an infinite loop (Emscripten unwinds the call stack so the browser event loop can run).
  >
  > **4. Updated `game_exit()`**: Added `emscripten_cancel_main_loop()` call before `exit(0)` under `#ifdef __EMSCRIPTEN__`, ensuring the main loop is properly cancelled before cleanup.
  >
  > Verified: Both Emscripten and native code paths compile cleanly (tested with gcc syntax checks). The native path is completely unchanged — all Emscripten code is gated behind `__EMSCRIPTEN__` preprocessor guards.

- [x] Handle platform-specific code in `src/pc/platform.c` (or wherever `sys_user_path` etc. are implemented). Read the file and:
  - For `TARGET_WEB`, `sys_user_path()` should return `"/save"` or `"/user"` — a path in Emscripten's virtual filesystem that will be persisted via IndexedDB (IDBFS)
  - `sys_exe_path_dir()` should return `"/"`
  - `sys_resource_path()` should return `"/"`
  - Add `#ifdef TARGET_WEB` blocks for these overrides
  - Also check `src/pc/fs/fs.c` for any POSIX-specific filesystem calls that need web alternatives

  > **Completed 2026-02-09:** Added `#ifdef TARGET_WEB` platform block to `src/pc/platform.c` and a web-specific directory creation guard to `src/pc/fs/fs.c`.
  >
  > **`src/pc/platform.c`** — Two changes:
  > - **Include guard** (lines 8-9): Added `#ifdef TARGET_WEB` / `#include "web/web_compat.h"` as the first branch of the platform include chain, before `_WIN32`. This brings in the `WEB_*` path constants and Emscripten headers (when `__EMSCRIPTEN__` is also defined).
  > - **Platform function block** (lines 92-123): Added full `#ifdef TARGET_WEB` block before `#elif defined(_WIN32)` containing:
  >   - `sys_user_path()` → returns `WEB_USER_PATH` (`"/save"`) — will be backed by IDBFS for persistence
  >   - `sys_resource_path()` → returns `WEB_RESOURCE_PATH` (`"/"`) — root of Emscripten VFS
  >   - `sys_exe_path_dir()` → returns `WEB_EXE_PATH_DIR` (`"/"`)
  >   - `sys_exe_path_file()` → returns `WEB_EXE_PATH_FILE` (`"/sm64coopdx"`)
  >   - `sys_fatal_impl()` → logs via `emscripten_log()` (under `__EMSCRIPTEN__` guard), then `fprintf(stderr)` + `exit(1)`
  > - Native platform implementations (Windows, SDL2, fallback) are completely unchanged — all web code is behind `#ifdef TARGET_WEB`.
  >
  > **`src/pc/fs/fs.c`** — One change:
  > - **`fs_init()` web directory creation** (lines 55-61): Added `#ifdef TARGET_WEB` guard that calls `fs_sys_mkdir(fs_writepath)` if the directory doesn't exist. This is necessary because unlike native builds (where `sys_user_path()` returns an OS-managed directory that already exists), the web path `/save` won't exist in Emscripten's MEMFS until explicitly created.
  > - All other POSIX filesystem operations in `fs.c` (`stat`, `opendir`/`readdir`/`closedir`, `mkdir`, `rmdir`) are fully supported by Emscripten's virtual filesystem — no changes needed.
  >
  > Verified: Isolated compilation tests pass — web path functions return correct values (`"/save"`, `"/"`, `"/sm64coopdx"`), and the `fs_init` web mkdir guard correctly creates missing directories.

- [ ] Create a build verification script `build_web.sh` in the project root that:
  - Checks if `emsdk` is installed and activated (checks for `emcc` in PATH)
  - Checks if `baserom.us.z64` exists in the expected location
  - Checks if Python 3 is available
  - Runs `make -f Makefile.web clean-web` then `make -f Makefile.web -j$(nproc)`
  - Prints helpful error messages if prerequisites are missing
  - Make it executable with `chmod +x`
  - This script is for developer convenience and CI purposes
