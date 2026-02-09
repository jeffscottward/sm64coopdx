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

- [x] Verify and fix controller/input handling for Emscripten:
  - Read `src/pc/controller/controller_sdl.c` (or similar) — Emscripten's SDL2 maps browser Gamepad API to SDL_GameController
  - Read `src/pc/controller/controller_keyboard.c` — keyboard input via SDL2 events should work unmodified in Emscripten
  - Read `src/pc/controller/controller_mouse.c` — mouse input should work, but pointer lock (for camera control) needs `emscripten_request_pointerlock` or SDL's relative mouse mode
  - Check for any platform-specific scan code mappings that might differ in browser SDL2
  - Ensure the controller bind mapping system doesn't rely on filesystem paths that don't exist in WASM VFS

  **Completion Notes:**
  - **controller_sdl2.c**: Added `#ifndef __EMSCRIPTEN__` guards around all SDL_Haptic subsystem code (variables, init, helper function, rumble play/stop, shutdown cleanup) and `gamecontrollerdb.txt` file loading. Emscripten's SDL2 port does not include a functional haptic backend, and the browser's Gamepad API is mapped natively by Emscripten without needing external mapping files. Rumble functions become clean no-ops on web with `(void)` casts to suppress unused parameter warnings.
  - **controller_keyboard.c**: Verified fully compatible — uses only `keyboard_on_key_down`/`keyboard_on_key_up` callbacks invoked from `gfx_sdl2.c` SDL event loop. No platform-specific code. `controller_keyboard.h` has `#ifdef __APPLE__` for control key scancodes; Emscripten correctly falls through to the non-Apple branch.
  - **controller_mouse.c**: Verified fully compatible — uses `SDL_SetRelativeMouseMode()` (Emscripten maps this to the Pointer Lock API), `SDL_GetRelativeMouseState()`, and `SDL_GetMouseState()`. All `WAPI_DXGI`-specific code is properly guarded. No changes needed.
  - **controller_bind_mapping.c**: Uses SDL_Scancode values which are identical in Emscripten's SDL2 port. The `windows_scancode_table` mapping is SDL-internal and portable. No changes needed.
  - **Filesystem paths**: `fs_load_file("gamecontrollerdb.txt")` returns NULL if file not found; the `if (gcdata && gcsize)` guard at line 126 handles this gracefully. Additionally, this path is now skipped entirely for Emscripten via the `#ifndef __EMSCRIPTEN__` guard.
  - Tests: Native and Emscripten-simulated compilation guard tests pass with `-Wall -Wextra -Werror`.

- [x] Stub out the networking layer for single-player web builds:
  - Read `src/pc/network/network.c` to understand how `gNetworkSystem` is assigned
  - Read `src/pc/network/socket/socket.c` (or the socket network system implementation) — this uses raw BSD sockets (`socket()`, `bind()`, `sendto()`, `recvfrom()`) which are unavailable in browsers
  - For `TARGET_WEB`, when `network_init` is called with `NT_NONE`, the existing code should work since it doesn't open sockets
  - Verify that `network_init(NT_NONE, false)` in `pc_main.c` line 602 correctly skips socket initialization
  - If any socket headers (`<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`) are included unconditionally, wrap them in `#ifndef TARGET_WEB` guards
  - Emscripten provides partial POSIX socket stubs, but they don't actually work for UDP — better to guard them out explicitly
  - Check `src/pc/network/socket/socket.h` for platform includes that need guarding

  **Completion Notes:**
  - **socket_linux.h**: Added `#ifdef TARGET_WEB` / `#else` guard around the entire file. The web branch provides minimal type stubs (`SOCKET`, `INVALID_SOCKET`, `SOCKET_ERROR`, `NO_ERROR`, `SOCKET_EWOULDBLOCK`, `SOCKET_ECONNRESET`, `RX_ADDR_SIZE_TYPE`, `struct sockaddr_in6`, `struct in6_addr`, `INET6_ADDRSTRLEN`, `AF_INET6`) so that all downstream consumers (`socket.h` → `network.c`, `chat_commands.c`, `djui_panel_join_direct.c`, `djui_panel_join_lobbies.c`, `smlua_hooks.c`, `network_player.c`, `dev/chat.c`, `pc_main.c`) compile without pulling in unavailable POSIX headers (`<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<netdb.h>`, `<fcntl.h>`, `<unistd.h>`). The native branch is unchanged.
  - **socket_linux.c**: Wrapped entire BSD socket implementation (`socket_initialize`, `socket_shutdown` using `socket()`, `fcntl()`, `setsockopt()`, `close()`) in `#ifndef TARGET_WEB` guard. These POSIX socket calls are unavailable in browsers.
  - **socket.c**: Added `#ifdef TARGET_WEB` / `#else` guard around the entire implementation. The web branch provides a complete no-op `gNetworkSystemSocket` stub: `initialize()` returns `true` only for `NT_NONE` (preventing accidental server/client mode on web), `send()` returns `SOCKET_ERROR`, `dup_addr()` returns `NULL`, `match_addr()` returns `false`, all other functions are clean no-ops. `socket_initialize()` and `socket_shutdown()` are provided as trivial stubs. The native branch with `resolve_domain()`, `getaddrinfo()`, `sendto()`, `recvfrom()`, `bind()`, etc. is unchanged.
  - **socket.h**: No changes needed — it dispatches to `socket_linux.h` (non-WINSOCK path) which now handles TARGET_WEB internally.
  - **network_init(NT_NONE) flow verified**: `pc_main.c:636` calls `network_init(NT_NONE, false)`. In `network.c:147`, `gNetworkSystem->initialize(NT_NONE, false)` calls the web stub which returns `true`. The error check at line 148 (`!rc && inNetworkType != NT_NONE`) is skipped since `inNetworkType == NT_NONE`. `gNetworkType` is set to `NT_NONE`. No sockets are opened.
  - **Downstream consumers**: 8 files include `socket.h` (via `socket_linux.h`). All compile cleanly because the stub types in `socket_linux.h` provide the necessary struct/type definitions.
  - Tests: Native and TARGET_WEB compilation tests pass with `-Wall -Wextra -Werror`. Syntax checks on modified headers pass in both modes.

- [x] Handle Discord SDK and CoopNet exclusion cleanly:
  - Verify that `DISCORD_SDK=0` in the build flags properly excludes all Discord code via `#ifdef DISCORD_SDK` guards
  - Read `src/pc/discord/discord.c` briefly to confirm it's fully behind `#ifdef DISCORD_SDK`
  - Verify `COOPNET=0` properly excludes CoopNet code via `#ifdef COOPNET` guards
  - Read `src/pc/network/coopnet/` files briefly to confirm proper guarding
  - Check for any linker references to Discord or CoopNet symbols that might slip through despite the define being 0

  **Completion Notes:**
  - **Discord SDK exclusion (DISCORD_SDK=0)**: Verified clean exclusion via two complementary mechanisms. First, the Makefile conditionally includes `src/pc/discord` in `SRC_DIRS` only when `DISCORD_SDK=1` (Makefile line 521-522), so `discord.c`, `discord_activity.c`, and `discord_game_sdk.h` are never compiled for web builds. Second, the `-DDISCORD_SDK` preprocessor flag is only added when `DISCORD_SDK=1` (Makefile lines 1061-1063). All external references to Discord symbols (`discord_update()`, `discord_activity_update()`, `gDiscordInitialized`, `discord_get_user_id()`) are properly guarded by `#ifdef DISCORD_SDK` in their call sites: `pc_main.c` (lines 488-490, 647-649), `network.c` (lines 184-188, 796-800), `network_player.c` (lines 361-365, 415-419), and `smlua_misc_utils.c` (lines 493-505 for `get_local_discord_id()`). The Discord source files themselves do NOT need internal `#ifdef` guards because they're excluded at the build system level.
  - **CoopNet exclusion (COOPNET=0)**: Verified clean exclusion via preprocessor guards. Unlike Discord, the `src/pc/network/coopnet` directory IS always in `SRC_DIRS` (Makefile line 519), but the `-DCOOPNET` flag is only added when `COOPNET=1` (lines 1067-1069). The main implementation file `coopnet.c` wraps its entire body (lines 16-320) in `#ifdef COOPNET`/`#endif`, including the `gNetworkSystemCoopNet` symbol definition. The header `coopnet.h` similarly wraps all declarations in `#ifdef COOPNET`. The utility file `coopnet_id.c` is NOT guarded but contains only self-contained helper functions (ID management, dest ID tracking) that don't call external CoopNet library functions — they operate on local arrays and are harmless when compiled without the CoopNet library. All external references to CoopNet symbols are properly guarded: `network.c` (lines 102-104 for `gNetworkSystemCoopNet`, lines 464-466 for reconnect, lines 552-570 for update), `smlua_misc_utils.c` (lines 507-517 for `get_coopnet_id()`), and `discord_activity.c` (lines 8-10, 34-51, 146-157 for `#ifdef COOPNET` blocks).
  - **Lua bindings**: `smlua_functions_autogen.c` binds `network_discord_id_from_local_index`, `get_local_discord_id`, and `get_coopnet_id` unconditionally (lines 38354, 38960-38961), but these wrapper functions are safe — they call C implementations that have internal `#ifdef` guards returning fallback values ("0" for Discord, "-1" for CoopNet) when the features are disabled.
  - **Linker references**: No unguarded symbol references found. CoopNet/Discord native libraries are only linked when their respective flags are 1 (Makefile lines 598-616 for Discord, 970-996 for CoopNet).
  - **No code changes required** — the existing guard structure is complete and correct for web builds.
  - Tests: Compilation guard tests pass in all three modes (disabled, enabled, mixed) with `-Wall -Wextra -Werror`.

- [x] Handle the Mumble positional audio integration:
  - Read `src/pc/mumble/mumble.c` — this uses shared memory (`shm_open`, `mmap`) which doesn't exist in browsers
  - Wrap the entire Mumble implementation in `#ifndef TARGET_WEB` guards
  - Make `mumble_init()` and `mumble_update()` no-ops when `TARGET_WEB` is defined
  - The calls in `pc_main.c` at lines 469 and 613 reference Mumble — ensure they compile to nothing for web

  **Completion Notes:**
  - **mumble.h**: Added `#ifdef TARGET_WEB` / `#else` guard around the entire header contents. The web branch provides `static inline` no-op stubs for all four public functions (`mumble_init()`, `mumble_update()`, `mumble_update_menu()`, `should_update_context()`) so call sites in `pc_main.c` compile to nothing without needing `#ifdef` guards at every call. The `<stdint.h>` and `<wchar.h>` includes (needed for the `LinkedMem` struct) and the struct definition itself are moved into the native-only branch, since browsers have no use for the shared memory data layout.
  - **mumble.c**: Wrapped the entire implementation body (all includes, the `LinkedMem *lm` global, and all four functions including POSIX `shm_open`/`mmap`/`getuid`/`fcntl` and Windows `OpenFileMappingW`/`MapViewOfFile` calls) in `#ifndef TARGET_WEB` / `#endif`. The `#include "mumble.h"` remains unconditional so the header's no-op stubs satisfy the linker on web builds.
  - **pc_main.c**: No changes needed. The three Mumble call sites (line 477 `mumble_init()`, line 491 `mumble_update()` in `web_main_loop_iteration`, line 650 `mumble_update()` in the native main loop) all include `mumble.h`, which provides the `static inline` no-op stubs when `TARGET_WEB` is defined. The compiler optimizes these to nothing.
  - Tests: Native and TARGET_WEB compilation tests pass with `-Wall -Wextra -Werror`. Syntax checks on the actual modified headers pass in both modes.
