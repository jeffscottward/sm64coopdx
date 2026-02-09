# SM64CoopDX WebSocket Relay Server

A lightweight Node.js WebSocket relay server that enables multiplayer for the SM64CoopDX web port. Since browsers cannot create UDP sockets, this relay bridges the gap — web clients connect via WebSockets, and the relay forwards game packets between players in the same room.

## Quick Start

```bash
cd tools/web_relay
npm install
node server.js
```

The server starts on port **8765** by default.

## How It Works

1. **Host** sends a `{"type": "host"}` JSON message to create a room. The server responds with a 6-character room code.
2. **Client** sends `{"type": "join", "roomCode": "ABCDEF"}` to join an existing room.
3. Once connected, all game data is sent as **binary WebSocket messages**:
   - Client sends: `[target_index] [packet_data...]`
   - Server relays: `[sender_index] [packet_data...]`
   - Use target index `0xFF` to broadcast to all other players in the room.
4. When the host disconnects, the room is closed and all clients are notified.

## Protocol

### Control Messages (JSON text)

| Direction | Type | Fields | Description |
|-----------|------|--------|-------------|
| Client→Server | `host` | — | Create a new room |
| Server→Client | `hosted` | `roomCode`, `clientIndex` | Room created, you are index 0 |
| Client→Server | `join` | `roomCode` | Join an existing room |
| Server→Client | `joined` | `roomCode`, `clientIndex` | Successfully joined |
| Server→Client | `player_joined` | `clientIndex` | Another player joined your room |
| Server→Client | `player_left` | `clientIndex` | A player left your room |
| Server→Client | `room_closed` | `reason` | Room was closed (host left) |
| Server→Client | `error` | `message` | Error description |

### Game Data (Binary)

All binary messages use the format:

```
[1 byte: index] [N bytes: game packet data]
```

- **Client→Server**: First byte is the **target** client index (or `0xFF` for broadcast)
- **Server→Client**: First byte is the **sender** client index

This matches the SM64CoopDX network packet format where `PACKET_LENGTH` is 3000 bytes.

## Configuration

Environment variables:

| Variable | Default | Description |
|----------|---------|-------------|
| `WS_RELAY_PORT` | `8765` | Server listen port |
| `WS_MAX_PLAYERS` | `16` | Max players per room |
| `WS_MAX_ROOMS` | `100` | Max concurrent rooms |
| `WS_MAX_CONNECTIONS` | `1000` | Max total connections |

## Health Check

```bash
curl http://localhost:8765/health
```

Returns JSON:

```json
{
  "status": "ok",
  "rooms": 0,
  "connections": 0,
  "uptime": 123.456
}
```

## Connecting from the Game

The game's WebSocket backend (`src/pc/network/websocket/network_websocket.c`) connects to the relay URL configured in `configWebSocketRelay` (default: `ws://localhost:8765`). This can be changed in the game's config file (`sm64config.txt`) under the `websocket_relay` key.

## Development

List active rooms (development mode only):

```bash
wscat -c ws://localhost:8765
> {"type": "list"}
```

## Architecture

```
Browser A (Host)         Relay Server         Browser B (Client)
     |                       |                       |
     |-- host -------------->|                       |
     |<-- hosted (code) -----|                       |
     |                       |<-- join (code) -------|
     |<-- player_joined -----|--- joined ----------->|
     |                       |                       |
     |== binary game data ==>|== binary game data ==>|
     |<= binary game data ===|<= binary game data ===|
```
