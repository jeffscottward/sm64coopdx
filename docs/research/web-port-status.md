---
type: report
title: SM64CoopDX Web Port Status Report
created: 2026-02-09
tags:
  - wasm
  - emscripten
  - web-port
  - status
related:
  - "[[SM64COOPDX-WEB-05]]"
  - "[[emscripten-patterns]]"
---

# SM64CoopDX Web Port Status

## Build Status: COMPILES AND INITIALIZES

The SM64CoopDX web port successfully compiles to WebAssembly and initializes in the browser. The WASM module loads, executes, and reaches the ROM selection screen.

## What Compiles

- **552+ C/C++ source files** compile cleanly with emcc/em++ (Emscripten SDK 5.0.0)
- **Lua 5.3.5** compiled from source to WebAssembly via `lib/lua/web/liblua53.a`
- **DynOS C++ code** (data/dynos*.cpp) compiles with em++ without template/STL issues
- **SDL2** provided via Emscripten ports (`-s USE_SDL=2`)
- **OpenGL ES 2.0** via Emscripten (`-s FULL_ES2=1`)
- **zlib** via Emscripten ports (`-s USE_ZLIB=1`)
- **ASYNCIFY** enabled for async/sync bridging (ROM file picker, IDBFS)
- **IDBFS** linked for persistent storage (`-lidbfs.js`)

### Build Artifacts

| File | Size | Description |
|------|------|-------------|
| sm64coopdx.wasm | 62 MB | WebAssembly binary (game engine + Lua + all subsystems) |
| sm64coopdx.js | 452 KB | Emscripten JS glue code (runtime bridge) |
| sm64coopdx.html | 9 KB | Custom shell with ROM upload UI |
| sm64coopdx.data | 205 KB | Preloaded virtual filesystem (lang files, DynOS mods) |

## What Works at Runtime

Verified via headless Chromium smoke test (Playwright):

| Feature | Status | Notes |
|---------|--------|-------|
| HTML page serves | PASS | All 4 artifacts serve via HTTP |
| JS glue loads | PASS | Emscripten Module object created |
| WASM instantiation | PASS | 62MB module loads and calledRun=true |
| IDBFS init | PASS | IndexedDB persistent storage mounts |
| Config auto-creation | PASS | sm64config.txt created on first run |
| ROM upload UI | PASS | Overlay renders correctly with button |
| Canvas element | PASS | Present in DOM, hidden until game starts |

## What Doesn't Work (Known Issues)

### 1. WebGL Initialization (Untested in Real Browser)

**Severity**: Unknown (fails in headless, likely works in real browser)

The headless Chromium test lacks GPU support, so WebGL cannot initialize. The error `Cannot read properties of undefined (reading 'getContextSafariWebGL2Fixed')` is specific to the Emscripten SDL2 port's Safari workaround code path, which fails gracefully when no GL context is available. **Manual browser testing is needed.**

### 2. "Invalid or unexpected token" JavaScript Error

**Severity**: Medium

A JavaScript syntax error occurs during WASM initialization. This may be from:
- An `EM_ASM` block with a syntax issue in the generated JS
- An `emscripten_run_script()` call with malformed JavaScript
- A code path in the audio or graphics initialization

Needs source-map debugging to pinpoint the exact location.

### 3. IDBFS Double-Sync Warning

**Severity**: Low (cosmetic)

`"2 FS.syncfs operations in flight at once"` — Two concurrent IDBFS sync operations fire during initialization. This is benign but indicates `web_storage_init()` and another codepath both call `FS.syncfs()` before the first completes. Fix: add a completion guard.

### 4. ROM Loading Flow (Untested)

**Severity**: Unknown

The file picker UI is present and functional, but the full ROM loading flow hasn't been tested with an actual ROM file. The ASYNCIFY-based polling loop in `web_rom_loader.c` needs verification.

### 5. Audio Subsystem (Untested)

**Severity**: Unknown

miniaudio has built-in `MA_EMSCRIPTEN` support with Web Audio API backend and `NO_THREADING` mode. The SDL2 audio API is configured. No runtime audio testing has been performed.

### 6. Input Handling (Untested)

**Severity**: Unknown

SDL2 keyboard and gamepad input via Emscripten is configured but untested in a real browser.

## Disabled Features

These features are intentionally disabled for the web build:

| Feature | Reason |
|---------|--------|
| Discord SDK | Not applicable for web (`DISCORD_SDK=0`) |
| CoopNet | Requires native sockets (`COOPNET=0`) |
| Update checker | Requires libcurl (`NO_UPDATE_CHECKER`) |
| Threaded loading screen | Requires pthreads (`WEB_LOADING_SCREEN_DISABLED`) |
| Crash handler | Linux/Windows only (guarded by `#if defined(__linux__)`) |
| Mumble voice | Not available on web (guarded by `#ifndef TARGET_WEB`) |

## Architecture

### Threading Model

**Single-threaded** — No pthreads (`-s USE_PTHREADS` not used). This avoids the SharedArrayBuffer requirement and COOP/COEP server header complexity. Thread abstraction (`src/pc/thread.h`) provides no-op stubs for web. miniaudio uses `NO_THREADING` mode automatically.

### Filesystem

- **MEMFS** (in-memory): Default Emscripten filesystem, used for game data
- **IDBFS** (persistent): Mounted at `/save` for config, saves, and cached ROM
- **Preloaded data**: Language files and DynOS mods embedded in `.data` file

### ROM Loading

Browser File API picker → FileReader → Emscripten VFS (`FS.writeFile`) → IDBFS persistence. Uses ASYNCIFY polling bridge between async JS and synchronous C.

## Build System

```
Makefile.web (wrapper)
  → lua-web target: emcc → lib/lua/web/liblua53.a
  → web target: gmake -f Makefile (main) with overrides
    CC=emcc, CXX=em++, LD=em++, AR=emar
    LDFLAGS overridden to exclude native flags
    Emscripten ports for SDL2, zlib, GL ES2
```

## Next Steps

1. **Manual browser test** — Open in Chrome/Firefox with DevTools to verify WebGL rendering
2. **Debug JS syntax error** — Use `--source-map-base` and browser debugger to find the "invalid token" source
3. **Test ROM loading** — Provide a ROM file through the picker and verify the game progresses past the title screen
4. **Test audio** — Verify Web Audio API output in a real browser
5. **Optimize WASM size** — 62MB is very large; consider `-Oz`, `-g0`, and server-side compression
6. **Implement web-specific input** — Touch controls for mobile browsers
7. **Add service worker** — For offline play support and caching of the large WASM binary
