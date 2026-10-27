#pragma once

#ifdef WITH_WEBSOCKET

#include "iron_global.h"

#define IRON_WEBSOCKET_CONNECTING 0
#define IRON_WEBSOCKET_OPEN       1
#define IRON_WEBSOCKET_CLOSED     2

i32   iron_websocket_open(char *url);          // Handle, -1 when no connection can be started
void  iron_websocket_send(i32 ws, char *text); // Dropped unless the connection is open
char *iron_websocket_receive(i32 ws);          // Next message or NULL, valid until the next receive on ws
i32   iron_websocket_state(i32 ws);
void  iron_websocket_close(i32 ws); // The handle is free afterwards

#endif
