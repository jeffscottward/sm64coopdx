/**
 * CoopNet Lobby Proxy
 *
 * Connects to the CoopNet server, queries public lobbies, and outputs them
 * as a JSON array to stdout. Used by the web relay server to bridge real
 * CoopNet lobbies to WebSocket clients.
 *
 * Usage: ./coopnet_proxy [host] [port]
 *   Default: net.coop64.us 34197
 *
 * Output: JSON array of lobby objects, one per line, followed by "END\n".
 * Each lobby line:
 *   {"lobbyId":123,"ownerId":456,"players":2,"maxPlayers":16,"game":"sm64coopdx","version":"v1.4.1","hostName":"Player","mode":"Normal","description":"..."}
 * Final line:
 *   END
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include "../../lib/coopnet/include/libcoopnet.h"

static volatile bool sDone = false;
static volatile bool sConnected = false;
static volatile bool sQueryStarted = false;
static int sLobbyCount = 0;

// JSON-escape a string into dest (handles " and \ and control chars)
static void json_escape(const char* src, char* dest, size_t destLen) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < destLen - 2; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            if (j + 2 >= destLen - 1) break;
            dest[j++] = '\\';
            dest[j++] = c;
        } else if (c < 0x20) {
            // Skip control characters
            continue;
        } else {
            dest[j++] = c;
        }
    }
    dest[j] = '\0';
}

static void on_connected(uint64_t userId) {
    (void)userId;
    sConnected = true;
}

static void on_disconnected(bool intentional) {
    (void)intentional;
    if (!sDone) {
        fprintf(stderr, "CoopNet disconnected%s\n", intentional ? " (intentional)" : "");
        sDone = true;
    }
}

static void on_lobby_list_got(uint64_t lobbyId, uint64_t ownerId,
    uint16_t connections, uint16_t maxConnections,
    const char* game, const char* version,
    const char* hostName, const char* mode, const char* description)
{
    char eGame[256], eVersion[256], eHostName[256], eMode[256], eDesc[2048];
    json_escape(game ? game : "", eGame, sizeof(eGame));
    json_escape(version ? version : "", eVersion, sizeof(eVersion));
    json_escape(hostName ? hostName : "", eHostName, sizeof(eHostName));
    json_escape(mode ? mode : "", eMode, sizeof(eMode));
    json_escape(description ? description : "", eDesc, sizeof(eDesc));

    printf("{\"lobbyId\":%llu,\"ownerId\":%llu,\"players\":%u,\"maxPlayers\":%u,"
           "\"game\":\"%s\",\"version\":\"%s\",\"hostName\":\"%s\","
           "\"mode\":\"%s\",\"description\":\"%s\"}\n",
           (unsigned long long)lobbyId, (unsigned long long)ownerId,
           connections, maxConnections,
           eGame, eVersion, eHostName, eMode, eDesc);
    fflush(stdout);
    sLobbyCount++;
}

static void on_lobby_list_finish(void) {
    printf("END\n");
    fflush(stdout);
    sDone = true;
}

static void on_error(enum MPacketErrorNumber errNum, uint64_t tag) {
    fprintf(stderr, "CoopNet error: %d (tag=%llu)\n", errNum, (unsigned long long)tag);
}

static void on_load_balance(const char* host, uint32_t port) {
    fprintf(stderr, "CoopNet load balance redirect: %s:%u\n", host, port);
}

static void signal_handler(int sig) {
    (void)sig;
    sDone = true;
}

int main(int argc, char* argv[]) {
    const char* host = "net.coop64.us";
    uint32_t port = 34197;

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = (uint32_t)atoi(argv[2]);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Set up callbacks
    memset(&gCoopNetCallbacks, 0, sizeof(gCoopNetCallbacks));
    gCoopNetCallbacks.OnConnected = on_connected;
    gCoopNetCallbacks.OnDisconnected = on_disconnected;
    gCoopNetCallbacks.OnLobbyListGot = on_lobby_list_got;
    gCoopNetCallbacks.OnLobbyListFinish = on_lobby_list_finish;
    gCoopNetCallbacks.OnError = on_error;
    gCoopNetCallbacks.OnLoadBalance = on_load_balance;

    fprintf(stderr, "Connecting to CoopNet at %s:%u...\n", host, port);

    if (coopnet_begin(host, port, "web-proxy", 0) != COOPNET_OK) {
        fprintf(stderr, "Failed to connect to CoopNet\n");
        return 1;
    }

    // Poll until connected or timeout (5 seconds)
    int timeout = 50; // 50 * 100ms = 5s
    while (!sConnected && !sDone && timeout > 0) {
        coopnet_update();
        usleep(100000); // 100ms
        timeout--;
    }

    if (!sConnected) {
        fprintf(stderr, "Timeout connecting to CoopNet\n");
        coopnet_shutdown();
        return 1;
    }

    fprintf(stderr, "Connected! Querying lobby list...\n");

    if (coopnet_lobby_list_get("sm64coopdx", "") != COOPNET_OK) {
        fprintf(stderr, "Failed to query lobby list\n");
        coopnet_shutdown();
        return 1;
    }
    sQueryStarted = true;

    // Poll until done or timeout (10 seconds)
    timeout = 100; // 100 * 100ms = 10s
    while (!sDone && timeout > 0) {
        coopnet_update();
        usleep(100000); // 100ms
        timeout--;
    }

    if (!sDone) {
        fprintf(stderr, "Timeout waiting for lobby list\n");
        printf("END\n");
        fflush(stdout);
    }

    fprintf(stderr, "Got %d lobbies\n", sLobbyCount);
    coopnet_shutdown();
    return 0;
}
