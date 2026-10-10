#include "global.h"

// The app asks for a code, opens the browser on it and polls until the code is approved there.
// The token it gets is kept in account.txt next to config.json and sent as Authorization: Bearer.

#ifndef IRON_WASM

#define ACCOUNT_HOST          "forums.armorpaint.org"
#define ACCOUNT_POLL_INTERVAL 2.0

static char *account_secret         = NULL;
static bool  account_loaded         = false;
static bool  account_poll_in_flight = false;
static f64   account_poll_time      = 0.0;
static bool  account_ticking        = false;
static i32   account_requests       = 0; // In flight, the app stays awake for their answers
static i32   account_attempt        = 0; // Each sign-in is an attempt, answers to a canceled one are ignored

static char *account_path() {
	if (path_is_protected()) {
		return string("%saccount.txt", iron_internal_save_path());
	}
	return string("%s%saccount.txt", path_data(), PATH_SEP);
}

static char *account_headers() {
	return string("Authorization: Bearer %s\r\n", account_token);
}

static void account_poll_done(int status, const char *body, void *attempt);

static void account_tick(void *_) {
	if (account_code == NULL && account_requests == 0) {
		account_ticking = false;
		return;
	}
	iron_delay_idle_sleep();
	if (account_code != NULL && !account_poll_in_flight && !string_equals(account_code, "") && sys_time() >= account_poll_time) {
		account_poll_time      = sys_time() + ACCOUNT_POLL_INTERVAL;
		account_poll_in_flight = true;
		account_requests++;
		iron_net_request(ACCOUNT_HOST, string("auth/app/poll?code=%s&secret=%s", account_code, account_secret), NULL, 443, IRON_HTTPS_GET, NULL,
		                 account_poll_done, (void *)(intptr_t)account_attempt, NULL);
	}
	sys_notify_on_next_frame(account_tick, NULL);
}

static void account_request(char *path, int method, char *headers, iron_https_callback_t callback, void *data) {
	account_requests++;
	if (!account_ticking) {
		account_ticking = true;
		sys_notify_on_next_frame(account_tick, NULL);
	}
	iron_net_request(ACCOUNT_HOST, path, NULL, 443, method, headers, callback, data, NULL);
}

static void account_answered() {
	account_requests--;
	ui_box_hwnd->redraws = 2;
}

static bool account_is_current(void *attempt) {
	return account_code != NULL && (intptr_t)attempt == account_attempt;
}

static void account_clear() {
	account_token = NULL;
	account_email = NULL;
	iron_delete_file(account_path());
}

static void account_me_done(int status, const char *body, void *_) {
	account_answered();
	if (status == 200) {
		any_map_t *m     = json_parse_to_map((char *)body);
		char      *email = any_map_get(m, "email");
		account_email    = email != NULL ? string_copy(email) : NULL;
	}
	else if (status == 401 && account_token != NULL) { // Signed out on the server, or the account is gone
		account_clear();
	}
}

static void account_fetch_me() {
	account_request("auth/me", IRON_HTTPS_GET, account_headers(), account_me_done, NULL);
}

void account_init() {
	if (account_loaded) {
		return;
	}
	account_loaded = true;
	buffer_t *b    = iron_load_blob(account_path());
	if (b == NULL) {
		return;
	}
	account_token = string_copy(trim_end(sys_buffer_to_string(b)));
	iron_delete_blob(b);
	if (string_equals(account_token, "")) {
		account_token = NULL;
		return;
	}
	account_fetch_me();
}

static void account_poll_done(int status, const char *body, void *attempt) {
	account_answered();
	if (!account_is_current(attempt)) {
		return;
	}
	account_poll_in_flight = false;
	if (status == 200) {
		any_map_t *m     = json_parse_to_map((char *)body);
		char      *token = any_map_get(m, "token");
		if (token == NULL) {
			return;
		}
		account_token = string_copy(token);
		account_code  = NULL;
		buffer_t *b   = sys_string_to_buffer(account_token);
		iron_file_save_bytes(account_path(), b, 0);
		account_fetch_me();
	}
	else if (status == 404) {
		account_code = NULL;
		console_error(tr("Sign in expired, try again"));
	}
	// 202 is still waiting for the browser, 0 is a network error, both try again
}

void account_open_browser() {
	iron_load_url(string("https://%s/auth/app?code=%s", ACCOUNT_HOST, account_code));
}

static void account_start_done(int status, const char *body, void *attempt) {
	account_answered();
	if (!account_is_current(attempt)) {
		return;
	}
	any_map_t *m      = status == 200 ? json_parse_to_map((char *)body) : NULL;
	char      *code   = m != NULL ? any_map_get(m, "code") : NULL;
	char      *secret = m != NULL ? any_map_get(m, "secret") : NULL;
	if (code == NULL || secret == NULL) {
		account_code = NULL;
		console_error(strings_check_internet_connection());
		return;
	}
	account_code      = string_copy(code);
	account_secret    = string_copy(secret);
	account_poll_time = sys_time() + ACCOUNT_POLL_INTERVAL;
	account_open_browser();
}

void account_sign_in() {
	if (account_code != NULL) {
		return;
	}
	account_attempt++;
	account_code           = "";
	account_poll_in_flight = false;
	account_request("auth/app/start", IRON_HTTPS_POST, NULL, account_start_done, (void *)(intptr_t)account_attempt);
}

void account_cancel() {
	account_code = NULL;
}

static void account_sign_out_done(int status, const char *body, void *_) {
	account_answered();
}

void account_sign_out() {
	account_request("auth/logout", IRON_HTTPS_POST, account_headers(), account_sign_out_done, NULL);
	account_clear();
}

#endif
