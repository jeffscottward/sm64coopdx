#ifndef COOPNET_H
#define COOPNET_H
#ifdef COOPNET

#include "pc/network/lobby_query.h"

// Backward-compatible aliases for existing CoopNet code
typedef LobbyQueryCallbackPtr QueryCallbackPtr;
typedef LobbyQueryFinishCallbackPtr QueryFinishCallbackPtr;

extern struct NetworkSystem gNetworkSystemCoopNet;
extern uint64_t gCoopNetDesiredLobby;
extern char gCoopNetPassword[];

bool ns_coopnet_query(QueryCallbackPtr callback, QueryFinishCallbackPtr finishCallback, const char* password);
bool ns_coopnet_is_connected(void);
void ns_coopnet_update(void);

#endif
#endif