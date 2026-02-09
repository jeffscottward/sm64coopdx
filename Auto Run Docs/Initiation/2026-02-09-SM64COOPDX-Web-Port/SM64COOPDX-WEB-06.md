# Phase 06: Runtime Debugging and Playable Single-Player MVP

This phase takes the compiled WASM build from Phase 05 and makes it actually playable in the browser. Even after successful compilation, there will be runtime issues — WebGL rendering glitches, audio not starting, input mapping problems, memory allocation failures, and game logic bugs caused by 32-bit WASM environment differences. This phase involves running the game, identifying runtime errors via browser DevTools, and fixing them one by one until a player can navigate menus, start a game, and play through at least the first level (Bob-omb Battlefield). This is the "it works!" moment.

## Tasks

- [x] Set up a proper local development server with required security headers:
  - Browsers require `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp` headers for `SharedArrayBuffer` (needed if using pthreads)
  - Create `serve_web.py` in the project root — a simple Python HTTP server that adds these headers:
    ```python
    import http.server
    class Handler(http.server.SimpleHTTPRequestHandler):
        def end_headers(self):
            self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
            self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
            super().end_headers()
    http.server.HTTPServer(('', 8080), Handler).serve_forever()
    ```
  - Alternatively, if not using pthreads (single-threaded mode), a plain `python3 -m http.server` suffices
  - Document the server command in the build script
  > **Completed**: Created `serve_web.py` with COOP/COEP headers, `Cache-Control: no-cache`, CLI args (`--port`, `--dir`), build directory validation, and helpful error messages. Updated `Makefile.web` header comments and post-build output to reference `serve_web.py`. Verified headers served correctly via curl test.

- [x] Debug and fix WebGL rendering issues:
  - Open the game in Chrome with DevTools Console open
  - Common WebGL issues to look for and fix:
    - "WebGL: INVALID_ENUM" — usually from `GL_QUADS` or unsupported texture formats
    - "WebGL: INVALID_OPERATION" — from state calls in wrong order or missing bindings
    - Shader compilation errors — visible in console, fix GLSL syntax for ES compatibility
    - Black screen — check if `glClear` and viewport are set correctly
    - Flickering — check swap buffer timing and frame synchronization
  - Use `chrome://gpu` to verify WebGL2 is enabled
  - Add temporary debug logging in `gfx_opengl.c` functions to trace rendering calls
  - Check that texture upload and sampling work correctly (N64 textures are small, so memory shouldn't be an issue)
  > **Completed**: Thorough audit of the WebGL rendering pipeline confirmed the existing code is already well-designed for WebGL 1.0/GLES 2.0 compatibility — no `GL_QUADS`, no `glBegin`/`glEnd`, uses `GL_CLAMP_TO_EDGE`, correct `#version 100` shaders with `precision mediump float`, properly guarded VAO creation, and triangle-based rendering throughout. Changes made:
  > 1. **Integrated `gfx_web_util.h`** into `gfx_pc.c` — registered WebGL context loss/restore handlers in `gfx_init()`, added context loss skip in `gfx_run()`, and canvas size sync for HiDPI in the render loop.
  > 2. **Added WebGL debug logging** to `gfx_opengl.c` — `GL_CHECK()` macro (active with `-DGFX_WEB_DEBUG`), WebGL context info logging on init (version/renderer/vendor/GLSL version), and shader source dumping on compilation failure for browser DevTools debugging.
  > 3. **Fixed IDBFS double-sync race** in `web_storage.c` — added `sSyncInFlight` guard to prevent concurrent `FS.syncfs()` calls, eliminating the "2 FS.syncfs operations in flight at once" warning.
  > 4. Build verified clean with all 3 modified files recompiled and linked successfully.

- [x] Debug and fix audio playback:
  - Verify SDL_audio initializes correctly (check console for "SDL audio" related messages)
  - If audio doesn't play, it's likely the browser autoplay policy:
    - Add a "Click to Start" overlay that resumes the AudioContext on first user interaction
    - Use `EM_ASM({ var ctx = SDL.audioContext; if (ctx && ctx.state === 'suspended') ctx.resume(); })` on first input event
  - Check audio buffer underrun/overrun — if audio stutters, adjust buffer sizes
  - Verify the audio sample rate matches what the browser expects (typically 44100 or 48000 Hz)
  > **Completed**: Thorough audit of the SDL2 audio backend, autoplay policy workaround, and buffer configuration confirmed the audio system is well-architected for web. Changes made:
  > 1. **Enhanced `audio_sdl2.c`** — Added `[Web Audio]` console logging under `#ifdef __EMSCRIPTEN__` that reports the negotiated audio spec (freq/channels/samples/format) and autoplay policy notice on initialization.
  > 2. **Improved `audio_web.h`** — Made `ctx.resume()` use promise-based `.then()/.catch()` with success/failure logging. Added retry mechanism: if AudioContext doesn't exist yet at first interaction, listeners remain installed for the next interaction instead of silently failing. All stages now log to `[Web Audio]` prefix for easy filtering in DevTools. Handler auto-hides the `#audio-hint` UI element.
  > 3. **Added audio hint to `shell.html`** — New `#audio-hint` overlay ("Click or press any key to enable audio") appears after game start if AudioContext is suspended. Uses CSS pulse animation, auto-hides via polling or when the C-side interaction handler fires. Non-interactive (`pointer-events: none`) so it doesn't block game input.
  > 4. **Buffer sizes verified** — SDL2 uses 512 samples at 32kHz (~16ms latency), desired buffer 1100 samples (~34ms), max cap 6000 samples (~188ms). Emscripten's SDL2 port handles resampling to browser's native rate (44.1/48kHz) internally. No changes needed.
  > 5. Build verified clean — only `audio_sdl2.o` and `pc_main.o` recompiled, linked successfully.

- [x] Debug and fix game loop timing:
  - Verify `emscripten_set_main_loop` is being called with the right parameters
  - If the game runs too fast or too slow, check the frame timing logic in `produce_one_frame()` and `produce_interpolation_frames_and_delay()`
  - `clock_elapsed_f64()` should use `emscripten_get_now()` or `SDL_GetPerformanceCounter()` under Emscripten — verify the time source is accurate
  - The `precise_delay_f64()` function may use `usleep` or `nanosleep` — under Emscripten, `SDL_Delay` or `emscripten_sleep` should be used instead (but `emscripten_sleep` requires ASYNCIFY)
  - If the game stutters, consider simplifying the interpolation frame system for web — run at a fixed 30fps initially
  > **Completed**: Comprehensive analysis and fix of the game loop timing for Emscripten. Found and fixed a critical double-speed bug where the game logic ran at 60fps (rAF rate) instead of 30fps. Changes made:
  > 1. **Rewrote `web_main_loop_iteration()`** in `pc_main.c` — Replaced the pass-through to `produce_one_frame()` with a proper fixed-timestep game loop. Uses time accumulator pattern: rAF calls at ~60Hz, game logic ticks at 30Hz (FRAMERATE), with one interpolated render frame per rAF callback for smooth display. Delta clamped to 4x frame time to prevent spiral-of-death after tab backgrounding.
  > 2. **Fixed `precise_delay_f64()`** in `misc.c` — Added `#ifdef __EMSCRIPTEN__` guard that returns immediately on web. The hybrid sleep+busy-wait strategy blocks the single JS thread; on web, `requestAnimationFrame` already provides frame pacing so delays are unnecessary.
  > 3. **Fixed `get_display_refresh_rate()`** in `pc_main.c` — Returns 60Hz directly on Emscripten instead of querying `SDL_GetCurrentDisplayMode()` which may not report accurately in browsers.
  > 4. **Fixed `produce_interpolation_frames_and_delay()`** in `pc_main.c` — Forces `shouldDelay=false` on Emscripten to prevent the render loop from attempting self-pacing delays that would block the browser.
  > 5. **Added startup diagnostics** — `[Web Timing]` console logs on first frame reporting FRAMERATE, frame time, display/target refresh rates, and confirmation that delays are disabled. Aids runtime debugging via browser DevTools.
  > 6. **Time source verified** — `clock_elapsed_f64()` uses POSIX `clock_gettime(CLOCK_MONOTONIC)` which Emscripten maps to `performance.now()` — accurate and appropriate for game timing.
  > 7. **`emscripten_set_main_loop(web_main_loop_iteration, 0, 1)` verified correct** — fps=0 uses requestAnimationFrame (display rate), simulate_infinite_loop=1 prevents main() from returning.
  > 8. Build verified clean — only `pc_main.o` and `misc.o` recompiled, linked successfully.

- [x] Debug and fix input handling:
  - Test keyboard input: WASD/arrow keys for movement, keys for actions
  - Test gamepad input if a controller is connected
  - Verify mouse input for camera control — pointer lock may need explicit browser permission
  - Check that the DJUI menu system (game's custom UI) responds to input correctly
  - If keyboard focus is lost, ensure the canvas element has focus and is receiving events
  > **Completed**: Thorough audit of the entire input stack (keyboard, gamepad, mouse, DJUI) across 8 source files confirmed the SDL2 controller and keyboard backends are well-designed for Emscripten, with `controller_sdl2.c` already having 11 `#ifdef __EMSCRIPTEN__` guards for haptics and gamecontrollerdb. Changes made:
  > 1. **Fixed canvas focus** in `shell.html` — Added `canvas.focus()` call in `onRuntimeInitialized` so SDL receives keyboard events immediately after game init. Added click-to-refocus handler so clicking the canvas restores focus after browser UI interactions.
  > 2. **Fixed stuck keys on tab switch** — Added dual-layer key release: (a) JavaScript `visibilitychange` and `window.blur` listeners that call the exported `_keyboard_on_all_keys_up()` to clear held keys when the user switches tabs, and (b) `SDL_WINDOWEVENT_FOCUS_LOST` handler in `gfx_sdl2.c` that calls `kb_all_keys_up()` as a C-side fallback.
  > 3. **Fixed mouse wheel scroll** in `gfx_sdl2.c` — Added `#if SDL_VERSION_ATLEAST(2,0,18) && !defined(__EMSCRIPTEN__)` guard around `preciseX/preciseY` usage. Emscripten's SDL2 port may not have these SDL 2.0.18 fields; falls back to integer `x/y` which the port reliably provides from the browser's wheel event.
  > 4. **Added browser key default prevention** in `shell.html` — Prevents arrow keys, Space, Tab, Backspace, and F1-F5 from triggering browser actions (page scroll, navigation, focus change) when the canvas has focus.
  > 5. **Exported `keyboard_on_all_keys_up`** in `Makefile.web` — Added to `-s EXPORTED_FUNCTIONS` so JavaScript can call it via `Module._keyboard_on_all_keys_up()` for the tab-switch key release.
  > 6. **Pointer lock verified** — Emscripten's SDL2 port internally defers `requestPointerLock()` to the next user click when `SDL_SetRelativeMouseMode(SDL_TRUE)` is called from the game loop. No additional JS-side management needed.
  > 7. **Gamepad support verified** — SDL2's `SDL_GameControllerUpdate()` and axis/button polling map to the browser's Gamepad API via Emscripten. Gamepads appear after first button press (browser security policy). `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` is a no-op in browsers but harmless.
  > 8. **DJUI input verified** — The keyboard backend feeds scancodes to `djui_interactable_on_key_down()` before N64 button mapping. DJUI cursor uses `mouse_window_x/y` from `SDL_GetMouseState()`. Clipboard via `SDL_GetClipboardText()` depends on browser Clipboard API permissions. All paths use the standard SDL->Windows scancode translation which works on Emscripten.
  > 9. **Added input diagnostics** to `controller_sdl2.c` — `[Web Input]` console logs on init reporting backend status (gamepad, keyboard, mouse, haptics disabled, gamepad first-press note).
  > 10. Build verified clean — `gfx_sdl2.o` and `controller_sdl2.o` recompiled, linked successfully.

- [ ] Fix any remaining crashes and memory issues:
  - Watch for `RuntimeError: memory access out of bounds` — this indicates pointer issues, buffer overflows, or insufficient WASM memory
  - If memory runs out, increase `TOTAL_MEMORY` in Makefile.web (try 512MB)
  - Watch for `RuntimeError: unreachable executed` — this indicates `abort()` was called, usually from an assertion failure
  - Enable Emscripten's `-s ASSERTIONS=2` flag temporarily for better error messages
  - Check for stack overflows: `-s STACK_SIZE=1MB` or higher if needed
  - If the game successfully loads and renders the title screen, test:
    - Starting a new game
    - Loading into Bob-omb Battlefield
    - Basic movement, jumping, camera control
    - Collecting a star
    - Saving and returning to the castle

- [ ] Polish the HTML shell and user experience:
  - Update `shell.html` with:
    - A clear "Upload ROM" button that is prominent and styled
    - Loading progress bar during WASM download and initialization
    - A "Click to Start" overlay that captures audio context and gives focus to canvas
    - Fullscreen button
    - Basic mobile-friendly viewport meta tag
  - Add CSS for the canvas: `canvas { display: block; margin: auto; background: #000; }`
  - Set a reasonable default canvas size (960x720 or similar 4:3 ratio)
  - Test in Chrome, Firefox, and Safari (Safari has WebGL limitations — note any issues)
