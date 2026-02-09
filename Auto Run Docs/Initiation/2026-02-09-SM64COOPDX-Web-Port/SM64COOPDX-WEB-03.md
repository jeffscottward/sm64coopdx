# Phase 03: Audio, Input, and Networking Stubs for Web

This phase adapts the audio system, controller input, and networking layer for browser execution. The audio system already uses SDL2 which Emscripten supports, but browser audio requires user interaction to start (autoplay policy). Controller input via SDL2 mostly works through Emscripten's Gamepad API mapping, but keyboard handling needs verification. Networking (UDP sockets, CoopNet) must be stubbed out for the initial single-player web build since browsers cannot create raw UDP sockets. This phase ensures all non-rendering subsystems compile and function under Emscripten.

## Tasks

- [x] Adapt the audio system for web browser execution. Read `src/pc/audio/audio_sdl.h` and the corresponding `.c` file:
  - Emscripten's SDL2 port includes SDL_audio support that uses the Web Audio API internally
  - Browser autoplay policy requires user interaction before audio can start. Add a mechanism to resume the SDL audio device after first user click/keypress:
    - In the SDL2 window manager init or in `pc_main.c`, register a one-time event handler that calls `SDL_PauseAudioDevice(dev, 0)` on first interaction
    - Alternatively, use `EM_ASM` to add a JavaScript click handler that resumes the AudioContext
  - The `audio_thread` function in `pc_main.c` runs audio in a separate thread — for the single-threaded web build, audio buffering should happen inline in `produce_one_frame()` (this is already handled by the `if (gAudioThread.state == INVALID)` check at line 361 of pc_main.c)
  - Verify that `SAMPLES_HIGH` (544) and `SAMPLES_LOW` (528) buffer sizes are reasonable for web audio latency
  - Check that `create_next_audio_buffer` and the audio pipeline don't use any POSIX-specific features

  **Completion Notes:**
  - Created `src/pc/audio/audio_web.h` — browser autoplay policy workaround header following the `gfx_web_util.h` cross-platform pattern. Uses `EM_ASM` to inject a one-shot JavaScript event listener (click/keydown/touchstart) that calls `SDL2.audioContext.resume()` on first user interaction. Includes `audio_web_is_running()` for UI hints. Compiles to nothing on native builds.
  - Added `#include "audio/audio_web.h"` and `audio_web_setup_resume()` call in `pc_main.c` after audio API initialization (line ~593).
  - **Audio threading**: Verified `gAudioThread` is initialized to `{ 0 }` in `data.c`, so `state == INVALID`. The `produce_one_frame()` check at line 365 correctly routes audio through inline `buffer_audio()` for single-threaded web builds. Mutex operations are already no-ops via `thread.h` TARGET_WEB stubs.
  - **Buffer sizes**: `SAMPLES_HIGH=544`, `SAMPLES_LOW=528` at 32kHz = ~17ms per buffer. Web Audio API typically uses 128-1024 sample buffers. SDL2's Emscripten port uses `ScriptProcessorNode`/`AudioWorklet` internally with compatible buffer sizes. No changes needed.
  - **Audio pipeline POSIX audit**: `create_next_audio_buffer()` calls `synthesis_execute()` which is pure audio DSP. All N64 OS primitives (`osPiStartDma`, `osInvalDCache`, `osRecvMesg`, etc.) are already reimplemented as portable C stubs in `ultra_reimplementation.c` (memcpy/no-ops). No POSIX-specific features found.
  - Tests: Native and Emscripten-stub compilation tests pass with `-Wall -Wextra -Werror`.

- [ ] Verify and fix controller/input handling for Emscripten:
  - Read `src/pc/controller/controller_sdl.c` (or similar) — Emscripten's SDL2 maps browser Gamepad API to SDL_GameController
  - Read `src/pc/controller/controller_keyboard.c` — keyboard input via SDL2 events should work unmodified in Emscripten
  - Read `src/pc/controller/controller_mouse.c` — mouse input should work, but pointer lock (for camera control) needs `emscripten_request_pointerlock` or SDL's relative mouse mode
  - Check for any platform-specific scan code mappings that might differ in browser SDL2
  - Ensure the controller bind mapping system doesn't rely on filesystem paths that don't exist in WASM VFS

- [ ] Stub out the networking layer for single-player web builds:
  - Read `src/pc/network/network.c` to understand how `gNetworkSystem` is assigned
  - Read `src/pc/network/socket/socket.c` (or the socket network system implementation) — this uses raw BSD sockets (`socket()`, `bind()`, `sendto()`, `recvfrom()`) which are unavailable in browsers
  - For `TARGET_WEB`, when `network_init` is called with `NT_NONE`, the existing code should work since it doesn't open sockets
  - Verify that `network_init(NT_NONE, false)` in `pc_main.c` line 602 correctly skips socket initialization
  - If any socket headers (`<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`) are included unconditionally, wrap them in `#ifndef TARGET_WEB` guards
  - Emscripten provides partial POSIX socket stubs, but they don't actually work for UDP — better to guard them out explicitly
  - Check `src/pc/network/socket/socket.h` for platform includes that need guarding

- [ ] Handle Discord SDK and CoopNet exclusion cleanly:
  - Verify that `DISCORD_SDK=0` in the build flags properly excludes all Discord code via `#ifdef DISCORD_SDK` guards
  - Read `src/pc/discord/discord.c` briefly to confirm it's fully behind `#ifdef DISCORD_SDK`
  - Verify `COOPNET=0` properly excludes CoopNet code via `#ifdef COOPNET` guards
  - Read `src/pc/network/coopnet/` files briefly to confirm proper guarding
  - Check for any linker references to Discord or CoopNet symbols that might slip through despite the define being 0

- [ ] Handle the Mumble positional audio integration:
  - Read `src/pc/mumble/mumble.c` — this uses shared memory (`shm_open`, `mmap`) which doesn't exist in browsers
  - Wrap the entire Mumble implementation in `#ifndef TARGET_WEB` guards
  - Make `mumble_init()` and `mumble_update()` no-ops when `TARGET_WEB` is defined
  - The calls in `pc_main.c` at lines 469 and 613 reference Mumble — ensure they compile to nothing for web
