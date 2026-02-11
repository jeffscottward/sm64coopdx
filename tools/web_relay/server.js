"use strict";

const http = require("http");
const { WebSocketServer } = require("ws");
const crypto = require("crypto");
const { execFile } = require("child_process");
const path = require("path");

// Configuration from environment or defaults
const PORT = parseInt(process.env.WS_RELAY_PORT || "8765", 10);
const MAX_PLAYERS_PER_ROOM = parseInt(process.env.WS_MAX_PLAYERS || "16", 10);
const MAX_ROOMS = parseInt(process.env.WS_MAX_ROOMS || "100", 10);
const MAX_CONNECTIONS = parseInt(process.env.WS_MAX_CONNECTIONS || "1000", 10);
const RATE_LIMIT_WINDOW_MS = 1000;
const RATE_LIMIT_MAX_MESSAGES = 120; // ~4 packets per frame at 30Hz
const ROOM_CODE_LENGTH = 6;
const MAX_MESSAGE_SIZE = 4096; // PACKET_LENGTH(3000) + 16 + overhead

// Room management
const rooms = new Map();      // roomCode -> Room
let totalConnections = 0;

// Truncate a string to maxLen characters
function truncStr(str, maxLen) {
    if (typeof str !== "string") return "";
    return str.slice(0, maxLen);
}

class Room {
    constructor(code, host, meta) {
        this.code = code;
        this.clients = new Map(); // clientIndex -> ws
        this.host = host;
        this.nextClientIndex = 1; // 0 is reserved for host
        this.createdAt = Date.now();

        // Room metadata (from host command)
        this.hostName = truncStr(meta.hostName || "", 64);
        this.version = truncStr(meta.version || "", 32);
        this.mode = truncStr(meta.mode || "", 64);
        this.maxPlayers = Math.min(
            Math.max(parseInt(meta.maxPlayers) || MAX_PLAYERS_PER_ROOM, 2),
            MAX_PLAYERS_PER_ROOM
        );
        this.description = truncStr(meta.description || "", 512);
        this.isPublic = meta.isPublic !== false; // default true
    }

    addClient(ws) {
        if (this.clients.size >= this.maxPlayers) {
            return -1;
        }
        const index = this.host === ws ? 0 : this.nextClientIndex++;
        this.clients.set(index, ws);
        ws.clientIndex = index;
        ws.room = this;
        return index;
    }

    removeClient(ws) {
        if (ws.clientIndex !== undefined) {
            this.clients.delete(ws.clientIndex);
        }
    }

    isEmpty() {
        return this.clients.size === 0;
    }

    isHostGone() {
        return !this.clients.has(0);
    }

    broadcast(senderIndex, data) {
        // Prepend sender's client index and forward to all other clients
        const relayBuffer = Buffer.alloc(1 + data.length);
        relayBuffer[0] = senderIndex;
        data.copy(relayBuffer, 1);

        for (const [index, client] of this.clients) {
            if (index !== senderIndex && client.readyState === 1 /* OPEN */) {
                client.send(relayBuffer);
            }
        }
    }

    sendTo(targetIndex, senderIndex, data) {
        const target = this.clients.get(targetIndex);
        if (target && target.readyState === 1 /* OPEN */) {
            const relayBuffer = Buffer.alloc(1 + data.length);
            relayBuffer[0] = senderIndex;
            data.copy(relayBuffer, 1);
            target.send(relayBuffer);
        }
    }
}

function generateRoomCode() {
    const chars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no I,O,0,1 to avoid confusion
    let code;
    do {
        code = "";
        const bytes = crypto.randomBytes(ROOM_CODE_LENGTH);
        for (let i = 0; i < ROOM_CODE_LENGTH; i++) {
            code += chars[bytes[i] % chars.length];
        }
    } while (rooms.has(code));
    return code;
}

// Rate limiter per connection
function checkRateLimit(ws) {
    const now = Date.now();
    if (now - ws.rateLimitWindowStart > RATE_LIMIT_WINDOW_MS) {
        ws.rateLimitWindowStart = now;
        ws.rateLimitCount = 0;
    }
    ws.rateLimitCount++;
    return ws.rateLimitCount <= RATE_LIMIT_MAX_MESSAGES;
}

// --- CoopNet lobby proxy integration ---
const COOPNET_PROXY_PATH = path.join(__dirname, "coopnet_proxy");
const COOPNET_CACHE_TTL_MS = 30_000; // Refresh every 30 seconds
const COOPNET_HOST = process.env.COOPNET_HOST || "net.coop64.us";
const COOPNET_PORT = process.env.COOPNET_PORT || "34197";

let coopnetLobbies = [];       // Cached CoopNet lobby list
let coopnetLastFetch = 0;      // Timestamp of last successful fetch
let coopnetFetching = false;   // Prevent concurrent fetches

function fetchCoopNetLobbies() {
    if (coopnetFetching) return;

    // Check if the proxy binary exists
    const fs = require("fs");
    if (!fs.existsSync(COOPNET_PROXY_PATH)) {
        // Proxy not compiled — skip silently
        return;
    }

    coopnetFetching = true;
    const lobbies = [];
    const child = execFile(COOPNET_PROXY_PATH, [COOPNET_HOST, COOPNET_PORT], {
        timeout: 20_000,
        maxBuffer: 1024 * 1024,
        env: {
            ...process.env,
            DYLD_LIBRARY_PATH: path.join(__dirname, "../../lib/coopnet/mac_arm"),
            LD_LIBRARY_PATH: path.join(__dirname, "../../lib/coopnet/linux"),
        },
    }, (error, stdout, stderr) => {
        coopnetFetching = false;
        if (error) {
            console.error(`CoopNet proxy error: ${error.message}`);
            if (stderr) console.error(`  stderr: ${stderr.trim()}`);
            return;
        }

        const lines = stdout.split("\n");
        for (const line of lines) {
            const trimmed = line.trim();
            if (trimmed === "END" || trimmed === "") continue;
            try {
                const lobby = JSON.parse(trimmed);
                lobbies.push({
                    code: `CN-${lobby.lobbyId}`, // Prefix to distinguish from relay rooms
                    hostName: lobby.hostName || "",
                    version: lobby.version || "",
                    mode: lobby.mode || "",
                    players: lobby.players || 0,
                    maxPlayers: lobby.maxPlayers || 16,
                    description: lobby.description || "",
                    source: "coopnet",
                    lobbyId: lobby.lobbyId,
                });
            } catch {
                // Skip malformed lines
            }
        }

        coopnetLobbies = lobbies;
        coopnetLastFetch = Date.now();
        console.log(`CoopNet proxy: cached ${lobbies.length} lobbies`);
    });
}

// Refresh CoopNet lobbies periodically
setInterval(() => {
    fetchCoopNetLobbies();
}, COOPNET_CACHE_TTL_MS);

// Initial fetch on startup (after a short delay)
setTimeout(() => fetchCoopNetLobbies(), 2000);

// HTTP server for health check
const httpServer = http.createServer((req, res) => {
    if (req.url === "/health" && req.method === "GET") {
        const status = {
            status: "ok",
            rooms: rooms.size,
            connections: totalConnections,
            uptime: process.uptime(),
        };
        res.writeHead(200, { "Content-Type": "application/json" });
        res.end(JSON.stringify(status));
    } else {
        res.writeHead(404);
        res.end("Not Found");
    }
});

// WebSocket server
const wss = new WebSocketServer({
    server: httpServer,
    maxPayload: MAX_MESSAGE_SIZE,
});

wss.on("connection", (ws) => {
    totalConnections++;

    if (totalConnections > MAX_CONNECTIONS) {
        ws.close(1013, "Server is full");
        totalConnections--;
        return;
    }

    // Rate limit state
    ws.rateLimitWindowStart = Date.now();
    ws.rateLimitCount = 0;
    ws.clientIndex = undefined;
    ws.room = null;
    ws.authenticated = false; // Has sent a control message to join/host

    ws.on("message", (data, isBinary) => {
        // Control messages are text, game data is binary
        if (!isBinary) {
            handleControlMessage(ws, data.toString());
            return;
        }

        // Binary game data — relay to room
        if (!ws.room || ws.clientIndex === undefined) {
            return; // Not in a room yet
        }

        if (!checkRateLimit(ws)) {
            return; // Rate limited, silently drop
        }

        if (data.length < 2) {
            return; // Need at least target index + 1 byte of data
        }

        // First byte from client is the target client index
        const targetIndex = data[0];
        const packetData = data.slice(1);

        if (targetIndex === 0xFF) {
            // Broadcast to all others in room
            ws.room.broadcast(ws.clientIndex, packetData);
        } else {
            // Send to specific client
            ws.room.sendTo(targetIndex, ws.clientIndex, packetData);
        }
    });

    ws.on("close", () => {
        totalConnections--;
        if (ws.room) {
            ws.room.removeClient(ws);

            // If host left, notify remaining clients and close room
            if (ws.room.isHostGone()) {
                for (const [, client] of ws.room.clients) {
                    if (client.readyState === 1) {
                        client.send(JSON.stringify({
                            type: "room_closed",
                            reason: "Host disconnected",
                        }));
                        client.close(1000, "Host disconnected");
                    }
                }
                rooms.delete(ws.room.code);
                console.log(`Room ${ws.room.code} closed (host left)`);
            } else if (ws.room.isEmpty()) {
                rooms.delete(ws.room.code);
                console.log(`Room ${ws.room.code} closed (empty)`);
            } else {
                // Notify remaining players
                for (const [, client] of ws.room.clients) {
                    if (client.readyState === 1) {
                        client.send(JSON.stringify({
                            type: "player_left",
                            clientIndex: ws.clientIndex,
                        }));
                    }
                }
                console.log(`Client ${ws.clientIndex} left room ${ws.room.code}`);
            }
        }
    });

    ws.on("error", (err) => {
        console.error("WebSocket error:", err.message);
    });
});

function handleControlMessage(ws, text) {
    let msg;
    try {
        msg = JSON.parse(text);
    } catch {
        ws.send(JSON.stringify({ type: "error", message: "Invalid JSON" }));
        return;
    }

    switch (msg.type) {
        case "host": {
            if (ws.room) {
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
            ws.send(JSON.stringify({
                type: "hosted",
                roomCode: code,
                clientIndex: 0,
            }));
            console.log(`Room ${code} created (host: ${room.hostName}, public: ${room.isPublic})`);
            break;
        }

        case "join": {
            if (ws.room) {
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
            ws.send(JSON.stringify({
                type: "joined",
                roomCode: roomCode,
                clientIndex: index,
            }));
            // Notify other clients (including host) that a new player joined
            for (const [idx, client] of room.clients) {
                if (idx !== index && client.readyState === 1) {
                    client.send(JSON.stringify({
                        type: "player_joined",
                        clientIndex: index,
                    }));
                }
            }
            console.log(`Client ${index} joined room ${roomCode} (${room.clients.size}/${MAX_PLAYERS_PER_ROOM})`);
            break;
        }

        case "list": {
            const roomList = [];

            // Add local WebSocket relay rooms
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

            // Merge cached CoopNet lobbies (read-only, can't join via relay)
            for (const lobby of coopnetLobbies) {
                roomList.push(lobby);
            }

            ws.send(JSON.stringify({ type: "room_list", rooms: roomList }));
            break;
        }

        case "update_room": {
            if (!ws.room || ws.clientIndex !== 0) {
                ws.send(JSON.stringify({ type: "error", message: "Not a room host" }));
                return;
            }
            const room = ws.room;
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
            break;
    }
}

// Periodic cleanup of stale rooms (rooms older than 4 hours with no clients)
setInterval(() => {
    const now = Date.now();
    const FOUR_HOURS = 4 * 60 * 60 * 1000;
    for (const [code, room] of rooms) {
        if (room.isEmpty() || (now - room.createdAt > FOUR_HOURS)) {
            // Close any remaining connections
            for (const [, client] of room.clients) {
                if (client.readyState === 1) {
                    client.close(1000, "Room expired");
                }
            }
            rooms.delete(code);
            console.log(`Room ${code} expired and cleaned up`);
        }
    }
}, 60 * 1000); // Every minute

httpServer.listen(PORT, () => {
    console.log(`SM64CoopDX WebSocket Relay Server`);
    console.log(`  Port: ${PORT}`);
    console.log(`  Max players per room: ${MAX_PLAYERS_PER_ROOM}`);
    console.log(`  Max rooms: ${MAX_ROOMS}`);
    console.log(`  Health check: http://localhost:${PORT}/health`);
});

module.exports = { httpServer, wss, rooms, Room };
