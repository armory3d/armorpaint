// Extracts localizable strings from a set of source files and writes them to JSON files.
// This script can create new translations or update existing ones.
// Usage:
// `./base/make --c base/tools/extract_locales.c <locale code>`
// Generates a `paint/assets/locale/<locale code>.json` file

#include "amake/amake.h"

char *unescape_char(char c) {
	if (c == 'n') {
		return "\n";
	}
	if (c == 't') {
		return "\t";
	}
	if (c == 'r') {
		return "\r";
	}
	return string("%c", c); // Covers \" \\ and anything else
}

bool is_space(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int main() {
	char *locale = script_arg(0);
	if (locale == NULL) {
		print("Locale code not set!");
		return 0;
	}

	char  *locale_path = string("./paint/assets/locale/%s.json", locale);
	map_t *out         = map_create();
	map_t *old         = map_create();
	if (fs_exists(locale_path)) {
		old = json_parse_map(fs_readfile(locale_path));
	}

	char *source_paths[] = {
	    "paint/sources",        "paint/sources/nodes_material", "paint/sources/nodes_brush", "paint/sources/nodes_neural", "paint/sources/io",
	    "paint/sources/render", "paint/sources/slots",          "paint/sources/traits",      "paint/sources/ui",           "paint/sources/util"};

	for (int i = 0; i < 10; ++i) {
		char *path = source_paths[i];
		if (!fs_exists(path)) {
			continue;
		}

		string_array_t *files = fs_readdir(path);
		for (int j = 0; j < files->length; ++j) {
			char *file = files->buffer[j];
			if (!ends_with(file, ".c")) {
				continue;
			}

			char *data   = fs_readfile(string("%s/%s", path, file));
			int   length = string_length(data);
			int   start  = 0;
			while (true) {
				start = string_index_of(data, "tr(\"", start);
				if (start == -1) {
					break;
				}
				start += 3; // tr

				char *val = "";
				while (start < length && data[start] == '"') {
					++start; // Opening quote
					while (start < length) {
						char c = data[start];
						if (c == '\\') {
							val = string("%s%s", val, unescape_char(data[start + 1]));
							start += 2;
							continue;
						}
						if (c == '"') {
							++start; // Closing quote
							break;
						}
						val = string("%s%c", val, c);
						++start;
					}
					while (start < length && is_space(data[start])) {
						++start;
					}
				}

				char *translated = map_get(old, val);
				map_set(out, val, translated != NULL ? translated : "");
			}
		}
	}

	fs_writefile(locale_path, json_stringify_map(out, 4));
	return 0;
}
