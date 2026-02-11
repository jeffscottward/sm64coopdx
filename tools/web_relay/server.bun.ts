/**
 * SM64CoopDX WebSocket Relay Server (Bun)
 *
 * Production-ready relay server using Bun's native WebSocket support.
 * Handles room hosting/joining, binary game packet relay, lobby listing,
 * and CoopNet lobby proxy integration.
 *
 * Usage:
 *   bun run server.bun.ts
 *   WS_RELAY_PORT=8765 bun run server.bun.ts
 */

import { execFile } from "node:child_process";
import { existsSync } from "node:fs";
import { join } from "node:path";

// --- Configuration ---
const PORT = parseInt(Bun.env.WS_RELAY_PORT || "8765", 10);
const MAX_PLAYERS_PER_ROOM = parseInt(Bun.env.WS_MAX_PLAYERS || "16", 10);
const MAX_ROOMS = parseInt(Bun.env.WS_MAX_ROOMS || "100", 10);
const MAX_CONNECTIONS = parseInt(Bun.env.WS_MAX_CONNECTIONS || "1000", 10);
const RATE_LIMIT_WINDOW_MS = 1000;
const RATE_LIMIT_MAX_MESSAGES = 120;
const ROOM_CODE_LENGTH = 6;

// --- Types ---
interface RoomMeta {
  hostName?: string;
  version?: string;
  mode?: string;
  maxPlayers?: number;
  description?: string;
  isPublic?: boolean;
}

interface WsData {
  clientIndex: number | undefined;
  room: Room | null;
  rateLimitWindowStart: number;
  rateLimitCount: number;
}

type ServerWs = {
  data: WsData;
  send(data: string | ArrayBuffer | Uint8Array): void;
  close(code?: number, reason?: string): void;
  readyState: number;
};

// --- Room Management ---
const rooms = new Map<string, Room>();
let totalConnections = 0;

function truncStr(str: unknown, maxLen: number): string {
  if (typeof str !== "string") return "";
  return str.slice(0, maxLen);
}

class Room {
  code: string;
  clients: Map<number, ServerWs>;
  host: ServerWs;
  nextClientIndex: number;
  createdAt: number;
  hostName: string;
  version: string;
  mode: string;
  maxPlayers: number;
  description: string;
  isPublic: boolean;

  constructor(code: string, host: ServerWs, meta: RoomMeta) {
    this.code = code;
    this.clients = new Map();
    this.host = host;
    this.nextClientIndex = 1;
    this.createdAt = Date.now();
    this.hostName = truncStr(meta.hostName, 64);
    this.version = truncStr(meta.version, 32);
    this.mode = truncStr(meta.mode, 64);
    this.maxPlayers = Math.min(
      Math.max(parseInt(String(meta.maxPlayers)) || MAX_PLAYERS_PER_ROOM, 2),
      MAX_PLAYERS_PER_ROOM
    );
    this.description = truncStr(meta.description, 512);
    this.isPublic = meta.isPublic !== false;
  }

  addClient(ws: ServerWs): number {
    if (this.clients.size >= this.maxPlayers) return -1;
    const index = this.host === ws ? 0 : this.nextClientIndex++;
    this.clients.set(index, ws);
    ws.data.clientIndex = index;
    ws.data.room = this;
    return index;
  }

  removeClient(ws: ServerWs): void {
    if (ws.data.clientIndex !== undefined) {
      this.clients.delete(ws.data.clientIndex);
    }
  }

  isEmpty(): boolean {
    return this.clients.size === 0;
  }

  isHostGone(): boolean {
    return !this.clients.has(0);
  }

  broadcast(senderIndex: number, data: Uint8Array): void {
    const relay = new Uint8Array(1 + data.length);
    relay[0] = senderIndex;
    relay.set(data, 1);
    for (const [index, client] of this.clients) {
      if (index !== senderIndex && client.readyState === 1) {
        client.send(relay);
      }
    }
  }

  sendTo(targetIndex: number, senderIndex: number, data: Uint8Array): void {
    const target = this.clients.get(targetIndex);
    if (target && target.readyState === 1) {
      const relay = new Uint8Array(1 + data.length);
      relay[0] = senderIndex;
      relay.set(data, 1);
      target.send(relay);
    }
  }
}

function generateRoomCode(): string {
  const chars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  let code: string;
  do {
    code = "";
    for (let i = 0; i < ROOM_CODE_LENGTH; i++) {
      code += chars[Math.floor(Math.random() * chars.length)];
    }
  } while (rooms.has(code));
  return code;
}

function checkRateLimit(ws: ServerWs): boolean {
  const now = Date.now();
  if (now - ws.data.rateLimitWindowStart > RATE_LIMIT_WINDOW_MS) {
    ws.data.rateLimitWindowStart = now;
    ws.data.rateLimitCount = 0;
  }
  ws.data.rateLimitCount++;
  return ws.data.rateLimitCount <= RATE_LIMIT_MAX_MESSAGES;
}

// --- CoopNet Lobby Proxy ---
const COOPNET_PROXY_PATH = join(import.meta.dir, "coopnet_proxy");
const COOPNET_CACHE_TTL_MS = 30_000;
const COOPNET_HOST = Bun.env.COOPNET_HOST || "net.coop64.us";
const COOPNET_PORT = Bun.env.COOPNET_PORT || "34197";

interface CoopNetLobby {
  code: string;
  hostName: string;
  version: string;
  mode: string;
  players: number;
  maxPlayers: number;
  description: string;
  source: string;
  lobbyId: number;
}

let coopnetLobbies: CoopNetLobby[] = [];
let coopnetFetching = false;

function fetchCoopNetLobbies(): void {
  if (coopnetFetching) return;
  if (!existsSync(COOPNET_PROXY_PATH)) return;

  coopnetFetching = true;
  const lobbies: CoopNetLobby[] = [];

  execFile(COOPNET_PROXY_PATH, [COOPNET_HOST, COOPNET_PORT], {
    timeout: 20_000,
    maxBuffer: 1024 * 1024,
    env: {
      ...process.env,
      DYLD_LIBRARY_PATH: join(import.meta.dir, "../../lib/coopnet/mac_arm"),
      LD_LIBRARY_PATH: join(import.meta.dir, "../../lib/coopnet/linux"),
    },
  }, (error, stdout, stderr) => {
    coopnetFetching = false;
    if (error) {
      console.error(`CoopNet proxy error: ${error.message}`);
      if (stderr) console.error(`  stderr: ${stderr.trim()}`);
      return;
    }

    for (const line of stdout.split("\n")) {
      const trimmed = line.trim();
      if (trimmed === "END" || trimmed === "") continue;
      try {
        const lobby = JSON.parse(trimmed);
        lobbies.push({
          code: `CN-${lobby.lobbyId}`,
          hostName: lobby.hostName || "",
          version: lobby.version || "",
          mode: lobby.mode || "",
          players: lobby.players || 0,
          maxPlayers: lobby.maxPlayers || 16,
          description: lobby.description || "",
          source: "coopnet",
          lobbyId: lobby.lobbyId,
        });
      } catch { /* skip malformed */ }
    }

    coopnetLobbies = lobbies;
    console.log(`CoopNet proxy: cached ${lobbies.length} lobbies`);
  });
}

setInterval(fetchCoopNetLobbies, COOPNET_CACHE_TTL_MS);
setTimeout(fetchCoopNetLobbies, 2000);

// --- Control Message Handler ---
function handleControlMessage(ws: ServerWs, text: string): void {
  let msg: any;
  try {
    msg = JSON.parse(text);
  } catch {
    ws.send(JSON.stringify({ type: "error", message: "Invalid JSON" }));
    return;
  }

  switch (msg.type) {
    case "host": {
      if (ws.data.room) {
        ws.send(JSON.stringify({ type: "error", message: "Already in a room" }));
        return;
      }
      if (rooms.size >= MAX_ROOMS) {
        ws.send(JSON.stringify({ type: "error", message: "Server full, no room slots" }));
        return;
      }
      const code = generateRoomCode();
      const room = new Room(code, ws, {
        hostName: msg.hostName,
        version: msg.version,
        mode: msg.mode,
        maxPlayers: msg.maxPlayers,
        description: msg.description,
        isPublic: msg.isPublic,
      });
      room.addClient(ws);
      rooms.set(code, room);
      ws.send(JSON.stringify({ type: "hosted", roomCode: code, clientIndex: 0 }));
      console.log(`Room ${code} created (host: ${room.hostName}, public: ${room.isPublic})`);
      break;
    }

    case "join": {
      if (ws.data.room) {
        ws.send(JSON.stringify({ type: "error", message: "Already in a room" }));
        return;
      }
      const roomCode = (msg.roomCode || "").toUpperCase().trim();
      const room = rooms.get(roomCode);
      if (!room) {
        ws.send(JSON.stringify({ type: "error", message: "Room not found" }));
        return;
      }
      const index = room.addClient(ws);
      if (index < 0) {
        ws.send(JSON.stringify({ type: "error", message: "Room is full" }));
        return;
      }
      ws.send(JSON.stringify({ type: "joined", roomCode, clientIndex: index }));
      for (const [idx, client] of room.clients) {
        if (idx !== index && client.readyState === 1) {
          client.send(JSON.stringify({ type: "player_joined", clientIndex: index }));
        }
      }
      console.log(`Client ${index} joined room ${roomCode} (${room.clients.size}/${room.maxPlayers})`);
      break;
    }

    case "list": {
      const roomList: any[] = [];
      for (const [code, room] of rooms) {
        if (!room.isPublic) continue;
        if (room.isEmpty() || room.isHostGone()) continue;
        roomList.push({
          code,
          hostName: room.hostName,
          version: room.version,
          mode: room.mode,
          players: room.clients.size,
          maxPlayers: room.maxPlayers,
          description: room.description,
        });
      }
      for (const lobby of coopnetLobbies) {
        roomList.push(lobby);
      }
      ws.send(JSON.stringify({ type: "room_list", rooms: roomList }));
      break;
    }

    case "update_room": {
      if (!ws.data.room || ws.data.clientIndex !== 0) {
        ws.send(JSON.stringify({ type: "error", message: "Not a room host" }));
        return;
      }
      const room = ws.data.room;
      if (msg.hostName !== undefined) room.hostName = truncStr(msg.hostName, 64);
      if (msg.version !== undefined) room.version = truncStr(msg.version, 32);
      if (msg.mode !== undefined) room.mode = truncStr(msg.mode, 64);
      if (msg.description !== undefined) room.description = truncStr(msg.description, 512);
      if (msg.isPublic !== undefined) room.isPublic = !!msg.isPublic;
      if (msg.maxPlayers !== undefined) {
        room.maxPlayers = Math.min(
          Math.max(parseInt(msg.maxPlayers) || room.maxPlayers, 2),
          MAX_PLAYERS_PER_ROOM
        );
      }
      ws.send(JSON.stringify({ type: "room_updated" }));
      break;
    }

    default:
      ws.send(JSON.stringify({ type: "error", message: "Unknown message type" }));
  }
}

// --- Client Disconnect Handler ---
function handleDisconnect(ws: ServerWs): void {
  totalConnections--;
  const room = ws.data.room;
  if (!room) return;

  room.removeClient(ws);

  if (room.isHostGone()) {
    for (const [, client] of room.clients) {
      if (client.readyState === 1) {
        client.send(JSON.stringify({ type: "room_closed", reason: "Host disconnected" }));
        client.close(1000, "Host disconnected");
      }
    }
    rooms.delete(room.code);
    console.log(`Room ${room.code} closed (host left)`);
  } else if (room.isEmpty()) {
    rooms.delete(room.code);
    console.log(`Room ${room.code} closed (empty)`);
  } else {
    for (const [, client] of room.clients) {
      if (client.readyState === 1) {
        client.send(JSON.stringify({ type: "player_left", clientIndex: ws.data.clientIndex }));
      }
    }
    console.log(`Client ${ws.data.clientIndex} left room ${room.code}`);
  }
}

// --- Periodic Cleanup ---
setInterval(() => {
  const now = Date.now();
  const FOUR_HOURS = 4 * 60 * 60 * 1000;
  for (const [code, room] of rooms) {
    if (room.isEmpty() || (now - room.createdAt > FOUR_HOURS)) {
      for (const [, client] of room.clients) {
        if (client.readyState === 1) {
          client.close(1000, "Room expired");
        }
      }
      rooms.delete(code);
      console.log(`Room ${code} expired and cleaned up`);
    }
  }
}, 60_000);

// --- Bun Server ---
const server = Bun.serve({
  port: PORT,

  fetch(req, server) {
    const url = new URL(req.url);

    // Health check endpoint
    if (url.pathname === "/health" && req.method === "GET") {
      return Response.json({
        status: "ok",
        rooms: rooms.size,
        connections: totalConnections,
        uptime: process.uptime(),
      });
    }

    // WebSocket upgrade
    if (server.upgrade(req, {
      data: {
        clientIndex: undefined,
        room: null,
        rateLimitWindowStart: Date.now(),
        rateLimitCount: 0,
      } satisfies WsData,
    })) {
      return; // Upgraded successfully
    }

    return new Response("Not Found", { status: 404 });
  },

  websocket: {
    maxPayloadLength: 4096,
    idleTimeout: 120,

    open(ws: ServerWs) {
      totalConnections++;
      if (totalConnections > MAX_CONNECTIONS) {
        ws.close(1013, "Server is full");
        totalConnections--;
      }
    },

    message(ws: ServerWs, message: string | Buffer) {
      if (typeof message === "string") {
        handleControlMessage(ws, message);
        return;
      }

      // Binary game data relay
      const data = new Uint8Array(message as ArrayBuffer);
      if (!ws.data.room || ws.data.clientIndex === undefined) return;
      if (!checkRateLimit(ws)) return;
      if (data.length < 2) return;

      const targetIndex = data[0];
      const packetData = data.slice(1);

      if (targetIndex === 0xFF) {
        ws.data.room.broadcast(ws.data.clientIndex, packetData);
      } else {
        ws.data.room.sendTo(targetIndex, ws.data.clientIndex, packetData);
      }
    },

    close(ws: ServerWs) {
      handleDisconnect(ws);
    },

    error(ws: ServerWs, error: Error) {
      console.error("WebSocket error:", error.message);
    },
  },
});

console.log(`SM64CoopDX WebSocket Relay Server (Bun)`);
console.log(`  Port: ${PORT}`);
console.log(`  Max players per room: ${MAX_PLAYERS_PER_ROOM}`);
console.log(`  Max rooms: ${MAX_ROOMS}`);
console.log(`  Health check: http://localhost:${PORT}/health`);

export { server, rooms, Room };
