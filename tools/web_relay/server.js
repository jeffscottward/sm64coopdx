"use strict";

const http = require("http");
const { WebSocketServer } = require("ws");
const crypto = require("crypto");

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

class Room {
    constructor(code, host) {
        this.code = code;
        this.clients = new Map(); // clientIndex -> ws
        this.host = host;
        this.nextClientIndex = 1; // 0 is reserved for host
        this.createdAt = Date.now();
    }

    addClient(ws) {
        if (this.clients.size >= MAX_PLAYERS_PER_ROOM) {
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
            const room = new Room(code, ws);
            room.addClient(ws);
            rooms.set(code, room);
            ws.send(JSON.stringify({
                type: "hosted",
                roomCode: code,
                clientIndex: 0,
            }));
            console.log(`Room ${code} created`);
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
            // For debugging: list rooms (only in development)
            if (process.env.NODE_ENV === "production") {
                ws.send(JSON.stringify({ type: "error", message: "Not available" }));
                return;
            }
            const roomList = [];
            for (const [code, room] of rooms) {
                roomList.push({
                    code,
                    players: room.clients.size,
                    maxPlayers: MAX_PLAYERS_PER_ROOM,
                    createdAt: room.createdAt,
                });
            }
            ws.send(JSON.stringify({ type: "room_list", rooms: roomList }));
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
