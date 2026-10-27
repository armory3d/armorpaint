#include <errno.h>
#include <fcntl.h>
#include <iron_websocket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MAX_SOCKETS  8
#define PING_SECONDS 30
#define MAX_MESSAGE  (64 * 1024 * 1024)
#define RELEASED     3 // Closed by the caller while connecting, the connect thread frees it

// The connect thread owns the socket until it leaves CONNECTING, the main thread after
typedef struct {
	_Atomic int state;
	char       *host;
	char       *path;
	int         port;
	bool        tls;
	int         fd;
	SSL        *ssl;
	char       *rx;
	int         rx_len;
	int         rx_cap;
	char       *msg; // Message being put together, then handed out by receive
	int         msg_len;
	int         msg_cap;
	bool        msg_done;
	time_t      last_send;
} websocket_t;

static websocket_t *sockets[MAX_SOCKETS];
static SSL_CTX     *ctx = NULL;

// Bytes moved, 0 when the call would block, -1 when the connection is gone
static int io_read(websocket_t *ws, char *buf, int len) {
	int n = ws->ssl ? SSL_read(ws->ssl, buf, len) : (int)recv(ws->fd, buf, len, 0);
	if (n > 0) {
		return n;
	}
	if (ws->ssl) {
		int e = SSL_get_error(ws->ssl, n);
		return e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE ? 0 : -1;
	}
	return n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
}

static int io_write(websocket_t *ws, const char *buf, int len) {
	int n = ws->ssl ? SSL_write(ws->ssl, buf, len) : (int)send(ws->fd, buf, len, MSG_NOSIGNAL);
	if (n > 0) {
		return n;
	}
	if (ws->ssl) {
		int e = SSL_get_error(ws->ssl, n);
		return e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE ? 0 : -1;
	}
	return n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
}

static bool write_all(websocket_t *ws, const char *buf, int len) {
	while (len > 0) {
		int n = io_write(ws, buf, len);
		if (n < 0) {
			return false;
		}
		if (n == 0) {
			struct pollfd p = {ws->fd, POLLIN | POLLOUT, 0};
			if (poll(&p, 1, 5000) <= 0) {
				return false;
			}
		}
		buf += n;
		len -= n;
	}
	return true;
}

// Client frames are masked
static void send_frame(websocket_t *ws, int opcode, const char *data, int len) {
	unsigned char *p = malloc(14 + len);
	p[0]             = 0x80 | opcode;
	int i            = 2;
	if (len < 126) {
		p[1] = 0x80 | len;
	}
	else if (len <= 65535) {
		p[1] = 0x80 | 126;
		p[2] = (len >> 8) & 255;
		p[3] = len & 255;
		i    = 4;
	}
	else {
		p[1] = 0x80 | 127;
		for (int k = 0; k < 8; ++k) {
			p[2 + k] = k < 4 ? 0 : (len >> (8 * (7 - k))) & 255;
		}
		i = 10;
	}
	unsigned char *mask = p + i;
	RAND_bytes(mask, 4);
	i += 4;
	for (int k = 0; k < len; ++k) {
		p[i + k] = data[k] ^ mask[k & 3];
	}
	if (!write_all(ws, (char *)p, i + len)) {
		ws->state = IRON_WEBSOCKET_CLOSED;
	}
	ws->last_send = time(NULL);
	free(p);
}

static bool tcp_connect(websocket_t *ws) {
	struct addrinfo  hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM};
	struct addrinfo *res   = NULL;
	char             port[8];
	snprintf(port, sizeof(port), "%d", ws->port);
	if (getaddrinfo(ws->host, port, &hints, &res) != 0) {
		return false;
	}
	for (struct addrinfo *a = res; a != NULL && ws->fd < 0; a = a->ai_next) {
		ws->fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
		if (ws->fd >= 0 && connect(ws->fd, a->ai_addr, a->ai_addrlen) != 0) {
			close(ws->fd);
			ws->fd = -1;
		}
	}
	freeaddrinfo(res);
	if (ws->fd < 0) {
		return false;
	}
	int one = 1;
	setsockopt(ws->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	struct timeval timeout = {10, 0}; // For the handshake, the socket turns non-blocking after it
	setsockopt(ws->fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	setsockopt(ws->fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	if (!ws->tls) {
		return true;
	}
	ws->ssl = SSL_new(ctx);
	SSL_set_fd(ws->ssl, ws->fd);
	SSL_set_mode(ws->ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
	SSL_set_tlsext_host_name(ws->ssl, ws->host);
	SSL_set_verify(ws->ssl, SSL_VERIFY_PEER, NULL);
	SSL_set1_host(ws->ssl, ws->host);
	return SSL_connect(ws->ssl) == 1;
}

static bool handshake(websocket_t *ws) {
	bool default_port = ws->port == (ws->tls ? 443 : 80);
	char request[1024];
	int  len = snprintf(request, sizeof(request),
	                    "GET /%s HTTP/1.1\r\nHost: %s%s%.0d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
	                     "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n",
	                    ws->path, ws->host, default_port ? "" : ":", default_port ? 0 : ws->port);
	if (len >= (int)sizeof(request) || !write_all(ws, request, len)) {
		return false;
	}
	char *end = NULL;
	while (end == NULL) {
		int n = ws->rx_len < ws->rx_cap - 1 ? io_read(ws, ws->rx + ws->rx_len, ws->rx_cap - 1 - ws->rx_len) : -1;
		if (n <= 0) {
			return false;
		}
		ws->rx_len += n;
		ws->rx[ws->rx_len] = '\0';
		end                = strstr(ws->rx, "\r\n\r\n");
	}
	if (strncmp(ws->rx, "HTTP/1.1 101", 12) != 0) {
		return false;
	}
	// Keep frames that came with the response
	int header_len = (int)(end - ws->rx) + 4;
	ws->rx_len -= header_len;
	memmove(ws->rx, ws->rx + header_len, ws->rx_len);
	return true;
}

static void websocket_free(websocket_t *ws) {
	if (ws->ssl != NULL) {
		SSL_free(ws->ssl);
	}
	if (ws->fd >= 0) {
		close(ws->fd);
	}
	free(ws->rx);
	free(ws->msg);
	free(ws->host);
	free(ws->path);
	free(ws);
}

static void *connect_thread(void *arg) {
	websocket_t *ws = arg;
	bool         ok = tcp_connect(ws) && handshake(ws);
	if (ok) {
		fcntl(ws->fd, F_SETFL, fcntl(ws->fd, F_GETFL) | O_NONBLOCK);
		ws->last_send = time(NULL);
	}
	int connecting = IRON_WEBSOCKET_CONNECTING;
	if (!atomic_compare_exchange_strong(&ws->state, &connecting, ok ? IRON_WEBSOCKET_OPEN : IRON_WEBSOCKET_CLOSED)) {
		websocket_free(ws);
	}
	return NULL;
}

i32 iron_websocket_open(char *url) {
	bool tls = strncmp(url, "wss://", 6) == 0;
	if (!tls && strncmp(url, "ws://", 5) != 0) {
		return -1;
	}
	int slot = 0;
	while (slot < MAX_SOCKETS && sockets[slot] != NULL) {
		slot++;
	}
	if (slot == MAX_SOCKETS) {
		return -1;
	}
	if (ctx == NULL) {
		signal(SIGPIPE, SIG_IGN); // SSL_write fails on a dropped connection instead
		ctx = SSL_CTX_new(TLS_client_method());
		SSL_CTX_set_default_verify_paths(ctx);
	}
	const char  *start = url + (tls ? 6 : 5);
	const char  *slash = strchr(start, '/');
	websocket_t *ws    = calloc(1, sizeof(websocket_t));
	ws->host           = slash ? strndup(start, slash - start) : strdup(start);
	ws->path           = strdup(slash ? slash + 1 : "");
	ws->port           = tls ? 443 : 80;
	ws->tls            = tls;
	ws->fd             = -1;
	ws->rx_cap         = 16384;
	ws->rx             = malloc(ws->rx_cap);
	ws->state          = IRON_WEBSOCKET_CONNECTING;
	char *colon        = strrchr(ws->host, ':');
	if (colon != NULL) {
		ws->port = atoi(colon + 1);
		*colon   = '\0';
	}
	sockets[slot] = ws;
	pthread_t thread;
	pthread_create(&thread, NULL, connect_thread, ws);
	pthread_detach(thread);
	return slot;
}

static websocket_t *websocket_get(i32 handle) {
	return handle >= 0 && handle < MAX_SOCKETS ? sockets[handle] : NULL;
}

void iron_websocket_send(i32 handle, char *text) {
	websocket_t *ws = websocket_get(handle);
	if (ws != NULL && text != NULL && ws->state == IRON_WEBSOCKET_OPEN) {
		send_frame(ws, 1, text, (int)strlen(text));
	}
}

char *iron_websocket_receive(i32 handle) {
	websocket_t *ws = websocket_get(handle);
	if (ws == NULL || ws->state != IRON_WEBSOCKET_OPEN) {
		return NULL;
	}
	if (ws->msg_done) {
		ws->msg_len  = 0;
		ws->msg_done = false;
	}
	if (time(NULL) - ws->last_send >= PING_SECONDS) {
		send_frame(ws, 9, NULL, 0);
	}

	bool dead = false;
	while (true) {
		if (ws->rx_cap - ws->rx_len < 4096) {
			ws->rx_cap *= 2;
			ws->rx = realloc(ws->rx, ws->rx_cap);
		}
		int n = io_read(ws, ws->rx + ws->rx_len, ws->rx_cap - ws->rx_len);
		if (n <= 0) {
			dead = n < 0;
			break;
		}
		ws->rx_len += n;
	}

	char *r   = NULL;
	int   pos = 0;
	while (r == NULL && ws->state == IRON_WEBSOCKET_OPEN && ws->rx_len - pos >= 2) {
		unsigned char *p      = (unsigned char *)ws->rx + pos;
		int            avail  = ws->rx_len - pos;
		bool           fin    = (p[0] & 0x80) != 0;
		int            opcode = p[0] & 15;
		bool           masked = (p[1] & 0x80) != 0;
		uint64_t       len    = p[1] & 127;
		int            i      = 2;
		if (len == 126) {
			if (avail < 4) {
				break;
			}
			len = (p[2] << 8) | p[3];
			i   = 4;
		}
		else if (len == 127) {
			if (avail < 10) {
				break;
			}
			len = 0;
			for (int k = 0; k < 8; ++k) {
				len = (len << 8) | p[2 + k];
			}
			i = 10;
		}
		if (len > MAX_MESSAGE || (opcode < 3 && ws->msg_len + len > MAX_MESSAGE)) {
			ws->state = IRON_WEBSOCKET_CLOSED;
			break;
		}
		unsigned char *mask = p + i;
		i += masked ? 4 : 0;
		if ((uint64_t)avail < i + len) {
			break;
		}
		char *payload = (char *)p + i;
		for (uint64_t k = 0; masked && k < len; ++k) {
			payload[k] ^= mask[k & 3];
		}
		pos += i + (int)len;

		if (opcode < 3) { // Continuation, text, binary
			if (opcode != 0) {
				ws->msg_len = 0;
			}
			if (ws->msg_len + (int)len + 1 > ws->msg_cap) {
				ws->msg_cap = ws->msg_len + (int)len + 1;
				ws->msg     = realloc(ws->msg, ws->msg_cap);
			}
			memcpy(ws->msg + ws->msg_len, payload, len);
			ws->msg_len += (int)len;
			if (fin) {
				ws->msg[ws->msg_len] = '\0';
				ws->msg_done         = true;
				r                    = ws->msg;
			}
		}
		else if (opcode == 8) {
			ws->state = IRON_WEBSOCKET_CLOSED;
		}
		else if (opcode == 9) {
			send_frame(ws, 10, payload, (int)len);
		}
	}
	ws->rx_len -= pos;
	memmove(ws->rx, ws->rx + pos, ws->rx_len);
	if (r == NULL && dead) {
		ws->state = IRON_WEBSOCKET_CLOSED;
	}
	return r;
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
	int state       = atomic_exchange(&ws->state, RELEASED);
	if (state == IRON_WEBSOCKET_CONNECTING) {
		return;
	}
	if (state == IRON_WEBSOCKET_OPEN) {
		send_frame(ws, 8, "\x03\xe8", 2); // Normal closure
	}
	websocket_free(ws);
}
