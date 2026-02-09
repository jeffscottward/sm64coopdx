# Phase 05: Compilation Fixes and First Successful Build

This phase is the critical integration step where we attempt the first full Emscripten build and systematically fix every compilation error. Expect dozens of issues — missing headers, incompatible function calls, unsupported POSIX APIs, linker errors from native libraries, and 32-bit pointer size mismatches. This is the phase where the web port goes from "a plan" to "it compiles." Once it compiles, even if it doesn't run perfectly, the hardest part is done. The approach is iterative: build, fix errors, rebuild, repeat until clean.

## Tasks

- [x] Ensure Emscripten SDK is installed and run the first build attempt. Execute:
  - Verify `emcc --version` works (if not, install emsdk following https://emscripten.org/docs/getting_started/downloads.html)
  - Run `make -f Makefile.web -j$(nproc) 2>&1 | head -200` to capture the first wave of errors
  - Create a file `Auto Run Docs/Initiation/Working/build_errors_01.log` with the output
  - Categorize the errors into: missing headers, undefined symbols, incompatible types, linker errors
  - Fix the first batch of errors and document what was changed
  > **Completed 2026-02-09:** Installed Emscripten SDK 5.0.0 via emsdk. Required GNU Make 4.x+ (macOS ships 3.81 which doesn't support `!=` operator) — installed via `brew install make` (gmake 4.4.1). First build revealed 5 error categories: (1) build system issues (GNU Make suffix rule `tangle`, missing build directories), (2) missing `<float.h>` for `FLT_EPSILON`, (3) `EM_ASM` macro comma confusion in `audio_web.h`, (4) `LOADING_SCREEN_MUTEX` macro using `pthread_mutex_lock` directly instead of web-compatible abstractions, (5) `emcc -E -P` not recognizing `.h.in` extension. All fixed — 552 source files now compile cleanly. Only remaining issue is linker errors for Lua symbols (native `liblua53.a` incompatible with WASM, addressed by Task 4). Build errors log at `Auto Run Docs/Working/build_errors_01.log`.

- [x] Fix C/C++ standard library and POSIX compatibility issues:
  - `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>` — guard with `#ifndef TARGET_WEB` in all network source files
  - `<sys/mman.h>` (used by Mumble) — guard with `#ifndef TARGET_WEB`
  - `<dlfcn.h>` (`dlopen`, `dlsym`) — Emscripten has partial support. If used for dynamic loading (crash handler, loading screen), guard or stub
  - `<execinfo.h>` (`backtrace`) — not available in Emscripten. Guard the crash handler code
  - `<signal.h>` signal handlers — Emscripten has limited signal support. Guard `SIGSEGV`/`SIGABRT` handlers
  - `<pthread.h>` — should work if `-s USE_PTHREADS=1` is set, but verify. If single-threaded mode, stub it
  - `std::filesystem` in `rom_checker.cpp` — Emscripten supports it with `-s FORCE_FILESYSTEM=1`, but verify
  - `fork()`, `exec()`, `pipe()` — should not be used, but search and guard if found
  - Iterate: rebuild after each batch of fixes
  > **Completed 2026-02-09:** Comprehensive audit of all POSIX/std library compatibility. Most items were already guarded from prior work:
  > - **socket headers** (`socket_linux.h`): Already had `#ifdef TARGET_WEB` with minimal type stubs (prior commit)
  > - **sys/mman.h** (`mumble.c`/`mumble.h`): Already wrapped in `#ifndef TARGET_WEB` with no-op inline stubs (prior commit)
  > - **dlfcn.h, execinfo.h, signal.h** (`crash_handler.c`): Guarded by `#if (defined(_WIN32) || defined(__linux__))` — Emscripten defines `__unix__` but NOT `__linux__`, so crash handler is already excluded
  > - **std::filesystem** (`rom_checker.cpp`): Already guarded with `#ifndef TARGET_WEB` and web-specific C file I/O implementation (prior commit)
  > - **fork/exec/pipe**: Not used anywhere in src/
  > - **pthread.h** — NEW FIX: `smlua_audio_utils.c` had raw `#include <pthread.h>` and direct `pthread_mutex_lock/unlock` calls bypassing the thread.h abstraction. Added `#ifdef TARGET_WEB` guards with `SAMPLE_COPY_MUTEX_LOCK/UNLOCK` macros (no-ops on web, real mutex on native). Also removed `-s USE_PTHREADS=1 -pthread` and `PTHREAD_POOL_SIZE=4` from `Makefile.web` to match the single-threaded design (thread.c stubs). This avoids the SharedArrayBuffer/COOP/COEP server header requirement. miniaudio.h already has built-in `MA_EMSCRIPTEN` support with `NO_THREADING` mode. All 552+ source files still compile cleanly; only remaining issue is Lua linker errors (Task 4).

- [x] Fix linker errors from missing native libraries:
  - `-lcurl` — remove for web builds (update checker already stubbed)
  - `-ldl` — Emscripten has a stub, should link. If not, remove for web
  - `-lpthread` — should work with `-s USE_PTHREADS=1`. If single-threaded, remove
  - `-latomic` — Emscripten provides this, should link
  - `-lz` (zlib) — use `-s USE_ZLIB=1` Emscripten port instead of system zlib
  - `-rdynamic` — not supported by Emscripten, remove for web builds
  - `-no-pie` — not applicable for Emscripten, remove for web builds
  - `-march=native` — not applicable for WASM, remove for web builds
  - Lua library (`-l:liblua53.a`) — must compile Lua from source with Emscripten. Add a step to build `lib/lua/` with `emcc`. The Lua source is portable C and should compile cleanly
  - CoopNet and Discord libraries — already disabled via build flags, verify no stray references
  - Update `Makefile.web` with corrected linker flags
  > **Completed 2026-02-09:** All native linker flags (`-lcurl`, `-ldl`, `-lpthread`, `-latomic`, `-lz`, `-rdynamic`, `-no-pie`, `-march=native`) are **already excluded** — Makefile.web passes LDFLAGS on the command line which overrides the main Makefile's `:=` and `+=` assignments completely. Emscripten ports handle zlib (`-s USE_ZLIB=1`), SDL2 (`-s USE_SDL=2`), and OpenGL ES (`-s FULL_ES2=1`). For Lua: downloaded Lua 5.3.5 source code (35 files), placed in `lib/lua/src/`, compiled 33 library files (excluding lua.c/luac.c which contain main()) with `emcc -O2 -DLUA_USE_POSIX`, archived with `emar rcs` into `lib/lua/web/liblua53.a`. Added `lua-web` target to Makefile.web as a prerequisite of `web`. CoopNet (COOPNET=0) and Discord (DISCORD_SDK=0) confirmed disabled. **Build now completes successfully** — produces sm64coopdx.html (9K), sm64coopdx.js (452K), sm64coopdx.wasm (62MB), sm64coopdx.data (205K).

- [ ] Compile Lua 5.3 for Emscripten:
  - Read `lib/lua/include/` to understand the Lua version and header structure
  - The precompiled Lua libraries (`lib/lua/linux/liblua53.a`, etc.) are native — they won't work with WASM
  - Add a build step in `Makefile.web` that compiles Lua 5.3 from source using `emcc`:
    - Find or create the Lua source files (check if they're in `lib/lua/src/` or need downloading)
    - If Lua source isn't in the repo, download Lua 5.3.6 source and add to `lib/lua/src/`
    - Compile with `emcc -O2 -DLUA_USE_POSIX -I lib/lua/include -c` each Lua `.c` file
    - Create `lib/lua/web/liblua53.a` using `emar`
    - Link against this in the web build
  - Alternatively, use Emscripten's Lua port if available via `-s USE_LUA=1` (check availability)

- [ ] Fix remaining compilation errors iteratively:
  - Run the build again: `make -f Makefile.web -j$(nproc) 2>&1 | head -200`
  - Address each error category:
    - Type mismatches (32-bit vs 64-bit pointer sizes in WASM)
    - Missing function declarations (functions guarded out by TARGET_WEB but still referenced)
    - Implicit function declarations
    - Incompatible pointer types
  - Pay special attention to the DynOS C++ code (`data/dynos*.cpp`) — C++ compilation with `em++` may surface template or STL issues
  - After fixing errors, save the build log to `Auto Run Docs/Initiation/Working/build_errors_02.log`
  - Continue iterating until the build produces `.html`, `.js`, and `.wasm` output files

- [ ] Once compilation succeeds, do a basic smoke test:
  - Start a local web server: `python3 -m http.server 8080 --directory build/us_web/`
  - Note: If using pthreads, the server must send COOP/COEP headers. Use a script or `npx serve` with headers configured
  - Open `http://localhost:8080/sm64coopdx.html` in a browser
  - Check the browser console for errors
  - Document any runtime errors in `Auto Run Docs/Initiation/Working/runtime_errors_01.log`
  - The game likely won't fully work yet (ROM loading, rendering issues), but verify:
    - The WASM module loads
    - The canvas element appears
    - No immediate crashes
  - Create a research document at `docs/research/web-port-status.md` with front matter:
    - type: report, tags: [wasm, emscripten, web-port, status]
    - Document: what compiles, what doesn't, known issues, next steps
