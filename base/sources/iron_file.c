
#include "iron_file.h"
#include "iron_gc.h"
#include "iron_json.h"
#include "iron_map.h"
#include "iron_net.h"
#include "iron_path.h"
#include "iron_string.h"
#include "iron_system.h"
#include "kong/dir.h"
#ifdef IRON_ANDROID
#include "backends/android_system.h"
#endif
#ifdef IRON_IOS
#include "backends/ios_file_dialog.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef IRON_WINDOWS
#include "backends/windows_mini.h"
#include <malloc.h>
#include <memory.h>
#elif defined(IRON_WASM)
#else
#include <sys/stat.h>
#endif

i32         iron_sys_command(char *cmd);
extern char temp_string[1024 * 128];
#ifdef IRON_WINDOWS
extern wchar_t temp_wstring[1024 * 32];
#endif

static char *fileslocation = NULL;
#ifdef IRON_WINDOWS
static wchar_t wfilepath[1001];
#endif
#if defined(IRON_MACOS) || defined(IRON_IOS)
const char *iron_get_resource_path();
#endif

void iron_internal_set_files_location(char *dir) {
	fileslocation = dir;
}

char *iron_internal_files_location(void) {
#ifdef IRON_MACOS
	static char path[1024];
	strcpy(path, iron_get_resource_path());
	strcat(path, "/");
	strcat(path, IRON_OUTDIR);
	strcat(path, "/");
	return path;
#elif defined(IRON_IOS)
	static char path[1024];
	strcpy(path, iron_get_resource_path());
	strcat(path, "/");
	strcat(path, IRON_OUTDIR);
	strcat(path, "/");
	return path;
#elif defined(IRON_WASM)
	return "";
#else
	return fileslocation;
#endif
}

#ifdef IRON_WINDOWS
static size_t iron_libc_file_reader_read(iron_file_reader_t *reader, void *data, size_t size) {
	DWORD readBytes = 0;
	if (ReadFile(reader->data, data, (DWORD)size, &readBytes, NULL)) {
		return (size_t)readBytes;
	}
	else {
		return 0;
	}
}

static bool iron_libc_file_reader_seek(iron_file_reader_t *reader, size_t pos) {
	// TODO: make this 64-bit compliant
	SetFilePointer(reader->data, (LONG)pos, NULL, FILE_BEGIN);
	return true;
}

static bool iron_libc_file_reader_close(iron_file_reader_t *reader) {
	CloseHandle(reader->data);
	return true;
}

static size_t iron_libc_file_reader_pos(iron_file_reader_t *reader) {
	// TODO: make this 64-bit compliant
	return (size_t)SetFilePointer(reader->data, 0, NULL, FILE_CURRENT);
}
#else
static size_t iron_libc_file_reader_read(iron_file_reader_t *reader, void *data, size_t size) {
	return fread(data, 1, size, (FILE *)reader->data);
}

static bool iron_libc_file_reader_seek(iron_file_reader_t *reader, size_t pos) {
	fseek((FILE *)reader->data, pos, SEEK_SET);
	return true;
}

static bool iron_libc_file_reader_close(iron_file_reader_t *reader) {
	if (reader->data != NULL) {
		fclose((FILE *)reader->data);
		reader->data = NULL;
	}
	return true;
}

static size_t iron_libc_file_reader_pos(iron_file_reader_t *reader) {
	return ftell((FILE *)reader->data);
}
#endif

bool iron_internal_file_reader_open(iron_file_reader_t *reader, const char *filename, int type) {
	char filepath[1001];
#ifdef IRON_IOS
	strcpy(filepath, type == IRON_FILE_TYPE_SAVE ? iron_internal_save_path() : iron_get_resource_path());
	if (type != IRON_FILE_TYPE_SAVE) {
		strcat(filepath, "/");
		strcat(filepath, IRON_OUTDIR);
		strcat(filepath, "/");
	}

	strcat(filepath, filename);
#endif
#ifdef IRON_MACOS
	strcpy(filepath, type == IRON_FILE_TYPE_SAVE ? iron_internal_save_path() : iron_get_resource_path());
	if (type != IRON_FILE_TYPE_SAVE) {
		strcat(filepath, "/");
		strcat(filepath, IRON_OUTDIR);
		strcat(filepath, "/");
	}
	strcat(filepath, filename);
#endif
#ifdef IRON_WINDOWS
	if (type == IRON_FILE_TYPE_SAVE) {
		strcpy(filepath, iron_internal_save_path());
		strcat(filepath, filename);
	}
	else {
		strcpy(filepath, filename);
	}
	size_t filepathlength = strlen(filepath);
	for (size_t i = 0; i < filepathlength; ++i)
		if (filepath[i] == '/')
			filepath[i] = '\\';
#endif
#if defined(IRON_LINUX) || defined(IRON_ANDROID)
	if (type == IRON_FILE_TYPE_SAVE) {
		strcpy(filepath, iron_internal_save_path());
		strcat(filepath, filename);
	}
	else {
		strcpy(filepath, filename);
	}
#endif
#ifdef IRON_WASM
	strcpy(filepath, filename);
#endif

#ifdef IRON_WINDOWS
	// Drive letter or network
	bool is_abs = (filename[1] == ':' && filename[2] == '\\') || (filename[0] == '\\' && filename[1] == '\\');
#else
	bool is_abs = filename[0] == '/';
#endif

#ifdef IRON_WASM
	is_abs = is_abs || (filename[0] == '.' && filename[1] == '/');
#endif

	if (is_abs) {
		strcpy(filepath, filename);
	}
	else if (fileslocation != NULL && type != IRON_FILE_TYPE_SAVE) {
		strcpy(filepath, fileslocation);
		strcat(filepath, "/");
		strcat(filepath, filename);
	}

#ifdef IRON_WINDOWS
	MultiByteToWideChar(CP_UTF8, 0, filepath, -1, wfilepath, 1000);
	reader->data = CreateFileW(wfilepath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (reader->data == INVALID_HANDLE_VALUE) {
		return false;
	}
#else
	reader->data = fopen(filepath, "rb");
	if (reader->data == NULL) {
		return false;
	}
#endif

#ifdef IRON_WINDOWS
	// TODO: make this 64-bit compliant
	reader->size = (size_t)GetFileSize(reader->data, NULL);
#else
	fseek((FILE *)reader->data, 0, SEEK_END);
	reader->size = ftell((FILE *)reader->data);
	fseek((FILE *)reader->data, 0, SEEK_SET);
#endif

	reader->read  = iron_libc_file_reader_read;
	reader->seek  = iron_libc_file_reader_seek;
	reader->close = iron_libc_file_reader_close;
	reader->pos   = iron_libc_file_reader_pos;

	return true;
}

#if !defined(IRON_ANDROID)
bool iron_file_reader_open(iron_file_reader_t *reader, const char *filename, int type) {
	memset(reader, 0, sizeof(*reader));
	return iron_internal_file_reader_open(reader, filename, type);
}
#endif

size_t iron_file_reader_read(iron_file_reader_t *reader, void *data, size_t size) {
	return reader->read(reader, data, size);
}

bool iron_file_reader_seek(iron_file_reader_t *reader, size_t pos) {
	return reader->seek(reader, pos);
}

bool iron_file_reader_close(iron_file_reader_t *reader) {
	return reader->close(reader);
}

size_t iron_file_reader_pos(iron_file_reader_t *reader) {
	return reader->pos(reader);
}

size_t iron_file_reader_size(iron_file_reader_t *reader) {
	return reader->size;
}

float iron_read_f32le(uint8_t *data) {
	return *(float *)data;
}

uint64_t iron_read_u64le(uint8_t *data) {
	return *(uint64_t *)data;
}

int64_t iron_read_s64le(uint8_t *data) {
	return *(int64_t *)data;
}

uint32_t iron_read_u32le(uint8_t *data) {
	return *(uint32_t *)data;
}

int32_t iron_read_s32le(uint8_t *data) {
	return *(int32_t *)data;
}

uint16_t iron_read_u16le(uint8_t *data) {
	return *(uint16_t *)data;
}

int16_t iron_read_s16le(uint8_t *data) {
	return *(int16_t *)data;
}

uint8_t iron_read_u8(uint8_t *data) {
	return *data;
}

int8_t iron_read_s8(uint8_t *data) {
	return *(int8_t *)data;
}

bool iron_file_writer_open(iron_file_writer_t *writer, const char *filepath) {
	writer->file = NULL;

	char path[1001];
	strcpy(path, iron_internal_save_path());
	strcat(path, filepath);

#ifdef IRON_WINDOWS
	wchar_t wpath[MAX_PATH];
	MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH);
	writer->file = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
#else
	writer->file = fopen(path, "wb");
#endif
	if (writer->file == NULL) {
		iron_log("Could not open file %s.", filepath);
		return false;
	}
	return true;
}

void iron_file_writer_close(iron_file_writer_t *writer) {
	if (writer->file != NULL) {
#ifdef IRON_WINDOWS
		CloseHandle(writer->file);
#else
		fclose((FILE *)writer->file);
#endif
		writer->file = NULL;
	}
}

void iron_file_writer_write(iron_file_writer_t *writer, void *data, int size) {
#ifdef IRON_WINDOWS
	DWORD written = 0;
	WriteFile(writer->file, data, (DWORD)size, &written, NULL);
#else
	fwrite(data, 1, size, (FILE *)writer->file);
#endif
}

char *iron_read_directory(char *path) {
	char *files = temp_string;
	files[0]    = 0;

	directory dir = open_dir(path);
	if (dir.handle == NULL) {
		return files;
	}

	while (true) {
		file f = read_next_file(&dir);
		if (!f.valid) {
			break;
		}

#ifdef IRON_WINDOWS
		char file_path[512];
		strcpy(file_path, path);
		strcat(file_path, "\\");
		strcat(file_path, f.name);
		if (FILE_ATTRIBUTE_HIDDEN & GetFileAttributesA(file_path)) {
			continue; // Skip hidden files
		}
#endif

		if (files[0] != '\0') {
			strcat(files, "\n");
		}
		strcat(files, f.name);
	}
	close_dir(&dir);
	return files;
}

void iron_create_directory(char *path) {
#ifdef IRON_IOS
	IOSCreateDirectory(path);
#elif defined(IRON_WINDOWS)
	char cmd[1024];
	strcpy(cmd, "mkdir \"");
	strcat(cmd, path);
	strcat(cmd, "\"");
	iron_sys_command(cmd);
#elif defined(IRON_WASM)
	(void)path;
#else
	char cmd[1024];
	strcpy(cmd, "mkdir -p \"");
	strcat(cmd, path);
	strcat(cmd, "\"");
	iron_sys_command(cmd);
#endif
}

bool iron_is_directory(char *path) {
#ifdef IRON_WINDOWS
	DWORD attribs = GetFileAttributesA(path);
	return attribs != INVALID_FILE_ATTRIBUTES && (attribs & FILE_ATTRIBUTE_DIRECTORY);
#elif defined(IRON_WASM)
	return false;
#else
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool iron_file_exists(char *path) {
	iron_file_reader_t reader;
	if (iron_file_reader_open(&reader, path, IRON_FILE_TYPE_ASSET)) {
		iron_file_reader_close(&reader);
		return true;
	}
	return false;
}

void iron_delete_file(char *path) {
#ifdef IRON_IOS
	IOSDeleteFile(path);
#elif defined(IRON_WINDOWS)
	char cmd[1024];
	strcpy(cmd, "del /f \"");
	strcat(cmd, path);
	strcat(cmd, "\"");
	iron_sys_command(cmd);
#else
	char cmd[1024];
	strcpy(cmd, "rm \"");
	strcat(cmd, path);
	strcat(cmd, "\"");
	iron_sys_command(cmd);
#endif
}

void iron_file_save_bytes(char *path, buffer_t *bytes, u64 length) {
	u64 byte_length = length > 0 ? length : (u64)bytes->length;
	if (byte_length > (u64)bytes->length) {
		byte_length = (u64)bytes->length;
	}

#ifdef IRON_WINDOWS
	MultiByteToWideChar(CP_UTF8, 0, path, -1, temp_wstring, 1024);
	FILE *file = _wfopen(temp_wstring, L"wb");
#else
	FILE *file = fopen(path, "wb");
#endif
	if (file == NULL) {
		return;
	}
	fwrite(bytes->buffer, 1, byte_length, file);
	fclose(file);
}

typedef struct _callback_data {
	int32_t size;
	char    url[512];
	void (*func)(char *, buffer_t *);
} _callback_data_t;

void _https_callback(const char *body, void *callback_data) {
	_callback_data_t *cbd    = (_callback_data_t *)callback_data;
	buffer_t         *buffer = NULL;
	if (body != NULL) {
		buffer         = malloc(sizeof(buffer_t));
		buffer->length = cbd->size > 0 ? cbd->size : strlen(body);
		buffer->buffer = malloc(buffer->length);
		memcpy(buffer->buffer, body, buffer->length);
	}
	cbd->func(cbd->url, buffer);
	free(cbd);
}

void iron_file_download(char *url, void (*callback)(char *, buffer_t *), i32 size, char *dst_path) {
	_callback_data_t *cbd = malloc(sizeof(_callback_data_t));
	cbd->size             = size;
	strcpy(cbd->url, url);
	cbd->func = callback;

	char url_base[512];
	char url_path[512];
	int  i = 0;
	for (; i < strlen(url) - 8; ++i) {
		if (url[i + 8] == '/') {
			break;
		}
		url_base[i] = url[i + 8]; // Strip https://
	}
	url_base[i] = 0;
	int j       = 0;
	if (strlen(url_base) < strlen(url) - 8) {
		++i; // Skip /
	}
	for (; j < strlen(url) - 8 - i; ++j) {
		if (url[i + 8 + j] == 0) {
			break;
		}
		url_path[j] = url[i + 8 + j];
	}
	url_path[j] = 0;

	iron_net_request(url_base, url_path, NULL, 443, IRON_HTTPS_GET, &_https_callback, cbd, dst_path);
}

char     *strings_check_internet_connection(void);
char     *sys_buffer_to_string(buffer_t *b);
buffer_t *data_get_blob(char *file);
void      console_error(char *s);
i32       parse_int(const char *s);

#ifdef IRON_WINDOWS
static char *file_cmd_copy = "copy";
#else
static char *file_cmd_copy = "cp";
#endif

any_map_t *file_cloud       = NULL;
i32_map_t *file_cloud_sizes = NULL;

static void (*_file_init_cloud_bytes_done)(void) = NULL;

static any_map_t *_file_download_map    = NULL;
static any_map_t *_file_cache_cloud_map = NULL;

#if defined(IRON_ANDROID) || defined(IRON_WASM)
static any_map_t *file_internal = NULL;
#endif

any_array_t *file_read_directory(char *path) {
	if (starts_with(path, "cloud")) {
		any_array_t *files = file_cloud != NULL ? any_map_get(file_cloud, string_replace_all(path, "\\", "/")) : NULL;
		if (files != NULL) {
			return files;
		}
		else {
			return gc_alloc(sizeof(any_array_t));
		}
	}

#ifdef IRON_WASM
	if (starts_with(path, "/./")) {
		path = substring(path, 2, string_length(path));
	}
#endif

#if defined(IRON_ANDROID) || defined(IRON_WASM)
	path = string_replace_all(path, "//", "/");
	if (file_internal == NULL) {
		buffer_t *blob = data_get_blob("data_list.json");
		char     *s    = sys_buffer_to_string(blob);
		file_internal  = json_parse_to_map(s);
		gc_root(file_internal);
	}
	if (any_map_get(file_internal, path) != NULL) {
		return string_split(any_map_get(file_internal, path), ",");
	}
#endif

	any_array_t *files = string_split(iron_read_directory(path), "\n");
	if (files->length == 1 && string_equals(files->buffer[0], "")) {
		array_pop(files);
		return files;
	}
	array_sort(files, NULL);

	// Folders first
	i32 num = files->length;
	for (i32 i = 0; i < num; ++i) {
		char *f = files->buffer[i];
		if (!iron_is_directory(path_join(path, f))) {
			array_splice(files, i, 1);
			any_array_push(files, f);
			i--;
			num--;
		}
	}

	return files;
}

void file_copy(char *src_path, char *dst_path) {
	char cmd[1024];
	strcpy(cmd, file_cmd_copy);
	strcat(cmd, " \"");
	strcat(cmd, src_path);
	strcat(cmd, "\" \"");
	strcat(cmd, dst_path);
	strcat(cmd, "\"");
	iron_sys_command(cmd);
}

void file_start(char *path) {
#ifdef IRON_WINDOWS
	char cmd[1024];
	strcpy(cmd, "start \"\" \"");
	strcat(cmd, path);
	strcat(cmd, "\"");
	iron_sys_command(cmd);
#elif defined(IRON_LINUX)
	char cmd[1024];
	strcpy(cmd, "xdg-open \"");
	strcat(cmd, path);
	strcat(cmd, "\"");
	iron_sys_command(cmd);
#else
	char cmd[1024];
	strcpy(cmd, "open \"");
	strcat(cmd, path);
	strcat(cmd, "\"");
	iron_sys_command(cmd);
#endif
}

static void _file_download_to_callback(char *url, buffer_t *ab) {
	file_download_data_t *fdd = any_map_get(_file_download_map, url);
	fdd->done(url);
}

void file_download_to(char *url, char *dst_path, void (*done)(char *url), i32 size) {
	if (_file_download_map == NULL) {
		_file_download_map = any_map_create();
		gc_root(_file_download_map);
	}
	file_download_data_t *fdd = malloc(sizeof(file_download_data_t));
	fdd->done                 = done;
	any_map_set(_file_download_map, url, fdd);
	iron_file_download(url, _file_download_to_callback, size, dst_path);
}

static void _file_cache_cloud_callback(char *url) {
	file_cache_cloud_data_t *fccd = any_map_get(_file_cache_cloud_map, url);
	if (!iron_file_exists(fccd->dest)) {
		console_error(strings_check_internet_connection());
		fccd->done(NULL);
		free(fccd);
		return;
	}
	fccd->done(fccd->dest);
	free(fccd);
}

void file_cache_cloud(char *path, void (*done)(char *dest), char *server) {
	if (_file_cache_cloud_map == NULL) {
		_file_cache_cloud_map = any_map_create();
		gc_root(_file_cache_cloud_map);
	}

	char dest[512];
	if (path_is_protected()) {
		strcpy(dest, iron_internal_save_path());
	}
	else {
		strcpy(dest, iron_internal_files_location());
		strcat(dest, PATH_SEP);
	}
	strcat(dest, path);

	if (iron_file_exists(dest)) {
		done(dest);
		return;
	}

	char        *file_dir  = substring(dest, 0, string_last_index_of(dest, PATH_SEP));
	any_array_t *dir_files = file_read_directory(file_dir);
	if (dir_files->length == 0 || string_equals(dir_files->buffer[0], "")) {
		iron_create_directory(file_dir);
	}

#ifdef IRON_WINDOWS
	path = string_replace_all(path, "\\", "/");
#endif
	char *url = string("%s/%s", server, path);

	file_cache_cloud_data_t *fccd = malloc(sizeof(file_cache_cloud_data_t));
	strcpy(fccd->dest, dest);
	strcpy(fccd->path, path);
	fccd->done = done;
	any_map_set(_file_cache_cloud_map, url, fccd);

	file_download_to(url, dest, _file_cache_cloud_callback, i32_map_get(file_cloud_sizes, path));
}

static void _file_init_cloud_bytes_callback(char *url, buffer_t *buffer) {
	if (buffer == NULL) {
		any_array_t *empty = gc_alloc(sizeof(any_array_t));
		any_map_set(file_cloud, "cloud", empty);
		console_error(strings_check_internet_connection());
		return;
	}

	any_array_t *files     = gc_alloc(sizeof(any_array_t));
	i32_array_t *sizes     = gc_alloc(sizeof(i32_array_t));
	char        *str       = sys_buffer_to_string(buffer);
	i32          str_len   = string_length(str);
	i32          pos_start = 0;
	i32          pos_end   = 0;

	while (true) {
		if (pos_start >= str_len) {
			break;
		}
		pos_end = string_index_of_pos(str, " ", pos_start);
		any_array_push(files, substring(str, pos_start, pos_end));
		pos_start = pos_end + 1;
		pos_end   = string_index_of_pos(str, "\n", pos_start);
		if (pos_end == -1) {
			pos_end = str_len - 1;
		}
		i32_array_push(sizes, parse_int(substring(str, pos_start, pos_end)));
		pos_start = pos_end + 1;
	}

	for (i32 i = 0; i < (i32)files->length; ++i) {
		char *file = files->buffer[i];
		if (path_is_folder(file)) {
			any_array_t *empty = gc_alloc(sizeof(any_array_t));
			any_map_set(file_cloud, substring(file, 0, string_length(file) - 1), empty);
		}
	}
	for (i32 i = 0; i < (i32)files->length; ++i) {
		char *file   = files->buffer[i];
		bool  nested = string_index_of(file, "/") != string_last_index_of(file, "/");
		if (nested) {
			i32   delim  = path_is_folder(file) ? string_last_index_of(substring(file, 0, string_length(file) - 1), "/") : string_last_index_of(file, "/");
			char *parent = substring(file, 0, delim);
			char *child  = path_is_folder(file) ? substring(file, delim + 1, string_length(file) - 1) : substring(file, delim + 1, string_length(file));
			any_array_push(any_map_get(file_cloud, parent), child);
			if (!path_is_folder(file)) {
				i32_map_set(file_cloud_sizes, file, sizes->buffer[i]);
			}
		}
	}

	_file_init_cloud_bytes_done();
}

static void _file_init_cloud_index_callback(char *url, buffer_t *buffer) {}

void file_init_cloud_bytes(void (*done)(void), char *append, char *server) {
	_file_init_cloud_bytes_done = done;
	char *index_url             = string("%s/index.txt%s", server, append != NULL ? append : "");
	iron_file_download(index_url, _file_init_cloud_bytes_callback, 0, NULL);
	iron_file_download("https://cloud-index.armory3d.workers.dev/", _file_init_cloud_index_callback, 0, NULL);
}

void file_init_cloud(void (*done)(void), char *server) {
	file_cloud = any_map_create();
	gc_root(file_cloud);
	file_cloud_sizes = i32_map_create();
	gc_root(file_cloud_sizes);
	file_init_cloud_bytes(done, NULL, server);
}
