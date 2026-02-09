# Phase 02: Graphics Backend Adaptation for WebGL

This phase adapts the OpenGL rendering backend to work with WebGL 2.0 via Emscripten. The existing codebase already has a `USE_GLES` code path in `gfx_opengl.c` that generates GLSL ES 100 shaders — this is the foundation for WebGL support. However, WebGL has additional restrictions beyond standard OpenGL ES (no `glMapBuffer`, different extension handling, stricter shader validation). This phase ensures all rendering code compiles and functions correctly under Emscripten's GL emulation, and adapts the SDL2 window manager for browser canvas rendering.

## Tasks

- [x] Audit and fix `src/pc/gfx/gfx_opengl.c` for WebGL compatibility. Read the complete file and identify issues:
  - WebGL 2.0 maps to OpenGL ES 3.0, but the current `USE_GLES` path targets ES 2.0 (GLSL `#version 100`). This should work with WebGL 1.0 — verify or upgrade to `#version 300 es` for WebGL 2.0
  - Check for `glMapBuffer` / `glMapBufferRange` usage — WebGL does not support `glMapBuffer`. Replace with `glBufferSubData` or `glBufferData` calls
  - Check for `GL_QUADS` usage — WebGL only supports `GL_TRIANGLES`, `GL_TRIANGLE_STRIP`, `GL_TRIANGLE_FAN`, `GL_LINES`, `GL_LINE_STRIP`, `GL_LINE_LOOP`, `GL_POINTS`. Replace `GL_QUADS` with triangulated equivalents
  - Check for `glGenVertexArrays` / `glBindVertexArray` — these require the `OES_vertex_array_object` extension in WebGL 1.0 or are built-in for WebGL 2.0. Wrap with `#ifdef __EMSCRIPTEN__` if needed
  - Ensure all `#include <GL/glew.h>` paths are guarded — Emscripten provides its own GL headers, GLEW must not be included
  - Add `#ifdef __EMSCRIPTEN__` blocks to include `<GLES2/gl2.h>` or `<GLES3/gl3.h>` instead of GLEW/SDL_opengl headers
  - Check the `gfx_opengl_api` struct at the bottom to ensure all function pointers are correctly assigned

  > **Completed** — Full audit of gfx_opengl.c (779 lines). Changes made:
  > 1. **GL headers (lines 11-45)**: Added `#ifdef __EMSCRIPTEN__` block that includes `<SDL2/SDL.h>`, `<GLES2/gl2.h>`, `<GLES2/gl2ext.h>` and sets `FOR_WINDOWS 0`. Entire native header section wrapped in `#else`.
  > 2. **Shader version**: `#version 100` (GLSL ES 1.0) is CORRECT for WebGL 1.0 with `FULL_ES2=1`. No change needed.
  > 3. **glMapBuffer**: NOT USED anywhere. Uses `glBufferData(GL_STREAM_DRAW)` for VBO upload. WebGL-safe.
  > 4. **GL_QUADS**: NOT USED. Uses `GL_TRIANGLES` exclusively in `gfx_opengl_draw_triangles()`. WebGL-safe.
  > 5. **glGenVertexArrays/glBindVertexArray (lines 712-725)**: Wrapped in `#ifndef __EMSCRIPTEN__` — VAOs not available in WebGL 1.0 ES2 without extension.
  > 6. **GLEW**: Already behind `FOR_WINDOWS || OSX_BUILD` guard, and `__EMSCRIPTEN__` block sets `FOR_WINDOWS 0`. Double-safe.
  > 7. **GL version check (lines 712-725)**: Wrapped in `#ifndef __EMSCRIPTEN__` — WebGL context is guaranteed by browser.
  > 8. **gfx_opengl_api struct**: All 22 function pointers verified correct, unchanged.

- [x] Adapt `src/pc/gfx/gfx_sdl2.c` for Emscripten's SDL2 implementation:
  - Read the complete file and identify web-incompatible code
  - Emscripten's SDL2 port handles the HTML5 canvas automatically — `SDL_CreateWindow` creates a canvas element
  - The `SDL_GL_SetAttribute` calls for OpenGL context need adjustment: use `SDL_GL_CONTEXT_PROFILE_ES` and ES version 2.0 or 3.0 under `__EMSCRIPTEN__`
  - The `main_loop` function (which calls `run_one_game_iter`) is handled differently — in Emscripten, the main loop callback is set via `emscripten_set_main_loop`, so the SDL2 `main_loop` implementation should simply call the callback once and return
  - Fullscreen handling: `SDL_WINDOW_FULLSCREEN_DESKTOP` works differently in browsers — use Emscripten's `emscripten_request_fullscreen_strategy` or HTML5 Fullscreen API
  - `SDL_GetCurrentDisplayMode` may return 0 for refresh rate in browsers — provide a fallback of 60Hz
  - `SDL_GL_SetSwapInterval` (vsync) is meaningless in browsers — requestAnimationFrame already provides vsync. Make it a no-op under `__EMSCRIPTEN__`
  - Window resize: browser canvas resize should be handled via Emscripten's `emscripten_set_canvas_element_size` or let SDL2 handle it automatically
  - Remove or guard any `unistd.h` calls (`sleep`, `usleep`) — use `emscripten_sleep` if needed, or SDL_Delay

  > **Completed** — Full audit and adaptation of gfx_sdl2.c (364 lines original → 449 lines). Changes made:
  > 1. **GL headers (lines 3-37)**: Added `#ifdef __EMSCRIPTEN__` block including `<SDL2/SDL.h>`, `<GLES2/gl2.h>`, `<GLES2/gl2ext.h>`, `<emscripten.h>`, `<emscripten/html5.h>`. Entire native header section wrapped in `#else`.
  > 2. **`unistd.h` (line 40-42)**: Guarded with `#ifndef __EMSCRIPTEN__` — not needed for web builds.
  > 3. **vsync (lines 81-89)**: `gfx_sdl_set_vsync()` made a no-op under `__EMSCRIPTEN__` — `requestAnimationFrame` already provides vsync in browsers.
  > 4. **Fullscreen (lines 91-111)**: `gfx_sdl_set_fullscreen()` body wrapped in `#ifndef __EMSCRIPTEN__` — `SDL_WINDOW_FULLSCREEN_DESKTOP` doesn't map to browser fullscreen; HTML5 Fullscreen API needed instead.
  > 5. **Window reset (lines 113-146)**: `gfx_sdl_reset_dimension_and_pos()` simplified for Emscripten — window position is meaningless in browsers, only reset width/height defaults.
  > 6. **SDL init (lines 148-219)**: DPI awareness call guarded, X11 compositor hint skipped, MSAA SDL attributes skipped (WebGL controls MSAA via canvas attributes), explicit ES 2.0 profile set via `#ifdef __EMSCRIPTEN__`, window creation simplified (no position), fullscreen setup skipped.
  > 7. **Alt+Enter toggle (lines 235-241)**: Guarded with `#ifndef __EMSCRIPTEN__` — browser fullscreen requires user gesture via HTML5 API.
  > 8. **File drop (lines 255-276)**: `gfx_sdl_ondropfile()` function and `SDL_DROPFILE` event case both wrapped in `#ifndef __EMSCRIPTEN__` — desktop file drop not applicable in browsers.
  > 9. **Window events (lines 298-321)**: Simplified for Emscripten — only track `SDL_WINDOWEVENT_SIZE_CHANGED` (canvas resize), skip window move and fullscreen state tracking.
  > 10. **MSAA query (lines 374-385)**: `gfx_sdl_get_max_msaa()` returns 0 on Emscripten — `GL_MAX_SAMPLES` not available in WebGL 1.0 (ES 2.0).
  > 11. **main_loop**: Already correct — `gfx_sdl_main_loop()` simply calls `run_one_game_iter()` once and returns. The Emscripten main loop callback is set in `pc_main.c` via `emscripten_set_main_loop()` (Phase 01 work).
  > 12. **SDL_GetCurrentDisplayMode**: NOT USED in gfx_sdl2.c. No refresh rate fallback needed.
  > 13. **Remaining SDL functions**: `SDL_GL_SwapWindow`, `SDL_Delay`, `SDL_GetWindowSize`, `SDL_SetWindowTitle`, clipboard, cursor, shutdown — all work correctly with Emscripten's SDL2 port. No changes needed.
  > 14. **gfx_sdl struct**: All 21 function pointers verified unchanged and correctly assigned.
  > 15. **Validation**: Preprocessor conditional balance verified (19 `__EMSCRIPTEN__` references, depth 0 final). Native code paths structurally unchanged.

- [x] Fix `src/pc/gfx/gfx_pc.c` for Emscripten compatibility. Read the file (it's ~83KB, focus on key areas):
  - Search for any direct OpenGL calls (there shouldn't be many since it goes through the rendering API abstraction)
  - Check for any `#include` that pulls in platform-specific GL headers
  - Verify the `gfx_init`, `gfx_start_frame`, `gfx_run`, `gfx_end_frame` functions work with the adapted backends
  - Check for any memory-mapped IO or pointer arithmetic that might behave differently in WASM (32-bit pointers, alignment)
  - The `MAX_CACHED_TEXTURES` (4096) and texture hashmap should work fine in WASM but verify memory usage is reasonable

  > **Completed** — Full audit of gfx_pc.c (2261 lines). **No code changes needed** — file is already Emscripten-compatible. Findings:
  > 1. **Direct OpenGL calls**: NONE. Only a comment on line 1674 references `glClear`. All rendering goes through `gfx_rapi` function pointer abstraction.
  > 2. **GL headers**: NONE included. All includes are standard C (`math.h`, `stdint.h`, `stdlib.h`, `stdio.h`, `string.h`, `stdbool.h`), `stb_image.h`, `PR/gbi.h`, and project-local headers (`gfx_cc.h`, `gfx_pc.h`, `gfx_rendering_api.h`, etc.).
  > 3. **`gfx_init` (line 1981)**: Calls `gfx_wapi->init()` and `gfx_rapi->init()` through function pointers — these are the already-adapted SDL2 and OpenGL backends from earlier tasks.
  > 4. **`gfx_start_frame` (line 1996)**: Calls `gfx_wapi->handle_events()` and `gfx_wapi->get_dimensions()` — both adapted in gfx_sdl2.c. No direct platform code.
  > 5. **`gfx_run` (line 2017)**: Calls `gfx_wapi->start_frame()` and `gfx_rapi->start_frame()` then processes the display list. Purely abstract.
  > 6. **`gfx_end_frame` (line 2048)**: Calls `gfx_rapi->end_frame()`, `gfx_rapi->finish_render()`, `gfx_wapi->swap_buffers_begin/end()`. All abstract.
  > 7. **SSE intrinsics (lines 8-9, 748-775)**: Properly guarded with `#ifdef __SSE__`. Emscripten does not define `__SSE__` by default, so the scalar fallback code (lines 770-774) is used automatically. No changes needed.
  > 8. **`OPTIMIZE_O3` macro**: Defined in `macros.h` as empty for clang (Emscripten's compiler). Functions marked with it compile normally.
  > 9. **`ALIGNED16` on Mat4 matrices (lines 57-59)**: WASM supports aligned memory allocation. No issues.
  > 10. **`seg_addr` (line 1719-1721)**: Simple `uintptr_t` → `void*` cast. In WASM32, both are 32-bit. No pointer size issues.
  > 11. **TextureCache memory (~130KB)**: `hashmap[8192]` pointers = ~32KB (4-byte WASM ptrs), `pool[4096]` nodes × ~24 bytes = ~98KB. Very reasonable for browser WASM.
  > 12. **`buf_vbo` static array (line 111)**: 256 × 26 × 3 = 19,968 floats × 4 bytes = ~78KB. Reasonable.
  > 13. **No `malloc`/`free`/`mmap`/`sleep`/`usleep`**: All data is statically allocated or stack-local. No platform-specific system calls.
  > 14. **No `#ifdef __EMSCRIPTEN__` blocks needed**: The file is entirely platform-agnostic through its use of the rendering API abstraction layer.

- [ ] Create `src/pc/gfx/gfx_web_util.h` with helper macros and functions for web rendering:
  - Provide canvas resize utility that syncs the HTML5 canvas size with the CSS display size (prevents blurry rendering)
  - Provide a function to get the device pixel ratio for HiDPI/Retina displays: `EM_ASM_DOUBLE({ return window.devicePixelRatio || 1.0; })`
  - Provide WebGL context loss handling: register a callback for `webglcontextlost` and `webglcontextrestored` events
  - This file should only be included when `__EMSCRIPTEN__` is defined
  - Keep it minimal — only add what's actually needed for compilation to succeed
