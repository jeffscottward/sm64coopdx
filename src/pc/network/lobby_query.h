#ifndef LOBBY_QUERY_H
#define LOBBY_QUERY_H

#include <stdint.h>

// Shared callback typedefs for lobby queries.
// Used by both CoopNet (native) and WebSocket (web) backends.
typedef void (*LobbyQueryCallbackPtr)(uint64_t lobbyId, uint64_t ownerId,
    uint16_t connections, uint16_t maxConnections,
    const char* game, const char* version,
    const char* hostName, const char* mode, const char* description);
typedef void (*LobbyQueryFinishCallbackPtr)(void);

#endif /* LOBBY_QUERY_H */
