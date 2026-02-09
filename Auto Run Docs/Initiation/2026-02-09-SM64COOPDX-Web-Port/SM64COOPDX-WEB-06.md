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

- [ ] Debug and fix WebGL rendering issues:
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

- [ ] Debug and fix audio playback:
  - Verify SDL_audio initializes correctly (check console for "SDL audio" related messages)
  - If audio doesn't play, it's likely the browser autoplay policy:
    - Add a "Click to Start" overlay that resumes the AudioContext on first user interaction
    - Use `EM_ASM({ var ctx = SDL.audioContext; if (ctx && ctx.state === 'suspended') ctx.resume(); })` on first input event
  - Check audio buffer underrun/overrun — if audio stutters, adjust buffer sizes
  - Verify the audio sample rate matches what the browser expects (typically 44100 or 48000 Hz)

- [ ] Debug and fix game loop timing:
  - Verify `emscripten_set_main_loop` is being called with the right parameters
  - If the game runs too fast or too slow, check the frame timing logic in `produce_one_frame()` and `produce_interpolation_frames_and_delay()`
  - `clock_elapsed_f64()` should use `emscripten_get_now()` or `SDL_GetPerformanceCounter()` under Emscripten — verify the time source is accurate
  - The `precise_delay_f64()` function may use `usleep` or `nanosleep` — under Emscripten, `SDL_Delay` or `emscripten_sleep` should be used instead (but `emscripten_sleep` requires ASYNCIFY)
  - If the game stutters, consider simplifying the interpolation frame system for web — run at a fixed 30fps initially

- [ ] Debug and fix input handling:
  - Test keyboard input: WASD/arrow keys for movement, keys for actions
  - Test gamepad input if a controller is connected
  - Verify mouse input for camera control — pointer lock may need explicit browser permission
  - Check that the DJUI menu system (game's custom UI) responds to input correctly
  - If keyboard focus is lost, ensure the canvas element has focus and is receiving events

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
