#include "dir.h"
#include "kong.h"
#include <stddef.h>
#include <stdio.h>

#ifdef _WIN32

#include <Windows.h>

directory open_dir(const char *dirname) {
	char pattern[1024];
	strcpy(pattern, dirname);
	strcat(pattern, "\\*");

	WIN32_FIND_DATAA data;
	directory        dir;
	dir.handle = FindFirstFileA(pattern, &data);
	if (dir.handle == INVALID_HANDLE_VALUE) {
		return dir;
	}
	FindNextFileA(dir.handle, &data);
	return dir;
}

file read_next_file(directory *dir) {
	WIN32_FIND_DATAA data;
	file             file;
	file.valid = FindNextFileA(dir->handle, &data) != 0;
	if (file.valid) {
		strcpy(file.name, data.cFileName);
	}
	else {
		file.name[0] = 0;
	}
	return file;
}

void close_dir(directory *dir) {
	FindClose(dir->handle);
}

#elif defined(IRON_WASM)

directory open_dir(const char *dirname) {
	directory dir;
	dir.handle = NULL;
	return dir;
}

file read_next_file(directory *dir) {
	file f;
	return f;
}

void close_dir(directory *dir) {}

#else

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

directory open_dir(const char *dirname) {
	directory dir;
	dir.handle = opendir(dirname);
	return dir;
}

file read_next_file(directory *dir) {
	struct dirent *entry = readdir(dir->handle);

	while (entry != NULL && (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)) {
		entry = readdir(dir->handle);
	}

	file f;
	f.valid = entry != NULL;

	if (f.valid) {
		strcpy(f.name, entry->d_name);
	}

	return f;
}

void close_dir(directory *dir) {
	if (dir->handle != NULL) {
		closedir(dir->handle);
		dir->handle = NULL;
	}
}

#endif
