#define _GNU_SOURCE // strcasestr
#include <fcntl.h>
#include <iron_net.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_THREADS 32

typedef struct async_context {
	SSL                  *ssl;
	int                   sock_fd;
	FILE                 *fp;
	char                 *buf;
	int                   buf_len;
	iron_https_callback_t callback;
	void                 *callbackdata;
	char                 *url_base;
	char                 *url_path;
	char                 *dst_path;
	char                 *data;
	char                 *headers;
	int                   method;
	int                   port;
	int                   status;
	struct async_context *next;
} async_context_t;

typedef struct request {
	int                   status;
	char                 *response;
	iron_https_callback_t callback;
	void                 *callbackdata;
	struct request       *next;
} request_t;

volatile uint64_t       iron_net_bytes_downloaded = 0;
static SSL_CTX         *ctx                       = NULL;
static pthread_mutex_t  ctx_mutex                 = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t  pending_mutex             = PTHREAD_MUTEX_INITIALIZER;
static request_t       *pending_head              = NULL;
static request_t       *pending_tail              = NULL;
static int              active_threads            = 0;
static async_context_t *queue_head                = NULL;
static async_context_t *queue_tail                = NULL;
static pthread_mutex_t  queue_mutex               = PTHREAD_MUTEX_INITIALIZER;

static void *download_thread(void *arg);

static void try_process_queue() {
	pthread_mutex_lock(&queue_mutex);
	while (active_threads < MAX_THREADS && queue_head != NULL) {
		async_context_t *async_ctx = queue_head;
		queue_head                 = async_ctx->next;
		if (queue_head == NULL) {
			queue_tail = NULL;
		}
		active_threads++;
		pthread_t thread;
		pthread_create(&thread, NULL, download_thread, async_ctx);
		pthread_detach(thread);
	}
	pthread_mutex_unlock(&queue_mutex);
}

static void finish_request(async_context_t *async_ctx, char *response) {
	if (async_ctx->fp) {
		fflush(async_ctx->fp);
		fclose(async_ctx->fp);
	}
	if (async_ctx->ssl) {
		SSL_shutdown(async_ctx->ssl);
		SSL_free(async_ctx->ssl);
	}
	if (async_ctx->sock_fd >= 0) {
		close(async_ctx->sock_fd);
	}
	if (async_ctx->buf) {
		free(async_ctx->buf);
	}
	if (async_ctx->url_base) {
		free(async_ctx->url_base);
	}
	if (async_ctx->url_path) {
		free(async_ctx->url_path);
	}
	if (async_ctx->dst_path) {
		free(async_ctx->dst_path);
	}
	free(async_ctx->data);
	free(async_ctx->headers);

	request_t *req    = malloc(sizeof(request_t));
	req->status       = async_ctx->status;
	req->response     = response;
	req->callback     = async_ctx->callback;
	req->callbackdata = async_ctx->callbackdata;
	req->next         = NULL;

	pthread_mutex_lock(&pending_mutex);
	if (pending_tail == NULL) {
		pending_head = pending_tail = req;
	}
	else {
		pending_tail->next = req;
		pending_tail       = req;
	}
	pthread_mutex_unlock(&pending_mutex);

	free(async_ctx);
	pthread_mutex_lock(&queue_mutex);
	active_threads--;
	pthread_mutex_unlock(&queue_mutex);
	try_process_queue();
}

static void parse_url(const char *url, char **host, char **path, int *port) {
	const char *start = url + 8;
	*port             = 443;
	const char *slash = strchr(start, '/');
	if (slash) {
		*host = strndup(start, slash - start);
		*path = strdup(slash + 1);
	}
	else {
		*host = strdup(start);
		*path = strdup("");
	}
	char *colon = strchr(*host, ':');
	if (colon) {
		*port  = atoi(colon + 1);
		*colon = '\0';
	}
}

static int do_request(async_context_t *async_ctx) {
	struct addrinfo  hints = {0};
	struct addrinfo *res   = NULL;
	hints.ai_family        = AF_INET;
	hints.ai_socktype      = SOCK_STREAM;
	char port_str[6];

	snprintf(port_str, sizeof(port_str), "%d", async_ctx->port);
	if (getaddrinfo(async_ctx->url_base, port_str, &hints, &res) != 0) {
		return 0;
	}

	int sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (connect(sock_fd, res->ai_addr, res->ai_addrlen) < 0) {
		close(sock_fd);
		freeaddrinfo(res);
		return 0;
	}
	freeaddrinfo(res);

	SSL *ssl = SSL_new(ctx);
	SSL_set_fd(ssl, sock_fd);
	SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
	SSL_set_tlsext_host_name(ssl, async_ctx->url_base);
	if (async_ctx->headers) { // Requests with headers carry credentials, check who gets them
		SSL_set_verify(ssl, SSL_VERIFY_PEER, NULL);
		SSL_set1_host(ssl, async_ctx->url_base);
	}
	if (SSL_connect(ssl) <= 0) {
		SSL_free(ssl);
		close(sock_fd);
		return 0;
	}

	async_ctx->ssl     = ssl;
	async_ctx->sock_fd = sock_fd;
	const char *method      = async_ctx->method == IRON_HTTPS_POST ? "POST" : "GET";
	const char *headers     = async_ctx->headers ? async_ctx->headers : "";
	const char *data        = async_ctx->data ? async_ctx->data : "";
	int         data_len    = (int)strlen(data);
	int         request_cap = (int)(strlen(async_ctx->url_path) + strlen(async_ctx->url_base) + strlen(headers) + data_len + 256);
	char       *request     = malloc(request_cap);
	int request_len = snprintf(request, request_cap, "%s /%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n%s", method, async_ctx->url_path, async_ctx->url_base,
	                           headers);
	if (async_ctx->method == IRON_HTTPS_POST) {
		request_len += snprintf(request + request_len, request_cap - request_len, "Content-Length: %d\r\n", data_len);
	}
	request_len += snprintf(request + request_len, request_cap - request_len, "\r\n%s", data);
	SSL_write(ssl, request, request_len);
	free(request);
	int   pos = 0;
	int   n;
	int   header_buf_size = 16384;
	char *header_buf      = malloc(header_buf_size);

	while ((n = SSL_read(ssl, header_buf + pos, header_buf_size - pos - 1)) > 0) {
		pos += n;
		header_buf[pos]   = '\0';
		char *headers_end = strstr(header_buf, "\r\n\r\n");
		if (headers_end == NULL) {
			if (pos >= header_buf_size - 1) {
				header_buf_size *= 2;
				header_buf = realloc(header_buf, header_buf_size);
			}
			continue;
		}
		sscanf(header_buf, "HTTP/%*s %d", &async_ctx->status);

		if (async_ctx->status == 302) {
			char *location = strstr(header_buf, "Location: ");
			if (location) {
				location += 10;
				char *location_end = strstr(location, "\r\n");
				*location_end      = '\0';
				char *new_host     = NULL;
				char *new_path     = NULL;
				int   new_port     = 0;
				parse_url(location, &new_host, &new_path, &new_port);
				SSL_shutdown(async_ctx->ssl);
				SSL_free(async_ctx->ssl);
				close(async_ctx->sock_fd);
				async_ctx->ssl     = NULL;
				async_ctx->sock_fd = -1;
				free(async_ctx->url_base);
				free(async_ctx->url_path);
				async_ctx->url_base = new_host;
				async_ctx->url_path = new_path;
				async_ctx->port     = new_port;
				free(header_buf);
				return do_request(async_ctx);
			}
		}

		char *body_start = headers_end + 4;
		int   body_len   = pos - (body_start - header_buf);
		int   is_chunked = strcasestr(header_buf, "transfer-encoding: chunked") != NULL;
		// Stop at the end of the body
		char *content_length_header = strcasestr(header_buf, "\r\ncontent-length:");
		int   content_length        = content_length_header != NULL && content_length_header < headers_end ? atoi(content_length_header + 17) : -1;

		if (async_ctx->fp) {
			// No chunked support for fp yet
			if (body_len > 0) {
				fwrite(body_start, 1, body_len, async_ctx->fp);
				iron_net_bytes_downloaded += body_len;
			}
			while ((n = SSL_read(ssl, async_ctx->buf, 64 * 1024)) > 0) {
				fwrite(async_ctx->buf, 1, n, async_ctx->fp);
				iron_net_bytes_downloaded += n;
			}
			free(header_buf);
			finish_request(async_ctx, NULL);
			return 1;
		}
		else {
			if (body_len > 0) {
				if (body_len >= async_ctx->buf_len) {
					async_ctx->buf_len = body_len + 1;
					async_ctx->buf     = realloc(async_ctx->buf, async_ctx->buf_len);
				}
				memcpy(async_ctx->buf, body_start, body_len);
				pos = body_len;
			}
			else {
				pos = 0;
			}
			while ((content_length < 0 || pos < content_length) && (n = SSL_read(ssl, async_ctx->buf + pos, async_ctx->buf_len - pos - 1)) > 0) {
				pos += n;
				if (pos >= async_ctx->buf_len - 1) {
					async_ctx->buf_len *= 2;
					async_ctx->buf = realloc(async_ctx->buf, async_ctx->buf_len);
				}
			}
			async_ctx->buf[pos] = '\0';
			free(header_buf);
			if (is_chunked) {
				char *chunk_ptr = async_ctx->buf;
				char *out_ptr   = async_ctx->buf;
				while (1) {
					char *end_ptr;
					int   chunk_size = strtol(chunk_ptr, &end_ptr, 16);
					if (chunk_size == 0)
						break;
					if (end_ptr == chunk_ptr)
						break;
					chunk_ptr = end_ptr + 2; // \r\n
					memmove(out_ptr, chunk_ptr, chunk_size);
					out_ptr += chunk_size;
					chunk_ptr += chunk_size + 2; // \r\n
				}
				*out_ptr = '\0';
			}
			char *response = strdup(async_ctx->buf);
			finish_request(async_ctx, response);
			return 1;
		}
	}
	free(header_buf);
	return 0;
}

static void *download_thread(void *arg) {
	async_context_t *async_ctx = (async_context_t *)arg;
	if (async_ctx->dst_path) {
		async_ctx->fp = fopen(async_ctx->dst_path, "wb");
		if (!async_ctx->fp) {
			finish_request(async_ctx, NULL);
			return NULL;
		}
		setvbuf(async_ctx->fp, NULL, _IOFBF, 256 * 1024);
	}
	if (!do_request(async_ctx)) {
		finish_request(async_ctx, NULL);
	}
	return NULL;
}

void iron_net_request(const char *url_base, const char *url_path, const char *data, int port, int method, const char *headers,
                      iron_https_callback_t callback, void *callbackdata, const char *dst_path) {
	if (ctx == NULL) {
		ctx = SSL_CTX_new(TLS_client_method());
		SSL_CTX_set_mode(ctx, SSL_MODE_RELEASE_BUFFERS);
		SSL_CTX_set_default_verify_paths(ctx);
	}

	async_context_t *async_ctx = calloc(1, sizeof(async_context_t));
	async_ctx->callback        = callback;
	async_ctx->callbackdata    = callbackdata;
	async_ctx->buf_len         = 1024 * 1024;
	async_ctx->buf             = malloc(async_ctx->buf_len);
	async_ctx->sock_fd         = -1;
	async_ctx->url_base        = strdup(url_base);
	async_ctx->url_path        = strdup(url_path);
	async_ctx->port            = port;
	async_ctx->method          = method;
	async_ctx->data            = data ? strdup(data) : NULL;
	async_ctx->headers         = headers ? strdup(headers) : NULL;
	if (dst_path) {
		async_ctx->dst_path = strdup(dst_path);
	}

	pthread_mutex_lock(&queue_mutex);
	if (queue_tail == NULL) {
		queue_head = queue_tail = async_ctx;
	}
	else {
		queue_tail->next = async_ctx;
		queue_tail       = async_ctx;
	}
	pthread_mutex_unlock(&queue_mutex);
	try_process_queue();
}

void iron_net_update() {
	request_t *current;
	pthread_mutex_lock(&pending_mutex);
	current      = pending_head;
	pending_head = pending_tail = NULL;
	pthread_mutex_unlock(&pending_mutex);
	while (current) {
		request_t *next = current->next;
		current->callback(current->status, current->response, current->callbackdata);
		if (current->response) {
			free(current->response);
		}
		free(current);
		current = next;
	}
}
