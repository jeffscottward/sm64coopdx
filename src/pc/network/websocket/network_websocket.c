#ifdef TARGET_WEB

#include "network_websocket.h"
#include "../socket/socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc/configfile.h"
#include "pc/debuglog.h"
#include "pc/djui/djui.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/websocket.h>
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

#ifdef __EMSCRIPTEN__
static EMSCRIPTEN_WEBSOCKET_T sWebSocket = 0;

static EM_BOOL ws_on_open(int eventType, const EmscriptenWebSocketOpenEvent* wsEvent, void* userData) {
    (void)eventType; (void)wsEvent; (void)userData;
    sConnected = true;
    LOG_INFO("WebSocket connection opened");
    return EM_TRUE;
}

static EM_BOOL ws_on_message(int eventType, const EmscriptenWebSocketMessageEvent* wsEvent, void* userData) {
    (void)eventType; (void)userData;

    // Only handle binary messages
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
    sWebSocket = 0;
    return EM_TRUE;
}
#endif /* __EMSCRIPTEN__ */

static bool ns_websocket_initialize(enum NetworkType networkType, UNUSED bool reconnecting) {
#ifdef __EMSCRIPTEN__
    // Reset state
    sConnected = false;
    sMsgQueueHead = 0;
    sMsgQueueTail = 0;
    sMsgQueueCount = 0;
    sLocalClientIndex = 0;
    memset(sRoomCode, 0, sizeof(sRoomCode));
    memset(sClientIds, 0, sizeof(sClientIds));

    // Build the WebSocket URL from config
    // Default: ws://localhost:8765
    const char* relayUrl = configWebSocketRelay;
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

    if (networkType == NT_CLIENT) {
        djui_connect_menu_open();
        gNetworkType = NT_CLIENT;
        network_send_mod_list_request();
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
    void* address = malloc(sizeof(s64));
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
    sMsgQueueHead = 0;
    sMsgQueueTail = 0;
    sMsgQueueCount = 0;
    sLocalClientIndex = 0;
    memset(sRoomCode, 0, sizeof(sRoomCode));
    memset(sClientIds, 0, sizeof(sClientIds));
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
