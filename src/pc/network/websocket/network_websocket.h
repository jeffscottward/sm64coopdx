#ifndef NETWORK_WEBSOCKET_H
#define NETWORK_WEBSOCKET_H

#ifdef TARGET_WEB

#include "../network.h"

extern struct NetworkSystem gNetworkSystemWebSocket;

// Send a "host" command to the relay server to create a new room
void ns_websocket_send_host_command(void);

// Send a "join" command to the relay server with a room code
void ns_websocket_send_join_command(const char* roomCode);

// Get the current room code (empty string if not in a room)
const char* ns_websocket_get_room_code(void);

// Check if the WebSocket is connected to the relay
bool ns_websocket_is_connected(void);

// Set a pending join code to be sent after the WebSocket connection opens
// Must be called BEFORE network_init() since the connection is async
void ns_websocket_set_pending_join(const char* roomCode);

#endif /* TARGET_WEB */

#endif /* NETWORK_WEBSOCKET_H */
