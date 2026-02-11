#ifdef TARGET_WEB

#include "network_websocket.h"
#include "../socket/socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc/configfile.h"
#include "pc/debuglog.h"
#include "pc/djui/djui.h"
#include "pc/djui/djui_panel_join_message.h"
#include "pc/mods/mods.h"
#include "pc/network/version.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/websocket.h>
#include <emscripten/emscripten.h>
#endif

#define WS_MSG_QUEUE_SIZE 128
#define WS_MAX_MSG_SIZE (PACKET_LENGTH + 16)

// Room code assigned by the relay server after hosting
static char sRoomCode[32] = "";

// Local client index assigned by the relay server
static u8 sLocalClientIndex = 0;

// Client IDs for each player slot (assigned by relay)
static s64 sClientIds[MAX_PLAYERS] = { 0 };

// Whether the connection is established and ready
static bool sConnected = false;

// Whether we have successfully joined/hosted a room
static bool sInRoom = false;

// Whether we are waiting for a host/join response
static bool sWaitingForRoom = false;

// Pending room code for join command (sent after WebSocket opens)
static char sPendingJoinCode[32] = "";

// Whether to send a host command after WebSocket opens
static bool sPendingHost = false;

// Incoming message queue
struct WsMessage {
    u8 data[WS_MAX_MSG_SIZE];
    u16 dataLength;
    u8 clientIndex;
};

static struct WsMessage sMsgQueue[WS_MSG_QUEUE_SIZE];
static u16 sMsgQueueHead = 0;
static u16 sMsgQueueTail = 0;
static u16 sMsgQueueCount = 0;

// Lobby query: mapping lobbyId -> room code for join-by-click
#define WS_LOBBY_MAP_MAX 128
struct LobbyCodeEntry {
    uint64_t lobbyId;
    char code[8]; // 6-char room code + null
};
static struct LobbyCodeEntry sLobbyCodeMap[WS_LOBBY_MAP_MAX];
static int sLobbyCodeMapCount = 0;

// Escape a string for safe JSON embedding (handles " and \ characters)
static void ws_json_escape(const char* src, char* dest, size_t destLen) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < destLen - 2; i++) {
        if (src[i] == '"' || src[i] == '\\') {
            if (j + 2 >= destLen - 1) break;
            dest[j++] = '\\';
        }
        dest[j++] = src[i];
    }
    dest[j] = '\0';
}

// Generate a stable hash-based lobby ID from a room code string
static uint64_t ws_lobby_id_from_code(const char* code) {
    uint64_t hash = 5381;
    for (const char* p = code; *p; p++) {
        hash = ((hash << 5) + hash) + (uint64_t)(*p);
    }
    return hash;
}

#ifdef __EMSCRIPTEN__
static EMSCRIPTEN_WEBSOCKET_T sWebSocket = 0;

// Dedicated query WebSocket (separate from game connection)
static EMSCRIPTEN_WEBSOCKET_T sQueryWebSocket = 0;
static LobbyQueryCallbackPtr sQueryCallback = NULL;
static LobbyQueryFinishCallbackPtr sQueryFinishCallback = NULL;

// Simple JSON string value extractor (no dependency on a JSON library)
// Finds "key":"value" in a JSON string and copies value to dest
static bool ws_json_get_string(const char* json, const char* key, char* dest, size_t destLen) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* pos = strstr(json, search);
    if (!pos) { return false; }
    pos = strchr(pos + strlen(search), ':');
    if (!pos) { return false; }
    pos++;
    while (*pos == ' ' || *pos == '\t') { pos++; }
    if (*pos == '"') {
        pos++;
        size_t i = 0;
        while (*pos && *pos != '"' && i < destLen - 1) {
            dest[i++] = *pos++;
        }
        dest[i] = '\0';
        return true;
    }
    return false;
}

// Extract integer value for "key":123
static bool ws_json_get_int(const char* json, const char* key, int* dest) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* pos = strstr(json, search);
    if (!pos) { return false; }
    pos = strchr(pos + strlen(search), ':');
    if (!pos) { return false; }
    pos++;
    while (*pos == ' ' || *pos == '\t') { pos++; }
    if (*pos >= '0' && *pos <= '9') {
        *dest = atoi(pos);
        return true;
    }
    return false;
}

static void ws_handle_control_message(const char* json) {
    char msgType[32] = "";
    if (!ws_json_get_string(json, "type", msgType, sizeof(msgType))) {
        LOG_ERROR("WebSocket: could not parse control message type");
        return;
    }

    if (strcmp(msgType, "hosted") == 0) {
        // Room created successfully — extract room code and client index
        ws_json_get_string(json, "roomCode", sRoomCode, sizeof(sRoomCode));
        int idx = 0;
        if (ws_json_get_int(json, "clientIndex", &idx)) {
            sLocalClientIndex = (u8)idx;
        }
        sInRoom = true;
        sWaitingForRoom = false;
        LOG_INFO("WebSocket: hosted room %s (clientIndex=%d)", sRoomCode, sLocalClientIndex);

    } else if (strcmp(msgType, "joined") == 0) {
        // Joined room successfully
        ws_json_get_string(json, "roomCode", sRoomCode, sizeof(sRoomCode));
        int idx = 0;
        if (ws_json_get_int(json, "clientIndex", &idx)) {
            sLocalClientIndex = (u8)idx;
        }
        sInRoom = true;
        sWaitingForRoom = false;
        LOG_INFO("WebSocket: joined room %s (clientIndex=%d)", sRoomCode, sLocalClientIndex);

    } else if (strcmp(msgType, "player_joined") == 0) {
        int idx = 0;
        if (ws_json_get_int(json, "clientIndex", &idx)) {
            LOG_INFO("WebSocket: player joined (clientIndex=%d)", idx);
        }

    } else if (strcmp(msgType, "player_left") == 0) {
        int idx = 0;
        if (ws_json_get_int(json, "clientIndex", &idx)) {
            LOG_INFO("WebSocket: player left (clientIndex=%d)", idx);
        }

    } else if (strcmp(msgType, "room_closed") == 0) {
        char reason[64] = "";
        ws_json_get_string(json, "reason", reason, sizeof(reason));
        LOG_INFO("WebSocket: room closed (%s)", reason);
        sInRoom = false;
        memset(sRoomCode, 0, sizeof(sRoomCode));

    } else if (strcmp(msgType, "error") == 0) {
        char errorMsg[128] = "";
        ws_json_get_string(json, "message", errorMsg, sizeof(errorMsg));
        LOG_ERROR("WebSocket relay error: %s", errorMsg);
        sWaitingForRoom = false;
        // Show error in join message panel
        char displayMsg[192];
        snprintf(displayMsg, sizeof(displayMsg), "Relay error: %s", errorMsg);
        djui_panel_join_message_error(displayMsg);

    } else {
        LOG_INFO("WebSocket: unknown control message type '%s'", msgType);
    }
}

static EM_BOOL ws_on_open(int eventType, const EmscriptenWebSocketOpenEvent* wsEvent, void* userData) {
    (void)eventType; (void)wsEvent; (void)userData;
    sConnected = true;
    LOG_INFO("WebSocket connection opened");

    // If we have a pending host or join command, send it now
    if (sPendingHost) {
        sPendingHost = false;
        ns_websocket_send_host_command();
    } else if (sPendingJoinCode[0] != '\0') {
        char code[32];
        snprintf(code, sizeof(code), "%s", sPendingJoinCode);
        memset(sPendingJoinCode, 0, sizeof(sPendingJoinCode));
        ns_websocket_send_join_command(code);
    }

    return EM_TRUE;
}

static EM_BOOL ws_on_message(int eventType, const EmscriptenWebSocketMessageEvent* wsEvent, void* userData) {
    (void)eventType; (void)userData;

    // Text messages are JSON control messages from the relay
    if (wsEvent->isText && wsEvent->numBytes > 0) {
        ws_handle_control_message((const char*)wsEvent->data);
        return EM_TRUE;
    }

    // Binary messages are game data
    if (!wsEvent->isText && wsEvent->numBytes > 0) {
        if (sMsgQueueCount >= WS_MSG_QUEUE_SIZE) {
            LOG_ERROR("WebSocket message queue full, dropping packet");
            return EM_TRUE;
        }

        struct WsMessage* msg = &sMsgQueue[sMsgQueueHead];

        // First byte is the client index from the relay
        msg->clientIndex = wsEvent->data[0];

        // Remaining bytes are the game packet
        u16 packetLen = (u16)(wsEvent->numBytes - 1);
        if (packetLen > WS_MAX_MSG_SIZE) {
            packetLen = WS_MAX_MSG_SIZE;
        }
        memcpy(msg->data, wsEvent->data + 1, packetLen);
        msg->dataLength = packetLen;

        sMsgQueueHead = (sMsgQueueHead + 1) % WS_MSG_QUEUE_SIZE;
        sMsgQueueCount++;
    }

    return EM_TRUE;
}

static EM_BOOL ws_on_error(int eventType, const EmscriptenWebSocketErrorEvent* wsEvent, void* userData) {
    (void)eventType; (void)wsEvent; (void)userData;
    LOG_ERROR("WebSocket error");
    sConnected = false;
    return EM_TRUE;
}

static EM_BOOL ws_on_close(int eventType, const EmscriptenWebSocketCloseEvent* wsEvent, void* userData) {
    (void)eventType; (void)wsEvent; (void)userData;
    LOG_INFO("WebSocket connection closed");
    sConnected = false;
    sInRoom = false;
    sWebSocket = 0;
    return EM_TRUE;
}

// --- Lobby query WebSocket callbacks ---

static void ws_query_handle_room_list(const char* json) {
    // Find the "rooms" array in the JSON
    const char* roomsStart = strstr(json, "\"rooms\"");
    if (!roomsStart) {
        LOG_ERROR("WebSocket query: no rooms array in response");
        if (sQueryFinishCallback) sQueryFinishCallback();
        return;
    }
    const char* arrStart = strchr(roomsStart, '[');
    if (!arrStart) {
        if (sQueryFinishCallback) sQueryFinishCallback();
        return;
    }
    arrStart++;

    // Iterate over each {...} object in the array
    const char* p = arrStart;
    while (*p) {
        // Skip whitespace and commas
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (*p == ']' || *p == '\0') break;
        if (*p != '{') { p++; continue; }

        // Find matching closing brace
        const char* objStart = p;
        int depth = 0;
        const char* objEnd = NULL;
        for (const char* q = p; *q; q++) {
            if (*q == '{') depth++;
            else if (*q == '}') { depth--; if (depth == 0) { objEnd = q; break; } }
        }
        if (!objEnd) break;

        // Copy object to a temp buffer for parsing
        size_t objLen = (size_t)(objEnd - objStart + 1);
        char objBuf[2048];
        if (objLen >= sizeof(objBuf)) objLen = sizeof(objBuf) - 1;
        memcpy(objBuf, objStart, objLen);
        objBuf[objLen] = '\0';

        // Extract fields
        char code[8] = "";
        char hostName[65] = "";
        char version[33] = "";
        char mode[65] = "";
        char description[513] = "";
        int players = 0;
        int maxPlayers = 0;

        ws_json_get_string(objBuf, "code", code, sizeof(code));
        ws_json_get_string(objBuf, "hostName", hostName, sizeof(hostName));
        ws_json_get_string(objBuf, "version", version, sizeof(version));
        ws_json_get_string(objBuf, "mode", mode, sizeof(mode));
        ws_json_get_string(objBuf, "description", description, sizeof(description));
        ws_json_get_int(objBuf, "players", &players);
        ws_json_get_int(objBuf, "maxPlayers", &maxPlayers);

        // Generate a lobby ID from the room code and store the mapping
        uint64_t lobbyId = ws_lobby_id_from_code(code);
        if (sLobbyCodeMapCount < WS_LOBBY_MAP_MAX) {
            sLobbyCodeMap[sLobbyCodeMapCount].lobbyId = lobbyId;
            snprintf(sLobbyCodeMap[sLobbyCodeMapCount].code, sizeof(sLobbyCodeMap[0].code), "%s", code);
            sLobbyCodeMapCount++;
        }

        // Call the UI callback
        if (sQueryCallback) {
            sQueryCallback(lobbyId, 0,
                (uint16_t)players, (uint16_t)maxPlayers,
                GAME_NAME, version, hostName, mode, description);
        }

        p = objEnd + 1;
    }

    if (sQueryFinishCallback) sQueryFinishCallback();
}

static EM_BOOL ws_query_on_open(int eventType, const EmscriptenWebSocketOpenEvent* wsEvent, void* userData) {
    (void)eventType; (void)wsEvent; (void)userData;
    LOG_INFO("WebSocket query: connection opened, sending list request");
    emscripten_websocket_send_utf8_text(sQueryWebSocket, "{\"type\":\"list\"}");
    return EM_TRUE;
}

static EM_BOOL ws_query_on_message(int eventType, const EmscriptenWebSocketMessageEvent* wsEvent, void* userData) {
    (void)eventType; (void)userData;
    if (wsEvent->isText && wsEvent->numBytes > 0) {
        const char* json = (const char*)wsEvent->data;
        char msgType[32] = "";
        ws_json_get_string(json, "type", msgType, sizeof(msgType));
        if (strcmp(msgType, "room_list") == 0) {
            ws_query_handle_room_list(json);
        } else if (strcmp(msgType, "error") == 0) {
            char errorMsg[128] = "";
            ws_json_get_string(json, "message", errorMsg, sizeof(errorMsg));
            LOG_ERROR("WebSocket query error: %s", errorMsg);
            if (sQueryFinishCallback) sQueryFinishCallback();
        }
    }

    // Close the query WebSocket after we get a response
    if (sQueryWebSocket > 0) {
        emscripten_websocket_close(sQueryWebSocket, 1000, "query done");
        emscripten_websocket_delete(sQueryWebSocket);
        sQueryWebSocket = 0;
    }
    sQueryCallback = NULL;
    sQueryFinishCallback = NULL;
    return EM_TRUE;
}

static EM_BOOL ws_query_on_error(int eventType, const EmscriptenWebSocketErrorEvent* wsEvent, void* userData) {
    (void)eventType; (void)wsEvent; (void)userData;
    LOG_ERROR("WebSocket query: connection error");
    if (sQueryFinishCallback) sQueryFinishCallback();
    sQueryCallback = NULL;
    sQueryFinishCallback = NULL;
    if (sQueryWebSocket > 0) {
        emscripten_websocket_delete(sQueryWebSocket);
        sQueryWebSocket = 0;
    }
    return EM_TRUE;
}

static EM_BOOL ws_query_on_close(int eventType, const EmscriptenWebSocketCloseEvent* wsEvent, void* userData) {
    (void)eventType; (void)wsEvent; (void)userData;
    LOG_INFO("WebSocket query: connection closed");
    sQueryWebSocket = 0;
    return EM_TRUE;
}
#endif /* __EMSCRIPTEN__ */

void ns_websocket_send_host_command(void) {
#ifdef __EMSCRIPTEN__
    if (!sConnected || sWebSocket <= 0) {
        LOG_ERROR("WebSocket: cannot send host command, not connected");
        return;
    }

    // Get the active mod name (largest enabled mod, or "Super Mario 64" if none)
    char modeName[64] = "";
    mods_get_main_mod_name(modeName, sizeof(modeName));

    // Escape player name, version, and mode for safe JSON embedding
    char escapedName[130] = "";
    char escapedVersion[66] = "";
    char escapedMode[130] = "";
    ws_json_escape(configPlayerName, escapedName, sizeof(escapedName));
    ws_json_escape(get_version(), escapedVersion, sizeof(escapedVersion));
    ws_json_escape(modeName, escapedMode, sizeof(escapedMode));

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "{\"type\":\"host\",\"hostName\":\"%s\",\"version\":\"%s\","
        "\"mode\":\"%s\",\"maxPlayers\":%u,\"isPublic\":true}",
        escapedName, escapedVersion, escapedMode, configAmountOfPlayers);
    emscripten_websocket_send_utf8_text(sWebSocket, cmd);
    sWaitingForRoom = true;
    LOG_INFO("WebSocket: sent host command with metadata (mode=%s)", modeName);
#endif
}

void ns_websocket_send_join_command(const char* roomCode) {
#ifdef __EMSCRIPTEN__
    if (!sConnected || sWebSocket <= 0) {
        LOG_ERROR("WebSocket: cannot send join command, not connected");
        return;
    }
    // Validate room code: only alphanumeric characters allowed (prevents JSON injection)
    for (const char* p = roomCode; *p; p++) {
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))) {
            LOG_ERROR("WebSocket: invalid room code character '%c'", *p);
            return;
        }
    }
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "{\"type\":\"join\",\"roomCode\":\"%s\"}", roomCode);
    emscripten_websocket_send_utf8_text(sWebSocket, cmd);
    sWaitingForRoom = true;
    LOG_INFO("WebSocket: sent join command for room %s", roomCode);
#endif
}

const char* ns_websocket_get_room_code(void) {
    return sRoomCode;
}

bool ns_websocket_is_connected(void) {
    return sConnected;
}

void ns_websocket_set_pending_join(const char* roomCode) {
    snprintf(sPendingJoinCode, sizeof(sPendingJoinCode), "%s", roomCode);
    sPendingHost = false;
}

bool ns_websocket_query(LobbyQueryCallbackPtr callback, LobbyQueryFinishCallbackPtr finishCallback) {
#ifdef __EMSCRIPTEN__
    // Clear lobby code map for fresh query
    sLobbyCodeMapCount = 0;
    memset(sLobbyCodeMap, 0, sizeof(sLobbyCodeMap));

    sQueryCallback = callback;
    sQueryFinishCallback = finishCallback;

    // Close any existing query WebSocket
    if (sQueryWebSocket > 0) {
        emscripten_websocket_close(sQueryWebSocket, 1000, "new query");
        emscripten_websocket_delete(sQueryWebSocket);
        sQueryWebSocket = 0;
    }

    // Build relay URL (same logic as game connection)
    const char* relayUrl = configWebSocketRelay;
#ifdef SM64_WS_RELAY_URL
    relayUrl = SM64_WS_RELAY_URL;
#endif
    if (relayUrl == NULL || relayUrl[0] == '\0') {
        relayUrl = "ws://localhost:8765";
    }

    EmscriptenWebSocketCreateAttributes attrs = {
        .url = relayUrl,
        .protocols = NULL,
        .createOnMainThread = EM_TRUE,
    };

    sQueryWebSocket = emscripten_websocket_new(&attrs);
    if (sQueryWebSocket <= 0) {
        LOG_ERROR("WebSocket query: failed to create connection to %s", relayUrl);
        if (finishCallback) finishCallback();
        return false;
    }

    emscripten_websocket_set_onopen_callback(sQueryWebSocket, NULL, ws_query_on_open);
    emscripten_websocket_set_onmessage_callback(sQueryWebSocket, NULL, ws_query_on_message);
    emscripten_websocket_set_onerror_callback(sQueryWebSocket, NULL, ws_query_on_error);
    emscripten_websocket_set_onclose_callback(sQueryWebSocket, NULL, ws_query_on_close);

    LOG_INFO("WebSocket query: connecting to %s", relayUrl);
    return true;
#else
    (void)callback; (void)finishCallback;
    return false;
#endif
}

const char* ns_websocket_get_lobby_code(uint64_t lobbyId) {
    for (int i = 0; i < sLobbyCodeMapCount; i++) {
        if (sLobbyCodeMap[i].lobbyId == lobbyId) {
            return sLobbyCodeMap[i].code;
        }
    }
    return NULL;
}

static bool ns_websocket_initialize(enum NetworkType networkType, UNUSED bool reconnecting) {
#ifdef __EMSCRIPTEN__
    // Reset state
    sConnected = false;
    sInRoom = false;
    sWaitingForRoom = false;
    sMsgQueueHead = 0;
    sMsgQueueTail = 0;
    sMsgQueueCount = 0;
    sLocalClientIndex = 0;
    memset(sRoomCode, 0, sizeof(sRoomCode));
    memset(sClientIds, 0, sizeof(sClientIds));
    memset(sPendingJoinCode, 0, sizeof(sPendingJoinCode));
    sPendingHost = false;

    // Build the WebSocket URL from config
    // Priority: SM64_WS_RELAY_URL (build-time define) > configWebSocketRelay (user setting)
    const char* relayUrl = configWebSocketRelay;
#ifdef SM64_WS_RELAY_URL
    // Build-time override: baked in via SM64_WS_RELAY make variable
    relayUrl = SM64_WS_RELAY_URL;
#endif
    if (relayUrl == NULL || relayUrl[0] == '\0') {
        relayUrl = "ws://localhost:8765";
    }

    EmscriptenWebSocketCreateAttributes attrs = {
        .url = relayUrl,
        .protocols = NULL,
        .createOnMainThread = EM_TRUE,
    };

    sWebSocket = emscripten_websocket_new(&attrs);
    if (sWebSocket <= 0) {
        LOG_ERROR("Failed to create WebSocket connection to %s", relayUrl);
        return false;
    }

    emscripten_websocket_set_onopen_callback(sWebSocket, NULL, ws_on_open);
    emscripten_websocket_set_onmessage_callback(sWebSocket, NULL, ws_on_message);
    emscripten_websocket_set_onerror_callback(sWebSocket, NULL, ws_on_error);
    emscripten_websocket_set_onclose_callback(sWebSocket, NULL, ws_on_close);

    LOG_INFO("WebSocket connecting to %s (networkType=%d)", relayUrl, networkType);

    // Queue the host/join command to be sent after the WebSocket opens
    if (networkType == NT_SERVER) {
        sPendingHost = true;
    }

    return true;
#else
    (void)networkType; (void)reconnecting;
    LOG_ERROR("WebSocket backend requires Emscripten");
    return false;
#endif
}

static s64 ns_websocket_get_id(u8 localIndex) {
    if (localIndex == 0) { return (s64)sLocalClientIndex; }
    if (localIndex < MAX_PLAYERS) { return sClientIds[localIndex]; }
    return 0;
}

static char* ns_websocket_get_id_str(u8 localIndex) {
    static char id_str[32] = { 0 };
    if (localIndex == UNKNOWN_LOCAL_INDEX) {
        snprintf(id_str, sizeof(id_str), "???");
    } else {
        snprintf(id_str, sizeof(id_str), "ws:%lld", (long long)ns_websocket_get_id(localIndex));
    }
    return id_str;
}

static void ns_websocket_save_id(u8 localIndex, s64 networkId) {
    SOFT_ASSERT(localIndex > 0);
    SOFT_ASSERT(localIndex < MAX_PLAYERS);
    sClientIds[localIndex] = (networkId == 0) ? sClientIds[0] : networkId;
    LOG_INFO("saved WebSocket id for localIndex %d: %lld", localIndex, (long long)sClientIds[localIndex]);
}

static void ns_websocket_clear_id(u8 localIndex) {
    if (localIndex == 0) { return; }
    SOFT_ASSERT(localIndex < MAX_PLAYERS);
    sClientIds[localIndex] = 0;
    LOG_INFO("cleared WebSocket id for localIndex %d", localIndex);
}

static void* ns_websocket_dup_addr(u8 localIndex) {
    if (localIndex >= MAX_PLAYERS) { return NULL; }
    void* address = malloc(sizeof(s64));
    if (!address) { return NULL; }
    memcpy(address, &sClientIds[localIndex], sizeof(s64));
    return address;
}

static bool ns_websocket_match_addr(void* addr1, void* addr2) {
    return !memcmp(addr1, addr2, sizeof(s64));
}

static void ns_websocket_update(void) {
    if (gNetworkType == NT_NONE) { return; }

    // Process queued messages
    while (sMsgQueueCount > 0) {
        struct WsMessage* msg = &sMsgQueue[sMsgQueueTail];

        // Map relay client index to local player index
        u8 localIndex = UNKNOWN_LOCAL_INDEX;
        for (int i = 1; i < MAX_PLAYERS; i++) {
            if (gNetworkPlayers[i].connected && sClientIds[i] == (s64)msg->clientIndex) {
                localIndex = i;
                break;
            }
        }

        network_receive(localIndex, &sClientIds[0], msg->data, msg->dataLength);

        sMsgQueueTail = (sMsgQueueTail + 1) % WS_MSG_QUEUE_SIZE;
        sMsgQueueCount--;
    }
}

static int ns_websocket_send(u8 localIndex, void* address, u8* data, u16 dataLength) {
#ifdef __EMSCRIPTEN__
    if (!sConnected || sWebSocket <= 0) {
        return SOCKET_ERROR;
    }

    if (localIndex != 0) {
        if (gNetworkType == NT_SERVER && gNetworkPlayers[localIndex].type != NPT_CLIENT) { return SOCKET_ERROR; }
        if (gNetworkType == NT_CLIENT && gNetworkPlayers[localIndex].type != NPT_SERVER) { return SOCKET_ERROR; }
    }

    // Prepend the target client index byte
    u8 targetIndex = (u8)(localIndex);
    if (localIndex == 0 && address != NULL) {
        targetIndex = (u8)(*(s64*)address);
    }

    u16 totalLen = 1 + dataLength;
    u8* buffer = malloc(totalLen);
    if (!buffer) { return SOCKET_ERROR; }

    buffer[0] = targetIndex;
    memcpy(buffer + 1, data, dataLength);

    EMSCRIPTEN_RESULT result = emscripten_websocket_send_binary(sWebSocket, buffer, totalLen);
    free(buffer);

    if (result != EMSCRIPTEN_RESULT_SUCCESS) {
        LOG_ERROR("WebSocket send failed: %d", result);
        return SOCKET_ERROR;
    }

    return NO_ERROR;
#else
    (void)localIndex; (void)address; (void)data; (void)dataLength;
    return SOCKET_ERROR;
#endif
}

static void ns_websocket_get_lobby_id(char* destination, u32 destLength) {
    if (sRoomCode[0] != '\0') {
        snprintf(destination, destLength, "%s", sRoomCode);
    } else {
        snprintf(destination, destLength, "%s", "");
    }
}

static void ns_websocket_get_lobby_secret(char* destination, u32 destLength) {
    if (sRoomCode[0] != '\0') {
        snprintf(destination, destLength, "ws:%s", sRoomCode);
    } else {
        snprintf(destination, destLength, "%s", "");
    }
}

static void ns_websocket_shutdown(UNUSED bool reconnecting) {
#ifdef __EMSCRIPTEN__
    if (sWebSocket > 0) {
        emscripten_websocket_close(sWebSocket, 1000, "shutdown");
        emscripten_websocket_delete(sWebSocket);
        sWebSocket = 0;
    }
#endif
    sConnected = false;
    sInRoom = false;
    sWaitingForRoom = false;
    sMsgQueueHead = 0;
    sMsgQueueTail = 0;
    sMsgQueueCount = 0;
    sLocalClientIndex = 0;
    memset(sRoomCode, 0, sizeof(sRoomCode));
    memset(sClientIds, 0, sizeof(sClientIds));
    memset(sPendingJoinCode, 0, sizeof(sPendingJoinCode));
    sPendingHost = false;
    LOG_INFO("WebSocket shutdown");
}

struct NetworkSystem gNetworkSystemWebSocket = {
    .initialize       = ns_websocket_initialize,
    .get_id           = ns_websocket_get_id,
    .get_id_str       = ns_websocket_get_id_str,
    .save_id          = ns_websocket_save_id,
    .clear_id         = ns_websocket_clear_id,
    .dup_addr         = ns_websocket_dup_addr,
    .match_addr       = ns_websocket_match_addr,
    .update           = ns_websocket_update,
    .send             = ns_websocket_send,
    .get_lobby_id     = ns_websocket_get_lobby_id,
    .get_lobby_secret = ns_websocket_get_lobby_secret,
    .shutdown         = ns_websocket_shutdown,
    .requireServerBroadcast = true,
    .name             = "WebSocket",
};

#endif /* TARGET_WEB */
