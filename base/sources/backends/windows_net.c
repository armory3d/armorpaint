#include <Windows.h>
#include <iron_net.h>
#include <iron_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <winhttp.h>

typedef struct {
	HINTERNET             hsession;
	HINTERNET             hconnect;
	HINTERNET             hrequest;
	HANDLE                hfile;
	char                 *return_data;
	int                   return_data_size;
	int                   return_data_index;
	char                 *file_buffer;
	DWORD                 file_buffer_size;
	DWORD                 status;
	iron_https_callback_t callback;
	void                 *callbackdata;
} async_context_t;

typedef struct request {
	int                   status;
	char                 *response;
	iron_https_callback_t callback;
	void                 *callbackdata;
	async_context_t      *ctx;
	struct request       *next;
} request_t;

volatile uint64_t iron_net_bytes_downloaded = 0;

static BOOL             initialized  = FALSE;
static request_t       *pending_head = NULL;
static request_t       *pending_tail = NULL;
static CRITICAL_SECTION cs;

static void finish_request(async_context_t *ctx, BOOL success) {
	if (ctx->hfile != INVALID_HANDLE_VALUE) {
		CloseHandle(ctx->hfile);
		ctx->hfile = INVALID_HANDLE_VALUE;
	}

	char *response = NULL;
	if (ctx->return_data) {
		ctx->return_data[ctx->return_data_index] = '\0';
		response                                 = ctx->return_data;
		ctx->return_data                         = NULL;
	}

	request_t *req    = malloc(sizeof(request_t));
	req->status       = success ? (int)ctx->status : 0;
	req->response     = response;
	req->callback     = ctx->callback;
	req->callbackdata = ctx->callbackdata;
	req->ctx          = ctx;
	req->next         = NULL;

	EnterCriticalSection(&cs);
	if (pending_tail == NULL) {
		pending_head = pending_tail = req;
	}
	else {
		pending_tail->next = req;
		pending_tail       = req;
	}
	LeaveCriticalSection(&cs);
}

static void CALLBACK iron_winhttp_callback(HINTERNET hInternet, DWORD_PTR dwContext, DWORD dwInternetStatus, LPVOID lpvStatusInformation,
                                           DWORD dwStatusInformationLength) {
	async_context_t *ctx = (async_context_t *)dwContext;

	switch (dwInternetStatus) {
	case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
		WinHttpReceiveResponse(ctx->hrequest, NULL);
		break;

	case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE: {
		DWORD size = sizeof(ctx->status);
		WinHttpQueryHeaders(ctx->hrequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &ctx->status, &size,
		                    WINHTTP_NO_HEADER_INDEX);
		WinHttpQueryDataAvailable(ctx->hrequest, NULL);
		break;
	}

	case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE: {
		DWORD dwSize = *(DWORD *)lpvStatusInformation;
		if (dwSize == 0) {
			finish_request(ctx, TRUE);
		}
		else {
			if (ctx->hfile != INVALID_HANDLE_VALUE) {
				if (dwSize > ctx->file_buffer_size) {
					ctx->file_buffer      = realloc(ctx->file_buffer, dwSize);
					ctx->file_buffer_size = dwSize;
				}
				WinHttpReadData(ctx->hrequest, ctx->file_buffer, dwSize, NULL);
			}
			else {
				if (ctx->return_data_index + (int)dwSize + 1 > ctx->return_data_size) {
					ctx->return_data_size = (ctx->return_data_index + (int)dwSize + 1) * 2;
					ctx->return_data      = realloc(ctx->return_data, ctx->return_data_size);
				}
				WinHttpReadData(ctx->hrequest, ctx->return_data + ctx->return_data_index, dwSize, NULL);
			}
		}
		break;
	}

	case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
		if (dwStatusInformationLength != 0) {
			if (ctx->hfile != INVALID_HANDLE_VALUE) {
				DWORD written;
				WriteFile(ctx->hfile, ctx->file_buffer, dwStatusInformationLength, &written, NULL);
				InterlockedAdd64((LONG64 volatile *)&iron_net_bytes_downloaded, dwStatusInformationLength);
			}
			else {
				ctx->return_data_index += dwStatusInformationLength;
			}
		}
		WinHttpQueryDataAvailable(ctx->hrequest, NULL);
		break;

	case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
		finish_request(ctx, FALSE);
		break;

	case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
		if (ctx->return_data)
			free(ctx->return_data);
		if (ctx->file_buffer)
			free(ctx->file_buffer);
		free(ctx);
		break;
	}
}

void iron_net_request(const char *url_base, const char *url_path, const char *data, int port, int method, const char *headers,
                      iron_https_callback_t callback, void *callbackdata, const char *dst_path) {
	if (!initialized) {
		InitializeCriticalSection(&cs);
		initialized = TRUE;
	}

	async_context_t *ctx = calloc(1, sizeof(async_context_t));
	ctx->callback        = callback;
	ctx->callbackdata    = callbackdata;
	ctx->hfile           = INVALID_HANDLE_VALUE;

	if (dst_path) {
		wchar_t wpath[1024];
		MultiByteToWideChar(CP_UTF8, 0, dst_path, -1, wpath, 1024);
		ctx->hfile = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (ctx->hfile == INVALID_HANDLE_VALUE) {
			free(ctx);
			callback(0, NULL, callbackdata);
			return;
		}
	}

	ctx->hsession = WinHttpOpen(L"Iron/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
	if (!ctx->hsession) {
		if (ctx->hfile != INVALID_HANDLE_VALUE)
			CloseHandle(ctx->hfile);
		free(ctx);
		callback(0, NULL, callbackdata);
		return;
	}

	wchar_t wurl[4096];
	MultiByteToWideChar(CP_UTF8, 0, url_base, -1, wurl, 4096);
	ctx->hconnect = WinHttpConnect(ctx->hsession, wurl, port, 0);
	if (!ctx->hconnect) {
		WinHttpCloseHandle(ctx->hsession);
		if (ctx->hfile != INVALID_HANDLE_VALUE)
			CloseHandle(ctx->hfile);
		free(ctx);
		callback(0, NULL, callbackdata);
		return;
	}

	wchar_t wurl_path[4096];
	MultiByteToWideChar(CP_UTF8, 0, url_path, -1, wurl_path, 4096);
	ctx->hrequest = WinHttpOpenRequest(ctx->hconnect, method == IRON_HTTPS_GET ? L"GET" : L"POST", wurl_path, NULL, WINHTTP_NO_REFERER,
	                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

	if (!ctx->hrequest) {
		WinHttpCloseHandle(ctx->hsession);
		if (ctx->hfile != INVALID_HANDLE_VALUE)
			CloseHandle(ctx->hfile);
		free(ctx);
		callback(0, NULL, callbackdata);
		return;
	}

	WinHttpSetStatusCallback(ctx->hrequest, iron_winhttp_callback, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0);

	wchar_t wheaders[4096] = L"";
	if (headers) {
		MultiByteToWideChar(CP_UTF8, 0, headers, -1, wheaders, 4096);
	}
	DWORD data_len = data ? (DWORD)strlen(data) : 0;
	if (!WinHttpSendRequest(ctx->hrequest, headers ? wheaders : WINHTTP_NO_ADDITIONAL_HEADERS, headers ? (DWORD)-1L : 0, (LPVOID)data, data_len, data_len,
	                        (DWORD_PTR)ctx)) {
		WinHttpCloseHandle(ctx->hsession);
		callback(0, NULL, callbackdata);
	}
}

void iron_net_update() {
	if (!initialized)
		return;
	request_t *current = NULL;
	EnterCriticalSection(&cs);
	current      = pending_head;
	pending_head = pending_tail = NULL;
	LeaveCriticalSection(&cs);
	while (current) {
		request_t *next = current->next;
		if (current->callback)
			current->callback(current->status, current->response, current->callbackdata);
		if (current->ctx->hsession)
			WinHttpCloseHandle(current->ctx->hsession);
		if (current->response)
			free(current->response);
		free(current);
		current = next;
	}
}
