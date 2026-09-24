// Strings, paths, file system and process helpers

#ifdef __linux__
#define _GNU_SOURCE // popen, S_IFMT and st_mtim with -std=c11
#endif

#include "make.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

// ███████╗████████╗██████╗ ██╗███╗   ██╗ ██████╗
// ██╔════╝╚══██╔══╝██╔══██╗██║████╗  ██║██╔════╝
// ███████╗   ██║   ██████╔╝██║██╔██╗ ██║██║  ███╗
// ╚════██║   ██║   ██╔══██╗██║██║╚██╗██║██║   ██║
// ███████║   ██║   ██║  ██║██║██║ ╚████║╚██████╔╝
// ╚══════╝   ╚═╝   ╚═╝  ╚═╝╚═╝╚═╝  ╚═══╝ ╚═════╝

any_array_t *list_create(void) {
	return calloc(1, sizeof(any_array_t));
}

bool list_contains(any_array_t *list, char *s) {
	for (int i = 0; i < list->length; ++i) {
		if (strcmp(list->buffer[i], s) == 0) {
			return true;
		}
	}
	return false;
}

char *str_lower(char *s) {
	char *r = string_copy(s);
	for (char *c = r; *c; ++c) {
		*c = tolower((unsigned char)*c);
	}
	return r;
}

char *str_upper(char *s) {
	char *r = string_copy(s);
	for (char *c = r; *c; ++c) {
		*c = toupper((unsigned char)*c);
	}
	return r;
}

int str_index_of(char *s, char *search) {
	char *found = strstr(s, search);
	return found == NULL ? -1 : (int)(found - s);
}

int str_last_index_of(char *s, char *search) {
	int   len  = (int)strlen(search);
	char *last = NULL;
	char *pos  = s;
	while ((pos = strstr(pos, search)) != NULL) {
		last = pos;
		pos += len > 0 ? len : 1;
		if (len == 0 && *pos == '\0') {
			break;
		}
	}
	return last == NULL ? -1 : (int)(last - s);
}

static char *str_slice(char *s, int start, int end) {
	char *r = string_alloc(end - start + 1);
	memcpy(r, s + start, end - start);
	return r;
}

char *js_substring(char *s, int start, int end) {
	int len = (int)strlen(s);
	start   = start < 0 ? 0 : (start > len ? len : start);
	end     = end < 0 ? 0 : (end > len ? len : end);
	if (start > end) {
		int t = start;
		start = end;
		end   = t;
	}
	return str_slice(s, start, end);
}

char *js_substr(char *s, int start, int length) {
	int len = (int)strlen(s);
	if (start < 0) {
		start = len + start < 0 ? 0 : len + start;
	}
	if (start > len) {
		start = len;
	}
	if (length < 0) {
		length = 0;
	}
	if (length > len - start) {
		length = len - start;
	}
	return str_slice(s, start, start + length);
}

char *str_replace(char *s, char *search, char *replace) {
	return string_replace_all(s, search, replace);
}

char *str_trim(char *s) {
	int start = 0;
	int end   = (int)strlen(s);
	while (start < end && isspace((unsigned char)s[start])) {
		start++;
	}
	while (end > start && isspace((unsigned char)s[end - 1])) {
		end--;
	}
	return str_slice(s, start, end);
}

char *str_join(any_array_t *parts, char *sep) {
	return string_array_join(parts, sep);
}

char *json_escape(char *s) {
	buffer_t sb = {0};
	string_buffer_init(&sb);
	for (unsigned char *c = (unsigned char *)s; *c; ++c) {
		char tmp[8];
		switch (*c) {
		case '"':
			string_buffer_append(&sb, "\\\"");
			break;
		case '\\':
			string_buffer_append(&sb, "\\\\");
			break;
		case '\b':
			string_buffer_append(&sb, "\\b");
			break;
		case '\f':
			string_buffer_append(&sb, "\\f");
			break;
		case '\n':
			string_buffer_append(&sb, "\\n");
			break;
		case '\r':
			string_buffer_append(&sb, "\\r");
			break;
		case '\t':
			string_buffer_append(&sb, "\\t");
			break;
		default:
			if (*c < 0x20) {
				snprintf(tmp, sizeof(tmp), "\\u%04x", *c);
			}
			else {
				tmp[0] = *c;
				tmp[1] = '\0';
			}
			string_buffer_append(&sb, tmp);
		}
	}
	return string_buffer_get(&sb);
}

// "**" matches anything, "*" matches anything but "/"
static bool glob_match(const char *t, const char *p) {
	if (*p == '\0') {
		return *t == '\0';
	}
	if (p[0] == '*' && p[1] == '*') {
		for (const char *s = t;; ++s) {
			if (glob_match(s, p + 2)) {
				return true;
			}
			if (*s == '\0') {
				return false;
			}
		}
	}
	if (*p == '*') {
		for (const char *s = t;; ++s) {
			if (glob_match(s, p + 1)) {
				return true;
			}
			if (*s == '\0' || *s == '/') {
				return false;
			}
		}
	}
	return *t == *p && glob_match(t + 1, p + 1);
}

bool glob_matches(char *text, char *pattern) {
	return glob_match(text, pattern);
}

// ██████╗  █████╗ ████████╗██╗  ██╗
// ██╔══██╗██╔══██╗╚══██╔══╝██║  ██║
// ██████╔╝███████║   ██║   ███████║
// ██╔═══╝ ██╔══██║   ██║   ██╔══██║
// ██║     ██║  ██║   ██║   ██║  ██║
// ╚═╝     ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝

char *path_join_n(char **parts) {
	any_array_t *list = list_create();
	for (int i = 0; parts[i] != NULL; ++i) {
		any_array_push(list, parts[i]);
	}
	return path_normalize(str_join(list, path_sep));
}

bool path_isabs(char *p) {
	return p[0] == '/' || (p[0] != '\0' && p[1] == ':') || (p[0] == '\\' && p[1] == '\\');
}

static char *_path_resolve(char *base, char *relative) {
	any_array_t *stack = string_split(base, "/");
	any_array_t *parts = string_split(relative, "/");
	for (int i = 0; i < parts->length; ++i) {
		if (strcmp(parts->buffer[i], ".") == 0) {
			continue;
		}
		if (strcmp(parts->buffer[i], "..") == 0) {
			if (stack->length > 0) {
				stack->length--;
			}
		}
		else {
			any_array_push(stack, parts->buffer[i]);
		}
	}
	return str_join(stack, "/");
}

char *path_resolve_n(char **parts) {
	any_array_t *args = list_create();
	for (int i = 0; parts[i] != NULL; ++i) {
		any_array_push(args, parts[i]);
	}
	if (!path_isabs(args->buffer[0])) {
		array_insert(args, 0, os_cwd());
	}
	int   i = args->length - 1;
	char *p = path_normalize(args->buffer[i]);
	while (!path_isabs(p) && i > 0) {
		i--;
		p = _path_resolve(args->buffer[i], p);
		p = path_normalize(p);
	}
	return p;
}

char *path_relative(char *from, char *to) {
	any_array_t *a  = string_split(from, path_sep);
	any_array_t *b  = string_split(to, path_sep);
	int          ai = 0;
	int          bi = 0;
	while (strcmp(a->buffer[ai], b->buffer[bi]) == 0) {
		ai++;
		bi++;
		if (ai == a->length || bi == b->length) {
			break;
		}
	}
	buffer_t sb = {0};
	string_buffer_init(&sb);
	for (int i = ai; i < a->length; ++i) {
		string_buffer_append(&sb, "..");
		string_buffer_append(&sb, path_sep);
	}
	for (int i = bi; i < b->length; ++i) {
		string_buffer_append(&sb, b->buffer[i]);
		if (i < b->length - 1) {
			string_buffer_append(&sb, path_sep);
		}
	}
	return string_buffer_get(&sb);
}

char *path_normalize(char *p) {
	p                = str_replace(p, other_path_sep, path_sep);
	char *double_sep = string("%s%s", path_sep, path_sep);
	while (str_index_of(p, double_sep) != -1) {
		p = str_replace(p, double_sep, path_sep);
	}
	if (ends_with(p, path_sep)) {
		p = js_substring(p, 0, (int)strlen(p) - 1);
	}
	any_array_t *ar = string_split(p, path_sep);
	int          i  = 0;
	while (i < ar->length) {
		if (i > 0 && strcmp(ar->buffer[i], "..") == 0 && strcmp(ar->buffer[i - 1], "..") != 0) {
			array_splice(ar, i - 1, 2);
			i--;
		}
		else {
			i++;
		}
	}
	return str_join(ar, path_sep);
}

char *path_extname(char *p) {
	return js_substring(p, str_last_index_of(p, "."), (int)strlen(p));
}

char *path_basename(char *p) {
	return js_substring(p, str_last_index_of(p, path_sep) + 1, (int)strlen(p));
}

char *path_basename_noext(char *p) {
	return js_substring(p, str_last_index_of(p, path_sep) + 1, str_last_index_of(p, "."));
}

char *path_dirname(char *p) {
	return js_substring(p, 0, str_last_index_of(p, path_sep));
}

char *path_slashes(char *p) {
	return str_replace(p, "\\", "/");
}

// ███████╗███████╗
// ██╔════╝██╔════╝
// █████╗  ███████╗
// ██╔══╝  ╚════██║
// ██║     ███████║
// ╚═╝     ╚══════╝

bool fs_exists(char *p) {
	struct stat st;
	return stat(p, &st) == 0;
}

bool fs_isdir(char *p) {
	struct stat st;
	return stat(p, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

double fs_mtime(char *p) {
	struct stat st;
	if (stat(p, &st) != 0) {
		return 0;
	}
#if defined(_WIN32)
	return (double)st.st_mtime * 1000.0;
#elif defined(__APPLE__)
	return (double)((int64_t)st.st_mtimespec.tv_sec * 1000 + st.st_mtimespec.tv_nsec / 1000000);
#else
	return (double)((int64_t)st.st_mtim.tv_sec * 1000 + st.st_mtim.tv_nsec / 1000000);
#endif
}

any_array_t *fs_readdir(char *p) {
	any_array_t *files = list_create();
#ifdef _WIN32
	WIN32_FIND_DATAA data;
	HANDLE           handle = FindFirstFileA(string("%s\\*", p), &data);
	if (handle == INVALID_HANDLE_VALUE) {
		return files;
	}
	do {
		if (strcmp(data.cFileName, ".") != 0 && strcmp(data.cFileName, "..") != 0) {
			any_array_push(files, string_copy(data.cFileName));
		}
	} while (FindNextFileA(handle, &data));
	FindClose(handle);
#else
	DIR *dir = opendir(p);
	if (dir == NULL) {
		return files;
	}
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
			any_array_push(files, string_copy(entry->d_name));
		}
	}
	closedir(dir);
#endif
	return files;
}

char *fs_readfile(char *p, int *size) {
	FILE *f = fopen(p, "rb");
	if (f == NULL) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	int len = (int)ftell(f);
	fseek(f, 0, SEEK_SET);
	char *data = malloc(len + 1);
	len        = (int)fread(data, 1, len, f);
	data[len]  = '\0';
	fclose(f);
	if (size != NULL) {
		*size = len;
	}
	return data;
}

// Text mode, line endings follow the host platform
void fs_writefile(char *p, char *data) {
	FILE *f = fopen(p, "w");
	if (f == NULL) {
		printf("Error: could not write %s\n", p);
		return;
	}
	fputs(data, f);
	fclose(f);
}

void fs_writefile_bytes(char *p, char *data, int size) {
	FILE *f = fopen(p, "wb");
	if (f == NULL) {
		printf("Error: could not write %s\n", p);
		return;
	}
	fwrite(data, 1, size, f);
	fclose(f);
}

void fs_copyfile(char *from, char *to) {
	int   size;
	char *data = fs_readfile(from, &size);
	if (data == NULL) {
		printf("Error: could not read %s\n", from);
		return;
	}
	fs_writefile_bytes(to, data, size);
	free(data);
}

static void fs_mkdir(char *p) {
#ifdef _WIN32
	_mkdir(p);
#else
	mkdir(p, 0777);
#endif
}

void fs_ensuredir(char *dir) {
	if (dir[0] != '\0' && !fs_exists(dir)) {
		fs_ensuredir(js_substring(dir, 0, str_last_index_of(dir, path_sep)));
		fs_mkdir(dir);
	}
}

void fs_copydir(char *from, char *to) {
	fs_ensuredir(to);
	any_array_t *files = fs_readdir(from);
	for (int i = 0; i < files->length; ++i) {
		char *file = files->buffer[i];
		if (fs_isdir(path_join(from, file))) {
			fs_copydir(path_join(from, file), path_join(to, file));
		}
		else {
			fs_copyfile(path_join(from, file), path_join(to, file));
		}
	}
}

//  ██████╗ ███████╗
// ██╔═══██╗██╔════╝
// ██║   ██║███████╗
// ██║   ██║╚════██║
// ╚██████╔╝███████║
//  ╚═════╝ ╚══════╝

char *os_platform(void) {
#if defined(_WIN32)
	return "win32";
#elif defined(__APPLE__)
	return "darwin";
#else
	return "linux";
#endif
}

char *os_cwd(void) {
	char buf[4096];
#ifdef _WIN32
	_getcwd(buf, sizeof(buf));
#else
	if (getcwd(buf, sizeof(buf)) == NULL) {
		buf[0] = '\0';
	}
#endif
	return string_copy(buf);
}

char *os_env(char *name) {
	return getenv(name);
}

// Runs a command and waits for it, output goes to the terminal
// On Windows the output is also captured and the working directory change persists, as in the original tool
int os_exec(any_array_t *args, char *cwd, char **out_stdout) {
#ifdef _WIN32
	if (cwd != NULL) {
		_chdir(cwd);
	}
	buffer_t cmd = {0};
	string_buffer_init(&cmd);
	for (int i = 0; i < args->length; ++i) {
		string_buffer_append(&cmd, args->buffer[i]);
		string_buffer_append(&cmd, " ");
	}

	HANDLE              read_pipe;
	HANDLE              write_pipe;
	SECURITY_ATTRIBUTES sa;
	sa.nLength              = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle       = TRUE;
	sa.lpSecurityDescriptor = NULL;
	CreatePipe(&read_pipe, &write_pipe, &sa, 0);
	SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOA        si;
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof(si));
	si.cb         = sizeof(si);
	si.dwFlags    = STARTF_USESTDHANDLES;
	si.hStdOutput = write_pipe;
	si.hStdError  = write_pipe;
	ZeroMemory(&pi, sizeof(pi));
	if (!CreateProcessA(NULL, string_buffer_get(&cmd), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
		CloseHandle(write_pipe);
		CloseHandle(read_pipe);
		return -1;
	}
	CloseHandle(write_pipe);

	buffer_t out = {0};
	string_buffer_init(&out);
	char  buf[4096];
	DWORD bytes_read;
	while (ReadFile(read_pipe, buf, sizeof(buf) - 1, &bytes_read, NULL) && bytes_read > 0) {
		buf[bytes_read] = '\0';
		string_buffer_append(&out, buf);
	}
	CloseHandle(read_pipe);
	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD exit_code;
	GetExitCodeProcess(pi.hProcess, &exit_code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	char *result = string_buffer_get(&out);
	printf("%s", result);
	if (out_stdout != NULL) {
		*out_stdout = result;
	}
	return (int)exit_code;
#else
	fflush(stdout);
	char **argv = calloc(args->length + 1, sizeof(char *));
	for (int i = 0; i < args->length; ++i) {
		argv[i] = args->buffer[i];
	}
	pid_t pid = fork();
	if (pid == 0) {
		if (cwd != NULL && chdir(cwd) != 0) {
			_exit(127);
		}
		execvp(argv[0], argv);
		_exit(127);
	}
	if (out_stdout != NULL) {
		*out_stdout = "";
	}
	if (pid < 0) {
		return -1;
	}
	int status;
	if (waitpid(pid, &status, 0) < 0) {
		return -1;
	}
	if (WIFEXITED(status)) {
		return WEXITSTATUS(status);
	}
	return WIFSIGNALED(status) ? -WTERMSIG(status) : -1;
#endif
}

char *os_popen(char *cmd) {
	fflush(stdout);
#ifdef _WIN32
	FILE *p = _popen(cmd, "r");
#else
	FILE *p = popen(cmd, "r");
#endif
	if (p == NULL) {
		return "";
	}
	buffer_t out = {0};
	string_buffer_init(&out);
	char buf[4096];
	int  n;
	while ((n = (int)fread(buf, 1, sizeof(buf) - 1, p)) > 0) {
		buf[n] = '\0';
		string_buffer_append(&out, buf);
	}
#ifdef _WIN32
	_pclose(p);
#else
	pclose(p);
#endif
	return string_buffer_get(&out);
}

int os_cpus_length(void) {
	int cores = 0;
#ifdef _WIN32
	char *n = os_env("NUMBER_OF_PROCESSORS");
	cores   = n != NULL ? atoi(n) : 0;
#else
	cores = (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
	return cores > 0 ? cores : 8;
}

void os_chmod_exec(char *p) {
#ifndef _WIN32
	struct stat st;
	if (stat(p, &st) == 0) {
		chmod(p, st.st_mode | 0111);
	}
#endif
}

char *sys_dir(void) {
	if (strcmp(os_platform(), "linux") == 0) {
		return "linux_x64";
	}
	else if (strcmp(os_platform(), "win32") == 0) {
		return "windows_x64";
	}
	return "macos";
}

char *random_uuid(void) {
	static bool seeded = false;
	if (!seeded) {
		srand((unsigned)time(NULL) ^ (unsigned)clock());
		seeded = true;
	}
	unsigned a = ((unsigned)rand() << 16) ^ (unsigned)rand();
	unsigned b = (unsigned)rand() & 0xffff;
	unsigned c = (unsigned)rand() & 0xfff;
	unsigned d = ((unsigned)rand() << 16) ^ (unsigned)rand();
	unsigned e = (unsigned)rand() & 0xffff;
	return string("%08x-%04x-4000-8%03x-%08x%04x", a, b, c, d, e);
}

// ███████╗██╗  ██╗ █████╗  ██╗
// ██╔════╝██║  ██║██╔══██╗███║
// ███████╗███████║███████║╚██║
// ╚════██║██╔══██║██╔══██║ ██║
// ███████║██║  ██║██║  ██║ ██║
// ╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝ ╚═╝

static uint32_t rol(uint32_t v, int n) {
	return (v << n) | (v >> (32 - n));
}

static void sha1(const uint8_t *data, size_t len, uint8_t out[20]) {
	uint32_t h[5]   = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
	size_t   padded = ((len + 8) / 64 + 1) * 64;
	uint8_t *msg    = calloc(1, padded);
	memcpy(msg, data, len);
	msg[len]      = 0x80;
	uint64_t bits = (uint64_t)len * 8;
	for (int i = 0; i < 8; ++i) {
		msg[padded - 1 - i] = (uint8_t)(bits >> (i * 8));
	}
	for (size_t chunk = 0; chunk < padded; chunk += 64) {
		uint32_t w[80];
		for (int i = 0; i < 16; ++i) {
			const uint8_t *b = msg + chunk + i * 4;
			w[i]             = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
		}
		for (int i = 16; i < 80; ++i) {
			w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
		}
		uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
		for (int i = 0; i < 80; ++i) {
			uint32_t f, k;
			if (i < 20) {
				f = (b & c) | (~b & d);
				k = 0x5A827999;
			}
			else if (i < 40) {
				f = b ^ c ^ d;
				k = 0x6ED9EBA1;
			}
			else if (i < 60) {
				f = (b & c) | (b & d) | (c & d);
				k = 0x8F1BBCDC;
			}
			else {
				f = b ^ c ^ d;
				k = 0xCA62C1D6;
			}
			uint32_t t = rol(a, 5) + f + e + k + w[i];
			e          = d;
			d          = c;
			c          = rol(b, 30);
			b          = a;
			a          = t;
		}
		h[0] += a;
		h[1] += b;
		h[2] += c;
		h[3] += d;
		h[4] += e;
	}
	free(msg);
	for (int i = 0; i < 5; ++i) {
		out[i * 4 + 0] = (uint8_t)(h[i] >> 24);
		out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
		out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
		out[i * 4 + 3] = (uint8_t)h[i];
	}
}

// Deterministic id for xcode objects: sha1 of namespace + path, formatted as a uuid
char *new_path_id(char *path) {
	char   *input = string("7448ebd8-cfc8-4f45-8b3d-5df577ceea6d%s", path);
	uint8_t hash[20];
	char    hex[41];
	sha1((uint8_t *)input, strlen(input), hash);
	for (int i = 0; i < 20; ++i) {
		snprintf(hex + i * 2, 3, "%02X", hash[i]);
	}
	return string("%.8s-%.4s-%.4s-%.4s-%.12s", hex, hex + 8, hex + 12, hex + 16, hex + 20);
}
