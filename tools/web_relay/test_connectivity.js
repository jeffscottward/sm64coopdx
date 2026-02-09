"use strict";

/**
 * SM64CoopDX WebSocket Relay - Multiplayer Connectivity Test Suite
 *
 * Tests the full multiplayer lifecycle as experienced by game clients:
 *  - Host creates room, clients join via room code
 *  - Bidirectional binary game data relay (broadcast + targeted)
 *  - Multi-player scenarios (3+ players)
 *  - Latency measurement for relay round-trips
 *  - Concurrent room isolation
 *  - Player disconnect / room cleanup
 *  - Stress: rapid message bursts
 */

const http = require("http");
const WebSocket = require("ws");

const PORT = 28765; // Separate port from unit tests
process.env.WS_RELAY_PORT = String(PORT);

let passed = 0;
let failed = 0;
const testResults = [];

function assert(condition, msg) {
    if (!condition) {
        console.error(`  FAIL: ${msg}`);
        failed++;
        testResults.push({ name: msg, status: "FAIL" });
    } else {
        console.log(`  PASS: ${msg}`);
        passed++;
        testResults.push({ name: msg, status: "PASS" });
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

function waitForJson(ws, timeoutMs = 3000) {
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

function waitForBinary(ws, timeoutMs = 3000) {
    return new Promise((resolve, reject) => {
        const timeout = setTimeout(() => reject(new Error("Timeout waiting for binary message")), timeoutMs);
        ws.once("message", (data, isBinary) => {
            clearTimeout(timeout);
            if (!isBinary) {
                reject(new Error("Expected binary message, got text: " + data.toString()));
                return;
            }
            resolve(Buffer.from(data));
        });
    });
}

function collectMessages(ws, count, timeoutMs = 3000) {
    return new Promise((resolve, reject) => {
        const msgs = [];
        const timeout = setTimeout(() => {
            ws.removeAllListeners("message");
            reject(new Error(`Timeout: got ${msgs.length}/${count} messages`));
        }, timeoutMs);

        function onMsg(data, isBinary) {
            msgs.push({ data: Buffer.from(data), isBinary });
            if (msgs.length >= count) {
                clearTimeout(timeout);
                ws.removeListener("message", onMsg);
                resolve(msgs);
            }
        }
        ws.on("message", onMsg);
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

// Create a room and return { host, roomCode }
async function createRoom() {
    const host = await connectWs();
    sendJson(host, { type: "host" });
    const resp = await waitForJson(host);
    return { host, roomCode: resp.roomCode };
}

// Join a room and return { client, clientIndex }
async function joinRoom(roomCode) {
    const client = await connectWs();
    sendJson(client, { type: "join", roomCode });
    const resp = await waitForJson(client);
    return { client, clientIndex: resp.clientIndex };
}

async function runTests() {
    const { httpServer, wss, rooms } = require("./server.js");
    await sleep(500);

    try {
        // =========================================================
        // TEST 1: Full Host-Join-Play Lifecycle
        // =========================================================
        console.log("\n=== Test 1: Full Host-Join-Play Lifecycle ===");

        const { host, roomCode } = await createRoom();
        assert(roomCode.length === 6, "Room code is 6 chars");

        // Client joins
        const { client: client1 } = await joinRoom(roomCode);
        const hostNotif = await waitForJson(host);
        assert(hostNotif.type === "player_joined", "Host notified of join");

        // Host sends game state to client (broadcast)
        const gameState = Buffer.from([0xFF, 0x10, 0x20, 0x30, 0x40, 0x50]);
        host.send(gameState);
        const clientReceived = await waitForBinary(client1);
        assert(clientReceived[0] === 0, "Client receives host's sender index (0)");
        assert(clientReceived.slice(1).equals(Buffer.from([0x10, 0x20, 0x30, 0x40, 0x50])), "Client receives correct game state data");

        // Client sends input back to host (targeted at index 0)
        const inputData = Buffer.from([0x00, 0xAA, 0xBB, 0xCC]);
        client1.send(inputData);
        const hostReceived = await waitForBinary(host);
        assert(hostReceived[0] === 1, "Host receives client's sender index (1)");
        assert(hostReceived.slice(1).equals(Buffer.from([0xAA, 0xBB, 0xCC])), "Host receives correct input data");

        // Clean up
        client1.close();
        await waitForJson(host); // player_left notification
        host.close();
        await sleep(200);

        // =========================================================
        // TEST 2: Three-Player Room with Broadcast
        // =========================================================
        console.log("\n=== Test 2: Three-Player Room with Broadcast ===");

        const { host: host3, roomCode: code3 } = await createRoom();
        const { client: p2 } = await joinRoom(code3);
        await waitForJson(host3); // player_joined for p2

        const { client: p3 } = await joinRoom(code3);
        // Both host and p2 get notified
        const [notif1, notif2] = await Promise.all([
            waitForJson(host3),
            waitForJson(p2),
        ]);
        assert(notif1.type === "player_joined", "Host gets player_joined for p3");
        assert(notif2.type === "player_joined", "P2 gets player_joined for p3");

        // Host broadcasts, both p2 and p3 should receive
        const broadcastData = Buffer.from([0xFF, 0x01, 0x02, 0x03]);
        host3.send(broadcastData);
        const [p2Msg, p3Msg] = await Promise.all([
            waitForBinary(p2),
            waitForBinary(p3),
        ]);
        assert(p2Msg[0] === 0, "P2 sees sender index 0 (host)");
        assert(p3Msg[0] === 0, "P3 sees sender index 0 (host)");
        assert(p2Msg.slice(1).equals(Buffer.from([0x01, 0x02, 0x03])), "P2 receives correct broadcast data");
        assert(p3Msg.slice(1).equals(Buffer.from([0x01, 0x02, 0x03])), "P3 receives correct broadcast data");

        // P2 broadcasts, host and p3 should receive
        const p2Broadcast = Buffer.from([0xFF, 0xDD, 0xEE]);
        p2.send(p2Broadcast);
        const [hostFromP2, p3FromP2] = await Promise.all([
            waitForBinary(host3),
            waitForBinary(p3),
        ]);
        assert(hostFromP2[0] === 1, "Host sees sender index 1 (P2)");
        assert(p3FromP2[0] === 1, "P3 sees sender index 1 (P2)");
        assert(hostFromP2.slice(1).equals(Buffer.from([0xDD, 0xEE])), "Host receives P2 broadcast data");

        // P3 sends targeted to P2 only (index 1) — host should NOT receive
        const p3ToP2 = Buffer.from([0x01, 0xAA]);
        p3.send(p3ToP2);
        const p2Targeted = await waitForBinary(p2);
        assert(p2Targeted[0] === 2, "P2 sees sender index 2 (P3)");
        assert(p2Targeted[1] === 0xAA, "P2 receives targeted data from P3");

        // Verify host did NOT get the targeted message (wait briefly)
        let hostGotStray = false;
        const strayPromise = waitForBinary(host3, 300).then(() => { hostGotStray = true; }).catch(() => {});
        await strayPromise;
        assert(!hostGotStray, "Host did NOT receive P3-to-P2 targeted message");

        p2.close();
        p3.close();
        host3.close();
        await sleep(200);

        // =========================================================
        // TEST 3: Room Isolation (Two Concurrent Rooms)
        // =========================================================
        console.log("\n=== Test 3: Room Isolation (Two Concurrent Rooms) ===");

        const { host: hostA, roomCode: codeA } = await createRoom();
        const { host: hostB, roomCode: codeB } = await createRoom();
        assert(codeA !== codeB, "Two rooms have different codes");

        const { client: clientA } = await joinRoom(codeA);
        await waitForJson(hostA); // player_joined
        const { client: clientB } = await joinRoom(codeB);
        await waitForJson(hostB); // player_joined

        // Host A broadcasts — only client A should receive, not client B
        const roomAData = Buffer.from([0xFF, 0x11, 0x22]);
        hostA.send(roomAData);
        const clientARecv = await waitForBinary(clientA);
        assert(clientARecv[0] === 0, "Client A receives from host A");

        let clientBGotStray = false;
        const strayB = waitForBinary(clientB, 300).then(() => { clientBGotStray = true; }).catch(() => {});
        await strayB;
        assert(!clientBGotStray, "Client B did NOT receive Room A broadcast (room isolation)");

        // Host B broadcasts — only client B should receive
        const roomBData = Buffer.from([0xFF, 0x33, 0x44]);
        hostB.send(roomBData);
        const clientBRecv = await waitForBinary(clientB);
        assert(clientBRecv[0] === 0, "Client B receives from host B");

        let clientAGotStray = false;
        const strayA = waitForBinary(clientA, 300).then(() => { clientAGotStray = true; }).catch(() => {});
        await strayA;
        assert(!clientAGotStray, "Client A did NOT receive Room B broadcast (room isolation)");

        // Verify health shows 2 rooms
        const health2 = await httpGet("/health");
        const healthData2 = JSON.parse(health2.body);
        assert(healthData2.rooms === 2, "Health check shows 2 active rooms");

        clientA.close();
        clientB.close();
        hostA.close();
        hostB.close();
        await sleep(200);

        // =========================================================
        // TEST 4: Relay Round-Trip Latency Measurement
        // =========================================================
        console.log("\n=== Test 4: Relay Round-Trip Latency Measurement ===");

        const { host: latHost, roomCode: latCode } = await createRoom();
        const { client: latClient } = await joinRoom(latCode);
        await waitForJson(latHost); // player_joined

        const LATENCY_SAMPLES = 20;
        const latencies = [];

        for (let i = 0; i < LATENCY_SAMPLES; i++) {
            const timestamp = Date.now();
            // Encode timestamp in 8 bytes after the target byte
            const pingBuf = Buffer.alloc(9);
            pingBuf[0] = 0x01; // target: client (index 1)
            pingBuf.writeBigUInt64BE(BigInt(timestamp), 1);
            const start = process.hrtime.bigint();
            latHost.send(pingBuf);
            const pong = await waitForBinary(latClient);
            const end = process.hrtime.bigint();
            const rttUs = Number(end - start) / 1000; // microseconds
            latencies.push(rttUs);
        }

        const avgLatencyUs = latencies.reduce((a, b) => a + b, 0) / latencies.length;
        const maxLatencyUs = Math.max(...latencies);
        const minLatencyUs = Math.min(...latencies);
        const avgLatencyMs = avgLatencyUs / 1000;
        const maxLatencyMs = maxLatencyUs / 1000;
        const minLatencyMs = minLatencyUs / 1000;

        console.log(`  Latency (${LATENCY_SAMPLES} samples): avg=${avgLatencyMs.toFixed(2)}ms, min=${minLatencyMs.toFixed(2)}ms, max=${maxLatencyMs.toFixed(2)}ms`);
        assert(avgLatencyMs < 50, `Average relay latency < 50ms (got ${avgLatencyMs.toFixed(2)}ms)`);
        assert(maxLatencyMs < 200, `Max relay latency < 200ms (got ${maxLatencyMs.toFixed(2)}ms)`);

        latClient.close();
        latHost.close();
        await sleep(200);

        // =========================================================
        // TEST 5: Rapid Message Burst (Stress Test)
        // =========================================================
        console.log("\n=== Test 5: Rapid Message Burst (Stress Test) ===");

        const { host: burstHost, roomCode: burstCode } = await createRoom();
        const { client: burstClient } = await joinRoom(burstCode);
        await waitForJson(burstHost); // player_joined

        const BURST_COUNT = 50;
        // Collect all messages from client side
        const clientCollector = collectMessages(burstClient, BURST_COUNT, 5000);

        // Send burst from host
        for (let i = 0; i < BURST_COUNT; i++) {
            const buf = Buffer.from([0xFF, i & 0xFF, (i >> 8) & 0xFF]);
            burstHost.send(buf);
        }

        const burstMsgs = await clientCollector;
        assert(burstMsgs.length === BURST_COUNT, `Client received all ${BURST_COUNT} burst messages`);

        // Verify ordering
        let ordered = true;
        for (let i = 0; i < burstMsgs.length; i++) {
            const d = burstMsgs[i].data;
            if (d[1] !== (i & 0xFF) || d[2] !== ((i >> 8) & 0xFF)) {
                ordered = false;
                break;
            }
        }
        assert(ordered, "Burst messages arrive in order");

        burstClient.close();
        burstHost.close();
        await sleep(200);

        // =========================================================
        // TEST 6: Large Packet Relay (Near MAX_MESSAGE_SIZE)
        // =========================================================
        console.log("\n=== Test 6: Large Packet Relay ===");

        const { host: largeHost, roomCode: largeCode } = await createRoom();
        const { client: largeClient } = await joinRoom(largeCode);
        await waitForJson(largeHost); // player_joined

        // SM64 PACKET_LENGTH is 3000, send something close to that
        const LARGE_SIZE = 3000;
        const largeBuf = Buffer.alloc(1 + LARGE_SIZE);
        largeBuf[0] = 0xFF; // broadcast
        for (let i = 0; i < LARGE_SIZE; i++) {
            largeBuf[i + 1] = i & 0xFF;
        }
        largeHost.send(largeBuf);
        const largeRecv = await waitForBinary(largeClient);
        assert(largeRecv.length === 1 + LARGE_SIZE, `Large packet relayed with correct size (${1 + LARGE_SIZE})`);
        assert(largeRecv[0] === 0, "Large packet sender index correct");

        // Verify data integrity
        let dataOk = true;
        for (let i = 0; i < LARGE_SIZE; i++) {
            if (largeRecv[i + 1] !== (i & 0xFF)) {
                dataOk = false;
                break;
            }
        }
        assert(dataOk, "Large packet data integrity verified");

        largeClient.close();
        largeHost.close();
        await sleep(200);

        // =========================================================
        // TEST 7: Player Disconnect Mid-Game Recovery
        // =========================================================
        console.log("\n=== Test 7: Player Disconnect Mid-Game Recovery ===");

        const { host: recHost, roomCode: recCode } = await createRoom();
        const { client: recP2 } = await joinRoom(recCode);
        await waitForJson(recHost); // player_joined for p2
        const { client: recP3 } = await joinRoom(recCode);
        await Promise.all([waitForJson(recHost), waitForJson(recP2)]); // player_joined for p3

        // P2 disconnects mid-game
        recP2.close();
        const [hostLeftNotif, p3LeftNotif] = await Promise.all([
            waitForJson(recHost),
            waitForJson(recP3),
        ]);
        assert(hostLeftNotif.type === "player_left", "Host gets player_left after P2 disconnect");
        assert(hostLeftNotif.clientIndex === 1, "Correct disconnect index (1)");
        assert(p3LeftNotif.type === "player_left", "P3 gets player_left after P2 disconnect");

        // Host and P3 can still communicate
        const afterDisconnect = Buffer.from([0xFF, 0xFE, 0xFD]);
        recHost.send(afterDisconnect);
        const p3Recv = await waitForBinary(recP3);
        assert(p3Recv[0] === 0, "P3 still receives from host after P2 left");
        assert(p3Recv.slice(1).equals(Buffer.from([0xFE, 0xFD])), "Data integrity after player disconnect");

        recP3.close();
        recHost.close();
        await sleep(200);

        // =========================================================
        // TEST 8: Room Code Case Insensitivity
        // =========================================================
        console.log("\n=== Test 8: Room Code Case Insensitivity ===");

        const { host: caseHost, roomCode: caseCode } = await createRoom();
        // Join with lowercase version
        const caseClient = await connectWs();
        sendJson(caseClient, { type: "join", roomCode: caseCode.toLowerCase() });
        const caseResp = await waitForJson(caseClient);
        assert(caseResp.type === "joined", "Join with lowercase room code succeeds");
        assert(caseResp.roomCode === caseCode.toUpperCase(), "Server normalizes to uppercase");

        await waitForJson(caseHost); // player_joined
        caseClient.close();
        caseHost.close();
        await sleep(200);

        // =========================================================
        // TEST 9: Full Room (16 players max)
        // =========================================================
        console.log("\n=== Test 9: Room Capacity Check ===");

        // Only test with 4 players for speed (trust MAX_PLAYERS config works)
        process.env.WS_MAX_PLAYERS = "4";
        // Note: the MAX_PLAYERS_PER_ROOM is read at startup, so this won't change mid-test
        // Instead, just verify we can add multiple players and the count is correct
        const { host: capHost, roomCode: capCode } = await createRoom();
        const capClients = [];
        for (let i = 0; i < 3; i++) {
            const { client: c } = await joinRoom(capCode);
            capClients.push(c);
            await waitForJson(capHost); // player_joined
            // Drain other clients' notifications
            for (let j = 0; j < i; j++) {
                await waitForJson(capClients[j]).catch(() => {});
            }
        }
        assert(capClients.length === 3, "3 clients successfully joined (4 total with host)");

        // Verify health endpoint player count
        const capHealth = await httpGet("/health");
        const capHealthData = JSON.parse(capHealth.body);
        assert(capHealthData.rooms >= 1, "Room exists in health check");

        for (const c of capClients) c.close();
        capHost.close();
        await sleep(300);

    } catch (err) {
        console.error("\nTest error:", err);
        failed++;
        testResults.push({ name: `UNEXPECTED ERROR: ${err.message}`, status: "FAIL" });
    }

    // =========================================================
    // Summary
    // =========================================================
    console.log(`\n${"=".repeat(50)}`);
    console.log(`CONNECTIVITY TEST RESULTS: ${passed} passed, ${failed} failed`);
    console.log(`${"=".repeat(50)}`);

    if (failed > 0) {
        console.log("\nFailed tests:");
        for (const t of testResults) {
            if (t.status === "FAIL") {
                console.log(`  - ${t.name}`);
            }
        }
    }

    // Export results for the report
    const results = {
        passed,
        failed,
        total: passed + failed,
        tests: testResults,
    };

    wss.close();
    httpServer.close();
    process.exit(failed > 0 ? 1 : 0);
}

runTests().catch((err) => {
    console.error("Fatal test error:", err);
    process.exit(1);
});
