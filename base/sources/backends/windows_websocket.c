#include <Windows.h>
#include <iron_websocket.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <winhttp.h>

#define MAX_SOCKETS 8
#define RELEASED    3 // Closed by the caller, the receive thread frees it

typedef struct message {
	char           *text;
	struct message *next;
} message_t;

// WinHTTP handles TLS, pings and pongs, the receive thread queues whole messages for the main thread
typedef struct {
	volatile LONG    state;
	volatile LONG    refs; // Main thread and receive thread
	wchar_t          url[2048];
	HINTERNET        hsession;
	HINTERNET        hconnect;
	HINTERNET        hrequest;
	HINTERNET        hws;
	CRITICAL_SECTION cs;
	message_t       *head;
	message_t       *tail;
	char            *last; // Handed out by receive
} websocket_t;

static websocket_t *sockets[MAX_SOCKETS];

static void websocket_release(websocket_t *ws) {
	if (InterlockedDecrement(&ws->refs) > 0) {
		return;
	}
	if (ws->hws != NULL) {
		WinHttpCloseHandle(ws->hws);
	}
	if (ws->hrequest != NULL) {
		WinHttpCloseHandle(ws->hrequest);
	}
	if (ws->hconnect != NULL) {
		WinHttpCloseHandle(ws->hconnect);
	}
	if (ws->hsession != NULL) {
		WinHttpCloseHandle(ws->hsession);
	}
	while (ws->head != NULL) {
		message_t *m = ws->head;
		ws->head     = m->next;
		free(m->text);
		free(m);
	}
	free(ws->last);
	DeleteCriticalSection(&ws->cs);
	free(ws);
}

static bool websocket_connect(websocket_t *ws) {
	bool     tls   = wcsncmp(ws->url, L"wss://", 6) == 0;
	wchar_t *host  = ws->url + (tls ? 6 : 5);
	wchar_t *slash = wcschr(host, L'/');
	wchar_t  path[2048];
	wcscpy(path, slash != NULL ? slash : L"/");
	if (slash != NULL) {
		*slash = L'\0';
	}
	INTERNET_PORT port  = tls ? 443 : 80;
	wchar_t      *colon = wcsrchr(host, L':');
	if (colon != NULL) {
		port   = (INTERNET_PORT)_wtoi(colon + 1);
		*colon = L'\0';
	}

	ws->hsession = WinHttpOpen(L"Iron/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	ws->hconnect = ws->hsession ? WinHttpConnect(ws->hsession, host, port, 0) : NULL;
	ws->hrequest = ws->hconnect ? WinHttpOpenRequest(ws->hconnect, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
	                                                 tls ? WINHTTP_FLAG_SECURE : 0)
	                            : NULL;
	if (ws->hrequest == NULL || !WinHttpSetOption(ws->hrequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0) ||
	    !WinHttpSendRequest(ws->hrequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) || !WinHttpReceiveResponse(ws->hrequest, NULL)) {
		return false;
	}
	ws->hws = WinHttpWebSocketCompleteUpgrade(ws->hrequest, 0);
	return ws->hws != NULL;
}

static DWORD WINAPI receive_thread(LPVOID arg) {
	websocket_t *ws = arg;
	bool         ok = websocket_connect(ws);
	if (InterlockedCompareExchange(&ws->state, ok ? IRON_WEBSOCKET_OPEN : IRON_WEBSOCKET_CLOSED, IRON_WEBSOCKET_CONNECTING) !=
	        IRON_WEBSOCKET_CONNECTING ||
	    !ok) {
		websocket_release(ws);
		return 0;
	}

	char *msg     = NULL;
	DWORD msg_len = 0;
	DWORD msg_cap = 0;
	while (true) {
		if (msg_cap - msg_len < 4096) {
			msg_cap = msg_cap * 2 + 4096;
			msg     = realloc(msg, msg_cap);
		}
		DWORD                          n    = 0;
		WINHTTP_WEB_SOCKET_BUFFER_TYPE type = 0;
		if (WinHttpWebSocketReceive(ws->hws, msg + msg_len, msg_cap - msg_len - 1, &n, &type) != NO_ERROR ||
		    type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
			break;
		}
		msg_len += n;
		if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
			msg[msg_len] = '\0';
			message_t *m = malloc(sizeof(message_t));
			m->text      = msg;
			m->next      = NULL;
			EnterCriticalSection(&ws->cs);
			if (ws->tail != NULL) {
				ws->tail->next = m;
			}
			else {
				ws->head = m;
			}
			ws->tail = m;
			LeaveCriticalSection(&ws->cs);
			msg     = NULL;
			msg_len = 0;
			msg_cap = 0;
		}
	}
	free(msg);
	InterlockedCompareExchange(&ws->state, IRON_WEBSOCKET_CLOSED, IRON_WEBSOCKET_OPEN);
	websocket_release(ws);
	return 0;
}

i32 iron_websocket_open(char *url) {
	if (strncmp(url, "wss://", 6) != 0 && strncmp(url, "ws://", 5) != 0) {
		return -1;
	}
	int slot = 0;
	while (slot < MAX_SOCKETS && sockets[slot] != NULL) {
		slot++;
	}
	if (slot == MAX_SOCKETS) {
		return -1;
	}
	websocket_t *ws = calloc(1, sizeof(websocket_t));
	ws->state       = IRON_WEBSOCKET_CONNECTING;
	ws->refs        = 2;
	MultiByteToWideChar(CP_UTF8, 0, url, -1, ws->url, 2048);
	InitializeCriticalSection(&ws->cs);
	HANDLE thread = CreateThread(NULL, 0, receive_thread, ws, 0, NULL);
	if (thread == NULL) {
		ws->refs = 1;
		websocket_release(ws);
		return -1;
	}
	CloseHandle(thread);
	sockets[slot] = ws;
	return slot;
}

static websocket_t *websocket_get(i32 handle) {
	return handle >= 0 && handle < MAX_SOCKETS ? sockets[handle] : NULL;
}

void iron_websocket_send(i32 handle, char *text) {
	websocket_t *ws = websocket_get(handle);
	if (ws != NULL && text != NULL && ws->state == IRON_WEBSOCKET_OPEN &&
	    WinHttpWebSocketSend(ws->hws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, text, (DWORD)strlen(text)) != NO_ERROR) {
		InterlockedCompareExchange(&ws->state, IRON_WEBSOCKET_CLOSED, IRON_WEBSOCKET_OPEN);
	}
}

char *iron_websocket_receive(i32 handle) {
	websocket_t *ws = websocket_get(handle);
	if (ws == NULL) {
		return NULL;
	}
	free(ws->last);
	ws->last = NULL;
	EnterCriticalSection(&ws->cs);
	message_t *m = ws->head;
	if (m != NULL) {
		ws->head = m->next;
		if (ws->head == NULL) {
			ws->tail = NULL;
		}
	}
	LeaveCriticalSection(&ws->cs);
	if (m != NULL) {
		ws->last = m->text;
		free(m);
	}
	return ws->last;
}

i32 iron_websocket_state(i32 handle) {
	websocket_t *ws = websocket_get(handle);
	return ws != NULL ? ws->state : IRON_WEBSOCKET_CLOSED;
}

void iron_websocket_close(i32 handle) {
	websocket_t *ws = websocket_get(handle);
	if (ws == NULL) {
		return;
	}
	sockets[handle] = NULL;
	if (InterlockedExchange(&ws->state, RELEASED) == IRON_WEBSOCKET_OPEN) {
		// The server answers the close frame, which ends the receive thread
		WinHttpWebSocketShutdown(ws->hws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
	}
	websocket_release(ws);
}
