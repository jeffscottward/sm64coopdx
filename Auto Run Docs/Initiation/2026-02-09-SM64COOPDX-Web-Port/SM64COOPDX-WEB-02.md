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

- [ ] Adapt `src/pc/gfx/gfx_sdl2.c` for Emscripten's SDL2 implementation:
  - Read the complete file and identify web-incompatible code
  - Emscripten's SDL2 port handles the HTML5 canvas automatically — `SDL_CreateWindow` creates a canvas element
  - The `SDL_GL_SetAttribute` calls for OpenGL context need adjustment: use `SDL_GL_CONTEXT_PROFILE_ES` and ES version 2.0 or 3.0 under `__EMSCRIPTEN__`
  - The `main_loop` function (which calls `run_one_game_iter`) is handled differently — in Emscripten, the main loop callback is set via `emscripten_set_main_loop`, so the SDL2 `main_loop` implementation should simply call the callback once and return
  - Fullscreen handling: `SDL_WINDOW_FULLSCREEN_DESKTOP` works differently in browsers — use Emscripten's `emscripten_request_fullscreen_strategy` or HTML5 Fullscreen API
  - `SDL_GetCurrentDisplayMode` may return 0 for refresh rate in browsers — provide a fallback of 60Hz
  - `SDL_GL_SetSwapInterval` (vsync) is meaningless in browsers — requestAnimationFrame already provides vsync. Make it a no-op under `__EMSCRIPTEN__`
  - Window resize: browser canvas resize should be handled via Emscripten's `emscripten_set_canvas_element_size` or let SDL2 handle it automatically
  - Remove or guard any `unistd.h` calls (`sleep`, `usleep`) — use `emscripten_sleep` if needed, or SDL_Delay

- [ ] Fix `src/pc/gfx/gfx_pc.c` for Emscripten compatibility. Read the file (it's ~83KB, focus on key areas):
  - Search for any direct OpenGL calls (there shouldn't be many since it goes through the rendering API abstraction)
  - Check for any `#include` that pulls in platform-specific GL headers
  - Verify the `gfx_init`, `gfx_start_frame`, `gfx_run`, `gfx_end_frame` functions work with the adapted backends
  - Check for any memory-mapped IO or pointer arithmetic that might behave differently in WASM (32-bit pointers, alignment)
  - The `MAX_CACHED_TEXTURES` (4096) and texture hashmap should work fine in WASM but verify memory usage is reasonable

- [ ] Create `src/pc/gfx/gfx_web_util.h` with helper macros and functions for web rendering:
  - Provide canvas resize utility that syncs the HTML5 canvas size with the CSS display size (prevents blurry rendering)
  - Provide a function to get the device pixel ratio for HiDPI/Retina displays: `EM_ASM_DOUBLE({ return window.devicePixelRatio || 1.0; })`
  - Provide WebGL context loss handling: register a callback for `webglcontextlost` and `webglcontextrestored` events
  - This file should only be included when `__EMSCRIPTEN__` is defined
  - Keep it minimal — only add what's actually needed for compilation to succeed
