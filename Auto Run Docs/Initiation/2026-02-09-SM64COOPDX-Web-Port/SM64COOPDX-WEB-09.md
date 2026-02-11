# Phase 09: Public Lobby Browsing for Web Builds

This phase adds public lobby browsing to the web build so players can discover and join hosted games without needing a room code. The native Mac/Linux/Windows app uses CoopNet for lobby listing (public/private/direct), but the web build currently bypasses the lobby menu entirely due to `#ifdef TARGET_WEB` guards, sending users straight to a room code input. This phase extends the WebSocket relay server to store room metadata, implements a WebSocket-based lobby query client in C, and modifies the DJUI join panels to show the lobby browser on web builds. By the end of this phase, web users clicking "Join" will see "Public Lobbies" and "Direct" options, with the lobby browser showing all active public games with host name, version, player count, and game mode.

## Diagnosis

The lobby browsing gap between native and web builds is caused by three compile-time guards:

1. **`src/pc/djui/djui_panel_join.c:18`** — `#ifdef TARGET_WEB` bypasses the lobby menu entirely, jumping straight to the direct connect (room code) panel
2. **`src/pc/djui/djui_panel_join_lobbies.c`** — The entire lobby browser UI is wrapped in `#ifdef COOPNET`, which is disabled on web builds (`COOPNET=0`)
3. **`src/pc/network/coopnet/coopnet.h`** — The `QueryCallbackPtr` and `QueryFinishCallbackPtr` typedefs used by the lobby browser are inside `#ifdef COOPNET`

Additionally, the WebSocket relay server (`tools/web_relay/server.js`) has a `"list"` command but it is blocked in production and returns minimal data (no host name, version, or game mode).

## Tasks

- [x] Create shared lobby query callback typedefs (`src/pc/network/lobby_query.h`):
  - Create a new header file `src/pc/network/lobby_query.h` with callback types currently locked inside `#ifdef COOPNET`:
    ```c
    typedef void (*LobbyQueryCallbackPtr)(uint64_t lobbyId, uint64_t ownerId,
        uint16_t connections, uint16_t maxConnections,
        const char* game, const char* version,
        const char* hostName, const char* mode, const char* description);
    typedef void (*LobbyQueryFinishCallbackPtr)(void);
    ```
  - Update `src/pc/network/coopnet/coopnet.h` to `#include "pc/network/lobby_query.h"` and alias the existing names for backward compatibility:
    ```c
    typedef LobbyQueryCallbackPtr QueryCallbackPtr;
    typedef LobbyQueryFinishCallbackPtr QueryFinishCallbackPtr;
    ```
  - Ensure existing CoopNet builds continue to compile without changes
  > **Completed**: `lobby_query.h` created with shared typedefs. `coopnet.h` updated to include it with backward-compatible aliases. Existing `coopnet.c` still compiles using old `QueryCallbackPtr`/`QueryFinishCallbackPtr` names which are now typedef aliases.

- [x] Extend the WebSocket relay server with room metadata (`tools/web_relay/server.js`):
  - Add metadata fields to the `Room` class constructor: `hostName` (string, max 64 chars), `version` (string, max 32 chars), `mode` (string, max 64 chars — main mod/game mode), `maxPlayers` (number, room-specific cap), `description` (string, max 512 chars — version + mods list), `isPublic` (boolean, default true)
  - Extend the `"host"` command handler (line 226) to accept metadata from the client's JSON message:
    ```json
    {"type":"host", "hostName":"PlayerName", "version":"2.1.1", "mode":"Vanilla", "maxPlayers":4, "isPublic":true, "description":""}
    ```
  - Truncate all incoming string fields server-side to prevent abuse
  - Override `Room.addClient()` to respect room-specific `maxPlayers` (capped by global `MAX_PLAYERS_PER_ROOM`)
  - Enable the `"list"` command for production — remove the `NODE_ENV === "production"` guard (line 284)
  - Return rich metadata in the `"room_list"` response:
    ```json
    {"type":"room_list", "rooms":[
      {"code":"ABC123", "hostName":"Jeff", "version":"2.1.1", "mode":"Vanilla", "players":2, "maxPlayers":4, "description":"2.1.1\n\nMods:\nDynos"}
    ]}
    ```
  - Skip private rooms (`isPublic: false`) and dead rooms (empty or host gone) in the list response
  - Add an `"update_room"` command for hosts to update their room metadata after creation (for mod changes, etc.)
  > **Completed**: All subtasks verified implemented. Room class has metadata fields with `truncStr()` sanitization. Host command extracts metadata from client JSON. `addClient()` respects room-specific `maxPlayers` capped by `MAX_PLAYERS_PER_ROOM`. List command enabled for all environments (no production guard). Room list returns rich metadata (code, hostName, version, mode, players, maxPlayers, description). Private and dead rooms filtered. `update_room` command allows host (clientIndex 0) to modify metadata post-creation. Build verified clean.

- [x] Implement the WebSocket lobby query client (`src/pc/network/websocket/network_websocket.c`):
  - Add `ns_websocket_query(LobbyQueryCallbackPtr callback, LobbyQueryFinishCallbackPtr finishCallback)`:
    - Opens a **dedicated query WebSocket** (separate from the game connection) to the relay URL
    - On open: sends `{"type":"list"}`
    - On message: parses the `room_list` JSON array response
    - For each room object: calls `callback(lobbyId, 0, players, maxPlayers, game, version, hostName, mode, description)`
    - After processing all rooms: calls `finishCallback()`
    - Closes and deletes the query WebSocket
    - On error: calls `finishCallback()` so the UI shows "No lobbies found" rather than hanging
  - Implement a JSON array parser for the `room_list` response:
    - Find the `"rooms"` array in the JSON string
    - Iterate over each `{...}` object by tracking brace depth
    - Extract fields using existing `ws_json_get_string()` and `ws_json_get_int()` helpers
  - Maintain a lobby ID → room code mapping (`sLobbyCodeMap[]`, max 128 entries):
    - The DJUI lobby entry stores a numeric `tag` (s64), but WebSocket join needs the 6-char room code string
    - Generate a hash-based `lobbyId` from the room code for the UI tag
    - Add `ns_websocket_get_lobby_code(uint64_t lobbyId)` lookup function
    - Clear the map at the start of each new query
  - Add `ws_json_escape()` helper to sanitize strings containing `"` or `\` before embedding in JSON
  - Update `src/pc/network/websocket/network_websocket.h` with declarations:
    ```c
    #include "pc/network/lobby_query.h"
    bool ns_websocket_query(LobbyQueryCallbackPtr callback, LobbyQueryFinishCallbackPtr finishCallback);
    const char* ns_websocket_get_lobby_code(uint64_t lobbyId);
    ```
  > **Completed**: All subtasks verified already implemented from a prior session. `ns_websocket_query()` opens a dedicated query WebSocket to the relay, sends `{"type":"list"}`, and parses the JSON `room_list` response using a custom brace-depth parser with `ws_json_get_string()`/`ws_json_get_int()` helpers. The `sLobbyCodeMap[128]` array maps hash-based lobby IDs to 6-char room codes for UI integration. `ws_json_escape()` sanitizes strings for JSON embedding. `ws_query_on_error` calls `finishCallback()` to prevent UI hangs. Header updated with `#include "pc/network/lobby_query.h"` and function declarations. Build verified clean.

- [x] Send host metadata when creating rooms (`src/pc/network/websocket/network_websocket.c`):
  - Modify `ns_websocket_send_host_command()` to include metadata in the JSON:
    - `configPlayerName` (from `pc/configfile.h`) — the host's display name
    - `get_version()` (from `pc/network/version.h`) — game version string
    - Active mod name (main mod or "Normal")
    - `configAmountOfPlayers` — max player count configured by host
    - `isPublic: true` — default to public lobby
  - Use `ws_json_escape()` to sanitize player names and mod names before embedding in JSON
  - Add required `#include` directives for `pc/network/version.h` and `pc/configfile.h`
  > **Completed**: `ns_websocket_send_host_command()` already included `configPlayerName`, `get_version()`, `configAmountOfPlayers`, and `isPublic:true` from a prior session. The remaining gap was the `mode` field which was hardcoded as `"Normal"` — now replaced with `mods_get_main_mod_name()` (from `pc/mods/mods.h`) which returns the largest enabled mod's name or "Super Mario 64" if no mods are enabled, matching the CoopNet implementation in `coopnet.c`. All string fields (`hostName`, `version`, `mode`) are sanitized via `ws_json_escape()`. Includes for `pc/configfile.h` and `pc/network/version.h` were already present; added `pc/mods/mods.h`. Build verified clean.

- [ ] Modify the DJUI join panel to show the lobby menu on web builds (`src/pc/djui/djui_panel_join.c`):
  - Replace the `#ifdef TARGET_WEB` block (line 18) that bypasses to `djui_panel_join_direct_create()`
  - Instead show a panel with:
    - "Public Lobbies" button → `djui_panel_join_public_lobbies`
    - "Direct" button → `djui_panel_join_direct_create`
    - "Back" button
  - Skip "Private Lobbies" for now — the relay doesn't support password-protected room listing yet
  - Ensure the `djui_panel_join_public_lobbies` function is accessible on web builds (it's currently inside `#ifdef COOPNET`)

- [ ] Make the lobby browser panel work with WebSocket query (`src/pc/djui/djui_panel_join_lobbies.c`):
  - Change the compile guard from `#ifdef COOPNET` to `#if defined(COOPNET) || defined(TARGET_WEB)`
  - Add `#ifdef TARGET_WEB` includes for `pc/network/websocket/network_websocket.h`
  - Add `#ifdef COOPNET` includes for `pc/network/coopnet/coopnet.h` (keep existing)
  - In the refresh function: call `ns_websocket_query()` instead of `ns_coopnet_query()` when `TARGET_WEB` is defined
  - In the create function: same treatment — dispatch to WebSocket or CoopNet query based on build target
  - In `djui_panel_join_lobby()`: add `#ifdef TARGET_WEB` path:
    - Look up room code via `ns_websocket_get_lobby_code(lobbyId)`
    - Call `ns_websocket_set_pending_join(roomCode)`
    - Set network system to `NS_WEBSOCKET`
    - Call `network_init(NT_CLIENT, false)`
  - Verify `src/pc/djui/djui_panel_join_lobbies.h` is NOT wrapped in `#ifdef COOPNET`

- [ ] Build and test the complete lobby browsing flow:
  - Build: `source ~/emsdk/emsdk_env.sh && gmake -f Makefile.web -j8`
  - Start relay: `node tools/web_relay/server.js`
  - Serve web build: `python3 serve_web.py` (or pm2)
  - Test with two browser tabs:
    - Tab 1: Open `http://localhost:8080/sm64coopdx.html` → Click "Host" → verify game starts and room is created with metadata
    - Tab 2: Open `http://localhost:8080/sm64coopdx.html` → Click "Join" → Click "Public Lobbies" → verify Tab 1's room appears in the list with host name, version, player count, and game mode
    - Click the lobby entry → verify it joins Tab 1's room
  - Verify native builds still compile (`COOPNET=1` path unchanged)
  - Clean up any debug printfs added during development

## Scope Limits

- **Public lobbies only** — no private/password-protected lobby listing (can be added in a future phase)
- **No Discord integration** — browser-only (Discord Rich Presence is not available in web context)
- **Snapshot listing** — lobby list is fetched on demand via refresh button, no live/push updates
- **No load balancing** — single relay server (CoopNet supports multi-server load balancing via `OnLoadBalance` callback)

## Files Changed

| File | Type | Description |
|------|------|-------------|
| `src/pc/network/lobby_query.h` | **NEW** | Shared callback typedefs for lobby queries |
| `src/pc/network/coopnet/coopnet.h` | Modify | Include `lobby_query.h`, alias typedefs for backward compatibility |
| `tools/web_relay/server.js` | Modify | Room metadata storage, enable list command for production, extend host command with metadata, add update_room command |
| `src/pc/network/websocket/network_websocket.c` | Modify | Add `ns_websocket_query()`, JSON array parser, lobby code map, host metadata in host command, JSON escape helper |
| `src/pc/network/websocket/network_websocket.h` | Modify | Declare `ns_websocket_query()` and `ns_websocket_get_lobby_code()` |
| `src/pc/djui/djui_panel_join.c` | Modify | Show lobby menu on web builds instead of bypassing to direct connect |
| `src/pc/djui/djui_panel_join_lobbies.c` | Modify | Change compile guard to include `TARGET_WEB`, add WebSocket query/join paths |
| `src/pc/djui/djui_panel_join_lobbies.h` | Verify | Ensure not wrapped in `#ifdef COOPNET` |
