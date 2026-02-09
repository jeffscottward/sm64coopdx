"use strict";

const http = require("http");
const WebSocket = require("ws");

const PORT = 18765; // Use a different port to avoid conflict
process.env.WS_RELAY_PORT = String(PORT);

let passed = 0;
let failed = 0;

function assert(condition, msg) {
    if (!condition) {
        console.error(`  FAIL: ${msg}`);
        failed++;
    } else {
        console.log(`  PASS: ${msg}`);
        passed++;
    }
}

function connectWs() {
    return new Promise((resolve, reject) => {
        const ws = new WebSocket(`ws://localhost:${PORT}`);
        ws.on("open", () => resolve(ws));
        ws.on("error", reject);
    });
}

function sendJson(ws, obj) {
    ws.send(JSON.stringify(obj));
}

function waitForJson(ws, timeoutMs = 2000) {
    return new Promise((resolve, reject) => {
        const timeout = setTimeout(() => reject(new Error("Timeout waiting for JSON message")), timeoutMs);
        ws.once("message", (data, isBinary) => {
            clearTimeout(timeout);
            if (isBinary) {
                reject(new Error("Expected text message, got binary"));
                return;
            }
            resolve(JSON.parse(data.toString()));
        });
    });
}

function waitForBinary(ws, timeoutMs = 2000) {
    return new Promise((resolve, reject) => {
        const timeout = setTimeout(() => reject(new Error("Timeout waiting for binary message")), timeoutMs);
        ws.once("message", (data, isBinary) => {
            clearTimeout(timeout);
            if (!isBinary) {
                // Could be a control message; try to parse and re-wait
                reject(new Error("Expected binary message, got text: " + data.toString()));
                return;
            }
            resolve(Buffer.from(data));
        });
    });
}

function httpGet(path) {
    return new Promise((resolve, reject) => {
        http.get(`http://localhost:${PORT}${path}`, (res) => {
            let body = "";
            res.on("data", (chunk) => body += chunk);
            res.on("end", () => resolve({ status: res.statusCode, body }));
        }).on("error", reject);
    });
}

async function sleep(ms) {
    return new Promise(r => setTimeout(r, ms));
}

async function runTests() {
    // Start the server
    const { httpServer, wss, rooms } = require("./server.js");

    // Wait for server to start
    await sleep(500);

    try {
        // --- Test 1: Health check ---
        console.log("\nTest 1: Health check endpoint");
        const health = await httpGet("/health");
        assert(health.status === 200, "Health check returns 200");
        const healthData = JSON.parse(health.body);
        assert(healthData.status === "ok", "Status is 'ok'");
        assert(typeof healthData.rooms === "number", "Rooms count is a number");
        assert(typeof healthData.uptime === "number", "Uptime is a number");

        // --- Test 2: 404 for unknown paths ---
        console.log("\nTest 2: Unknown path returns 404");
        const notFound = await httpGet("/nonexistent");
        assert(notFound.status === 404, "Unknown path returns 404");

        // --- Test 3: Host a room ---
        console.log("\nTest 3: Hosting a room");
        const host = await connectWs();
        sendJson(host, { type: "host" });
        const hostResp = await waitForJson(host);
        assert(hostResp.type === "hosted", "Response type is 'hosted'");
        assert(typeof hostResp.roomCode === "string", "Room code is a string");
        assert(hostResp.roomCode.length === 6, "Room code is 6 characters");
        assert(hostResp.clientIndex === 0, "Host gets client index 0");
        const roomCode = hostResp.roomCode;

        // --- Test 4: Join the room ---
        console.log("\nTest 4: Joining a room");
        const client1 = await connectWs();
        sendJson(client1, { type: "join", roomCode });
        const [joinResp, playerJoinedNotif] = await Promise.all([
            waitForJson(client1),
            waitForJson(host),
        ]);
        assert(joinResp.type === "joined", "Client gets 'joined' response");
        assert(joinResp.clientIndex === 1, "First client gets index 1");
        assert(joinResp.roomCode === roomCode, "Room code matches");
        assert(playerJoinedNotif.type === "player_joined", "Host notified of player join");
        assert(playerJoinedNotif.clientIndex === 1, "Notification has correct client index");

        // --- Test 5: Binary game data relay (broadcast) ---
        console.log("\nTest 5: Binary data relay (broadcast)");
        const gameData = Buffer.from([0xFF, 0x01, 0x02, 0x03, 0x04]); // 0xFF = broadcast, then 4 bytes of data
        host.send(gameData);
        const received = await waitForBinary(client1);
        assert(received[0] === 0, "Sender index is 0 (host)");
        assert(received[1] === 0x01, "Game data byte 1 matches");
        assert(received[2] === 0x02, "Game data byte 2 matches");
        assert(received[3] === 0x03, "Game data byte 3 matches");
        assert(received[4] === 0x04, "Game data byte 4 matches");
        assert(received.length === 5, "Relayed message has correct length (1 sender + 4 data)");

        // --- Test 6: Binary game data relay (targeted) ---
        console.log("\nTest 6: Binary data relay (targeted to host)");
        const targetedData = Buffer.from([0x00, 0xAA, 0xBB]); // target index 0 (host), 2 bytes data
        client1.send(targetedData);
        const targetReceived = await waitForBinary(host);
        assert(targetReceived[0] === 1, "Sender index is 1 (client)");
        assert(targetReceived[1] === 0xAA, "Targeted data byte 1 matches");
        assert(targetReceived[2] === 0xBB, "Targeted data byte 2 matches");

        // --- Test 7: Join non-existent room ---
        console.log("\nTest 7: Joining non-existent room");
        const badClient = await connectWs();
        sendJson(badClient, { type: "join", roomCode: "ZZZZZZ" });
        const badResp = await waitForJson(badClient);
        assert(badResp.type === "error", "Error response for bad room code");
        assert(badResp.message === "Room not found", "Correct error message");
        badClient.close();

        // --- Test 8: Double host attempt ---
        console.log("\nTest 8: Double host attempt");
        sendJson(host, { type: "host" });
        const doubleHostResp = await waitForJson(host);
        assert(doubleHostResp.type === "error", "Error for double host");
        assert(doubleHostResp.message === "Already in a room", "Correct error message");

        // --- Test 9: Second client joins ---
        console.log("\nTest 9: Second client joining");
        const client2 = await connectWs();
        sendJson(client2, { type: "join", roomCode });
        const [join2Resp] = await Promise.all([
            waitForJson(client2),
            waitForJson(host),  // host gets player_joined
            waitForJson(client1), // client1 gets player_joined
        ]);
        assert(join2Resp.type === "joined", "Second client gets 'joined'");
        assert(join2Resp.clientIndex === 2, "Second client gets index 2");

        // --- Test 10: Client disconnect notifies others ---
        console.log("\nTest 10: Client disconnect notification");
        // Set up listeners before triggering close
        const leftPromise1 = waitForJson(host);
        const leftPromise2 = waitForJson(client1);
        client2.close();
        const [leftNotif1, leftNotif2] = await Promise.all([leftPromise1, leftPromise2]);
        assert(leftNotif1.type === "player_left", "Host notified of player leave");
        assert(leftNotif1.clientIndex === 2, "Correct leaving client index (host notif)");
        assert(leftNotif2.type === "player_left", "Client1 notified of player leave");

        // --- Test 11: Host disconnect closes room ---
        console.log("\nTest 11: Host disconnect closes room");
        const roomClosedPromise = waitForJson(client1);
        host.close();
        const roomClosedNotif = await roomClosedPromise;
        assert(roomClosedNotif.type === "room_closed", "Client notified of room closure");
        assert(roomClosedNotif.reason === "Host disconnected", "Correct closure reason");
        client1.close();
        await sleep(200);
        assert(!rooms.has(roomCode), "Room is deleted after host disconnect");

        // --- Test 12: Invalid JSON ---
        console.log("\nTest 12: Invalid JSON handling");
        const badJson = await connectWs();
        badJson.send("not json at all");
        const badJsonResp = await waitForJson(badJson);
        assert(badJsonResp.type === "error", "Error for invalid JSON");
        assert(badJsonResp.message === "Invalid JSON", "Correct error message");
        badJson.close();

        // --- Test 13: Unknown message type ---
        console.log("\nTest 13: Unknown message type");
        const unknown = await connectWs();
        sendJson(unknown, { type: "banana" });
        const unknownResp = await waitForJson(unknown);
        assert(unknownResp.type === "error", "Error for unknown type");
        unknown.close();

        // --- Test 14: Health check shows active rooms ---
        console.log("\nTest 14: Health check with active room");
        const host2 = await connectWs();
        sendJson(host2, { type: "host" });
        await waitForJson(host2);
        const healthWithRoom = await httpGet("/health");
        const healthData2 = JSON.parse(healthWithRoom.body);
        assert(healthData2.rooms === 1, "Health check shows 1 active room");
        host2.close();
        await sleep(100);

    } catch (err) {
        console.error("Test error:", err);
        failed++;
    }

    // Summary
    console.log(`\n${"=".repeat(40)}`);
    console.log(`Results: ${passed} passed, ${failed} failed`);
    console.log(`${"=".repeat(40)}`);

    // Close server
    wss.close();
    httpServer.close();

    process.exit(failed > 0 ? 1 : 0);
}

runTests().catch((err) => {
    console.error("Fatal test error:", err);
    process.exit(1);
});
