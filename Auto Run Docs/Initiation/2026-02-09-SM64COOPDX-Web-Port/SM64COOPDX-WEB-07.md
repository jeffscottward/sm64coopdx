# Phase 07: Multiplayer via WebSocket Network Backend

This phase implements multiplayer support for the web port by creating a new `NetworkSystem` backend that uses WebSockets instead of raw UDP sockets. Since browsers cannot create UDP sockets, a WebSocket relay server bridges the gap — web clients connect to the relay via WebSockets, and the relay can either forward to other WebSocket clients (web-to-web play) or translate to UDP for connecting to native desktop hosts. This brings the core multiplayer experience to the browser.

## Tasks

- [x] Create the WebSocket network system backend `src/pc/network/websocket/network_websocket.c`:
  - Implement the `NetworkSystem` interface (defined in `src/pc/network/network.h`):
    - `initialize()` — open a WebSocket connection to a relay server URL
    - `get_id()` / `get_id_str()` — return a unique client ID from the WebSocket session
    - `send()` — send packet data as a WebSocket binary message
    - `update()` — poll for incoming WebSocket messages and dispatch to the network packet handler
    - `shutdown()` — close the WebSocket connection
    - Other functions: `save_id`, `clear_id`, `dup_addr`, `match_addr`, `get_lobby_id`, `get_lobby_secret`
  - For Emscripten, use the `emscripten_websocket_*` API (from `<emscripten/websocket.h>`):
    - `emscripten_websocket_new()` — create a WebSocket
    - `emscripten_websocket_set_onmessage_callback()` — receive messages
    - `emscripten_websocket_send_binary()` — send binary data
    - `emscripten_websocket_close()` — close connection
    - `emscripten_websocket_delete()` — cleanup
  - Buffer incoming messages in a queue for the `update()` function to process
  - Add `NS_WEBSOCKET` to the `NetworkSystemType` enum in `network.h`
  - Register it in the network system selection code
  - Create corresponding header `src/pc/network/websocket/network_websocket.h`
  - Guard with `#ifdef TARGET_WEB` so it only compiles for web builds

  **Completion Notes (2026-02-09):**
  - Created `src/pc/network/websocket/network_websocket.h` — header declaring `gNetworkSystemWebSocket`, guarded with `#ifdef TARGET_WEB`
  - Created `src/pc/network/websocket/network_websocket.c` — full `NetworkSystem` implementation using Emscripten's `emscripten_websocket_*` API:
    - `ns_websocket_initialize()` creates a WebSocket via `emscripten_websocket_new()` connecting to the configurable relay URL
    - `ws_on_message()` callback buffers incoming binary messages in a circular queue (`WS_MSG_QUEUE_SIZE=128`), extracting the 1-byte client index header
    - `ns_websocket_update()` drains the queue and dispatches via `network_receive()`
    - `ns_websocket_send()` prepends target client index byte and sends via `emscripten_websocket_send_binary()`
    - `ns_websocket_shutdown()` gracefully closes and deletes the WebSocket
    - All other interface functions (`get_id`, `get_id_str`, `save_id`, `clear_id`, `dup_addr`, `match_addr`, `get_lobby_id`, `get_lobby_secret`) implemented following the coopnet pattern with s64 client IDs
  - Added `NS_WEBSOCKET` to `NetworkSystemType` enum in `network.h` (guarded with `#ifdef TARGET_WEB`)
  - Registered `NS_WEBSOCKET` in `network_set_system()`, `network_reconnect_begin()`, and `network_reconnect_update()` in `network.c`
  - Added `src/pc/network/websocket` to `SRC_DIRS` in `Makefile` (line 519)
  - Added `-lwebsocket.js` to `WEB_LDFLAGS` in `Makefile.web` for Emscripten WebSocket library linking
  - Added `configWebSocketRelay` config variable to `configfile.h`/`configfile.c` with default `ws://localhost:8765` and config table entry `websocket_relay`
  - `.requireServerBroadcast = true` — relay server handles message routing
  - Build verified: all 552+ source files compile and link successfully with emcc/em++

- [x] Create a WebSocket relay server (`tools/web_relay/`):
  - Build a lightweight Node.js WebSocket server using the `ws` library:
    - `tools/web_relay/server.js` — main server file
    - `tools/web_relay/package.json` — dependencies (just `ws`)
  - The server manages "rooms" (lobbies):
    - A host creates a room and gets a room code
    - Clients join by room code
    - All messages from one client are broadcast to all others in the room
  - Message format: prepend a 1-byte client index header to the raw game packet data
  - Support configurable port (default 8765) and max players per room (16, matching the game)
  - Add basic rate limiting and connection limits
  - Include a simple health check endpoint at `/health`
  - Document usage in a README in the relay directory

  **Completion Notes (2026-02-09):**
  - Created `tools/web_relay/server.js` — Node.js WebSocket relay server using the `ws` library:
    - **Room management**: Host creates rooms (6-char alphanumeric code), clients join by code
    - **Binary relay**: Messages forwarded with 1-byte sender index header; supports both broadcast (target `0xFF`) and targeted sends to specific client indices
    - **Control protocol**: JSON text messages for `host`, `join`, `list` (dev only); server sends `hosted`, `joined`, `player_joined`, `player_left`, `room_closed`, `error`
    - **Rate limiting**: 120 messages/second per connection (sliding window)
    - **Connection limits**: Max 1000 total connections, 100 rooms, 16 players per room
    - **Health check**: `GET /health` returns JSON with status, room count, connection count, uptime
    - **Room lifecycle**: Host disconnect closes room and notifies all clients; stale rooms cleaned up every 60 seconds (4-hour expiry)
    - **Room codes**: Use unambiguous characters (no I/O/0/1) to avoid confusion
  - Created `tools/web_relay/package.json` — single dependency on `ws ^8.16.0`
  - Created `tools/web_relay/test.js` — 39 automated tests covering:
    - Health check endpoint, 404 handling
    - Room creation, joining, room code validation
    - Binary data relay (broadcast and targeted)
    - Error handling (bad room codes, double host, invalid JSON, unknown message types)
    - Client disconnect notifications, host disconnect room closure
  - Created `tools/web_relay/README.md` — complete documentation with protocol reference, architecture diagram, configuration options
  - All environment variables configurable: `WS_RELAY_PORT`, `WS_MAX_PLAYERS`, `WS_MAX_ROOMS`, `WS_MAX_CONNECTIONS`
  - All 39 tests pass

- [x] Integrate the WebSocket backend into the game's network selection UI:
  - Read `src/pc/djui/djui_panel.c` and related DJUI panel files to understand the host/join UI
  - For web builds, modify the network system selection:
    - Remove Socket and CoopNet options (they don't work in browsers)
    - Default to WebSocket backend (`NS_WEBSOCKET`)
    - The "Host" option should start a WebSocket room via the relay server
    - The "Join" option should connect to a room by entering a room code
  - Update `src/pc/djui/djui_panel_join_message.c` (or equivalent) to show a room code input field instead of IP:port
  - Update `src/pc/djui/djui_panel_host_message.c` (or equivalent) to display the room code after hosting

  **Completion Notes (2026-02-09):**
  - **Enhanced `src/pc/network/websocket/network_websocket.c`** — Added full JSON control message protocol support:
    - Text message handler (`ws_handle_control_message`) parses relay responses: `hosted`, `joined`, `player_joined`, `player_left`, `room_closed`, `error`
    - `ns_websocket_send_host_command()` — sends `{"type":"host"}` to create a room on the relay
    - `ns_websocket_send_join_command(roomCode)` — sends `{"type":"join","roomCode":"..."}` to join an existing room
    - `ns_websocket_set_pending_join(roomCode)` — queues a join command to fire after async WebSocket connection opens
    - Pending host/join commands deferred to `ws_on_open` callback (WebSocket connection is async)
    - Room code, client index, and connection state (`sInRoom`, `sWaitingForRoom`) tracked statically
    - Relay errors displayed to user via `djui_panel_join_message_error()`
    - Simple JSON parser (no library dependency) extracts string and integer values from relay messages
  - **Updated `src/pc/network/websocket/network_websocket.h`** — Added public API declarations: `ns_websocket_send_host_command`, `ns_websocket_send_join_command`, `ns_websocket_get_room_code`, `ns_websocket_is_connected`, `ns_websocket_set_pending_join`
  - **Modified `src/pc/djui/djui_panel_host.c`** — For `TARGET_WEB`:
    - Replaced Socket/CoopNet network system dropdown with WebSocket relay info text
    - Hides port/password input fields (creates hidden dummy inputbox to satisfy port validator reference)
    - Host button forces `NS_WEBSOCKET` and goes directly to hosting (no port forwarding warning)
    - Port validation and text change callbacks guarded with `#ifndef TARGET_WEB`
  - **Modified `src/pc/djui/djui_panel_host_message.c`** — For `TARGET_WEB`:
    - `djui_panel_do_host()` forces `configNetworkSystem = NS_WEBSOCKET` on web builds
    - Host confirmation panel shows WebSocket-specific message instead of port forwarding instructions
  - **Modified `src/pc/djui/djui_panel_join.c`** — For `TARGET_WEB`:
    - Bypasses CoopNet Public/Private/Direct menu and goes directly to room code join panel
  - **Modified `src/pc/djui/djui_panel_join_direct.c`** — For `TARGET_WEB`:
    - Shows "Enter a room code to join" prompt instead of IP:port instructions
    - Inputbox limited to 32 chars (room codes are 6 chars, alphanumeric)
    - Join button sets pending join code, forces `NS_WEBSOCKET`, and initializes as client
    - IP parsing functions guarded with `#ifndef TARGET_WEB` (not needed for room codes)
  - **Added localization strings to `lang/English.ini`**:
    - `[HOST] WEBSOCKET_INFO` — relay hosting info text for host panel
    - `[HOST_MESSAGE] WARN_WEBSOCKET` — WebSocket host confirmation message
    - `[JOIN] JOIN_WEBSOCKET` — room code join prompt
  - Build verified: all 552+ source files compile and link successfully with emcc/em++

- [x] Handle the relay server URL configuration:
  - Add a config option `configWebSocketRelay` to `src/pc/configfile.c` for the relay server URL (default: `ws://localhost:8765`)
  - For production, this would point to a publicly hosted relay server
  - Add an environment variable override: `SM64_WS_RELAY` that can be baked in at build time
  - In the DJUI settings panel, add a field to configure the relay URL

  **Completion Notes (2026-02-09):**
  - **Config option already existed** — `configWebSocketRelay` was added in the Phase 07 backend task (configfile.h:166, configfile.c:202, config table entry at configfile.c:363)
  - **Added `SM64_WS_RELAY` build-time env var** in `Makefile.web`:
    - `SM64_WS_RELAY ?=` optional Make variable, passed as `-DSM64_WS_RELAY_URL=\\\"<url>\\\"` via `WEB_EXTRA_CFLAGS`
    - Usage: `gmake -f Makefile.web SM64_WS_RELAY=wss://relay.example.com`
    - Build-time define takes priority over `configWebSocketRelay` in `ns_websocket_initialize()`
  - **Added DJUI relay URL input field** in `src/pc/djui/djui_panel_host.c`:
    - Editable inputbox on the Host panel (web builds only) showing current `configWebSocketRelay` value
    - Real-time validation: must start with `ws://` or `wss://`, length > 6, fits in `MAX_CONFIG_STRING` (64)
    - Invalid URLs highlighted in red; reverts to current config on focus loss if invalid
    - Value saved to `configWebSocketRelay` on valid change (persisted via `configfile_save`)
  - **Added localization string** `RELAY_URL = "Relay URL"` under `[HOST]` in `lang/English.ini`
  - **Fixed code review issues**:
    - Added JSON injection prevention in `ns_websocket_send_join_command()` — room codes validated to alphanumeric only
    - Added NULL and bounds checks in `ns_websocket_dup_addr()`
    - Added URL length truncation check in relay URL validation
  - Build verified: compiles and links successfully with and without `SM64_WS_RELAY` override

- [ ] Test multiplayer connectivity:
  - Start the relay server: `cd tools/web_relay && npm install && node server.js`
  - Open two browser tabs/windows with the web build
  - Host a game in one tab, join from the other using the room code
  - Verify:
    - Both players see each other in the game
    - Movement is synchronized
    - Chat messages work
    - Player interactions work (bumping, PvP if enabled)
  - Document any latency or sync issues in `docs/research/web-multiplayer-testing.md` with front matter:
    - type: report, tags: [multiplayer, websocket, networking, testing]
