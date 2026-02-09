---
type: report
title: Web Multiplayer Connectivity Testing Report
created: 2026-02-09
tags:
  - multiplayer
  - websocket
  - networking
  - testing
related:
  - "[[web-port-status]]"
---

# Web Multiplayer Connectivity Testing Report

## Overview

This report documents the testing of the WebSocket-based multiplayer system for the SM64CoopDX web port (Phase 07). The system uses a Node.js WebSocket relay server to bridge communication between browser-based game clients, replacing the native UDP socket backend with WebSocket binary messaging.

## Architecture Under Test

```
┌──────────────┐     WebSocket     ┌─────────────────┐     WebSocket     ┌──────────────┐
│  Browser Tab  │ ◄──────────────► │   Relay Server   │ ◄──────────────► │  Browser Tab  │
│   (Host)      │    Binary + JSON  │  (Node.js + ws)  │    Binary + JSON  │   (Client)    │
└──────────────┘                   └─────────────────┘                   └──────────────┘
```

**Components tested:**
- `tools/web_relay/server.js` — Relay server (Node.js, `ws` library)
- `src/pc/network/websocket/network_websocket.c` — Game-side WebSocket `NetworkSystem` backend
- `src/pc/djui/djui_panel_host.c` / `djui_panel_join_direct.c` — DJUI integration for host/join UI

## Test Methodology

### Automated Testing

Since the game requires a ROM file and renders via WebGL in a browser, full end-to-end gameplay testing between two browser tabs is not automatable without a ROM. Instead, we validated the multiplayer stack through:

1. **Relay Server Unit Tests** (`test.js`) — 39 tests covering the relay server protocol
2. **Connectivity Integration Tests** (`test_connectivity.js`) — 40 tests simulating realistic multiplayer scenarios
3. **Build Verification** — Full Emscripten compilation confirming all WebSocket code links correctly

### What Was NOT Tested (Requires Manual Testing)

The following require manual testing with two browser tabs and a ROM:
- In-game player avatar visibility and rendering
- Movement synchronization fidelity
- Chat message delivery via DJUI
- Player-vs-player interactions (bumping, PvP)
- Game state transitions (star collection, level loading)
- Audio synchronization between clients

## Test Results

### Relay Server Unit Tests (test.js)

| Test | Result |
|------|--------|
| Health check endpoint | PASS |
| 404 for unknown paths | PASS |
| Room hosting (code generation) | PASS |
| Room joining (via code) | PASS |
| Binary data relay (broadcast) | PASS |
| Binary data relay (targeted) | PASS |
| Non-existent room join | PASS |
| Double host prevention | PASS |
| Multi-player join | PASS |
| Client disconnect notification | PASS |
| Host disconnect room closure | PASS |
| Invalid JSON handling | PASS |
| Unknown message type handling | PASS |
| Health check with active rooms | PASS |

**Result: 39/39 PASS**

### Connectivity Integration Tests (test_connectivity.js)

| Test Category | Tests | Result |
|---------------|-------|--------|
| Full host-join-play lifecycle | 6 | 6/6 PASS |
| Three-player room with broadcast | 12 | 12/12 PASS |
| Room isolation (concurrent rooms) | 6 | 6/6 PASS |
| Relay round-trip latency | 2 | 2/2 PASS |
| Rapid message burst (50 msgs) | 2 | 2/2 PASS |
| Large packet relay (3000 bytes) | 3 | 3/3 PASS |
| Player disconnect mid-game recovery | 5 | 5/5 PASS |
| Room code case insensitivity | 2 | 2/2 PASS |
| Room capacity check (4 players) | 2 | 2/2 PASS |

**Result: 40/40 PASS**

### Build Verification

- **Emscripten build**: Compiles and links successfully (552+ source files)
- **WebSocket library**: `-lwebsocket.js` links correctly
- **Output artifacts**: `sm64coopdx.html`, `.js`, `.wasm` (62MB), `.data`

## Performance Observations

### Relay Latency (localhost, Node.js)

| Metric | Value |
|--------|-------|
| Average RTT | 0.20 ms |
| Minimum RTT | 0.15 ms |
| Maximum RTT | 0.57 ms |
| Samples | 20 |

These numbers are for localhost only. Real-world latency will depend on:
- Geographic distance between clients and relay server
- Network conditions (ISP routing, congestion)
- Relay server hosting location

**Expected production latency**: 20-80ms for intra-continental, 100-200ms for cross-continental.

### Throughput

- **50-message burst**: All messages delivered in order, no drops
- **Large packets** (3000 bytes, matching SM64 `PACKET_LENGTH`): Relayed with full data integrity
- **Rate limiting**: 120 messages/second/client (sufficient for 30Hz game logic × 4 packets/frame)

## Known Limitations and Issues

### Architecture Limitations

1. **Relay adds latency**: Every message routes through the relay server, adding at least one network hop compared to direct P2P. For a LAN scenario, this adds ~0.2-1ms; for WAN, it adds the full RTT between client and relay.

2. **Single point of failure**: If the relay server goes down, all active games disconnect. There is no fallback or relay migration.

3. **No NAT traversal needed**: Unlike native UDP, WebSocket connections always succeed through NAT/firewalls since they use standard HTTP(S) ports. This is actually an advantage over the native port.

4. **No encryption by default**: The default relay URL uses `ws://` (unencrypted). Production deployments should use `wss://` with TLS termination.

### Game-Specific Considerations

1. **30Hz tick rate**: The game runs game logic at 30Hz. WebSocket adds no meaningful overhead at this frequency — even 120 messages/second is well within the rate limit.

2. **`requireServerBroadcast = true`**: The WebSocket backend relies on the relay to broadcast messages to all room members, matching the coopnet pattern. The relay server handles this correctly.

3. **Async WebSocket connect**: The Emscripten WebSocket API is asynchronous. The game backend queues host/join commands in `sPendingHost`/`sPendingJoinCode` and sends them from the `ws_on_open` callback. This was verified to work correctly in the C code review.

4. **JSON control protocol**: Room management (host/join/leave) uses text-based JSON messages, while game data uses binary messages. The relay correctly routes both types.

### Browser-Specific Considerations

1. **No SharedArrayBuffer/COOP/COEP**: The web build runs single-threaded (no pthreads), so there are no CORS isolation requirements for WebSocket connections.

2. **Tab visibility**: When a browser tab is backgrounded, `requestAnimationFrame` callbacks may be throttled. This could cause the game to fall behind on processing network messages, leading to micro-stutters when returning to the tab. The time accumulator in `web_main_loop_iteration()` helps catch up, but accumulated packets may cause a burst of updates.

3. **WebSocket disconnection detection**: Browser WebSocket connections may not detect disconnection immediately (no TCP keepalive equivalent by default). The relay server's room cleanup timer (60-second interval, 4-hour expiry) provides eventual cleanup.

## Recommendations for Production

1. **Deploy relay with TLS**: Use `wss://` in production. A reverse proxy like nginx or Caddy can handle TLS termination.

2. **Geographic relay placement**: Host relay servers in multiple regions (e.g., US-East, EU-West, Asia-Pacific) and let users select or auto-detect the closest one.

3. **Monitor relay health**: The `/health` endpoint provides room count, connection count, and uptime. Set up monitoring and alerting.

4. **Consider WebRTC for P2P**: For lower latency, a future enhancement could use WebRTC DataChannels for direct P2P communication, with the relay as a signaling server only. This would eliminate the relay hop for game data.

5. **Manual gameplay testing**: Before releasing to users, manually test with two browser tabs:
   - Start relay: `cd tools/web_relay && node server.js`
   - Open two tabs pointing to the web build
   - Host in one, join in the other
   - Verify player visibility, movement sync, and chat

## Conclusion

The WebSocket multiplayer relay system passes all automated tests (79/79 total) covering protocol correctness, room management, binary data relay, room isolation, latency, stress handling, and player lifecycle. The architecture is sound for browser-to-browser multiplayer. Manual gameplay testing with actual ROM and browser tabs is the remaining step to verify full game synchronization.
