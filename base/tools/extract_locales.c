// Extracts localizable strings from a set of source files and writes them to text files.
// `paint/assets/locale/en.txt` holds the sorted source strings, one per line.
// `paint/assets/locale/<locale code>.txt` holds the translation of each `en.txt` line, an empty line when not translated yet.
// Newlines are stored as `\n` and backslashes as `\\`.
// Usage:
// `./base/make --c base/tools/extract_locales.c`
// Updates `en.txt` and all the existing locale files
// `./base/make --c base/tools/extract_locales.c <locale code>`
// Additionally generates a new `paint/assets/locale/<locale code>.txt` file

#include "amake/amake.h"

char *keys[8192];
int   keys_count = 0;

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

// Keeps each string on a single line
char *escape_line(char *s) {
	char *out    = "";
	int   length = string_length(s);
	for (int i = 0; i < length; ++i) {
		char c = s[i];
		if (c == '\n') {
			out = string("%s\\n", out);
		}
		else if (c == '\\') {
			out = string("%s\\\\", out);
		}
		else if (c != '\r') {
			out = string("%s%c", out, c);
		}
	}
	return out;
}

// Byte order, same as strcmp
int compare(char *a, char *b) {
	int i = 0;
	while (true) {
		int ca = a[i] & 255;
		int cb = b[i] & 255;
		if (ca != cb) {
			return ca - cb;
		}
		if (ca == 0) {
			return 0;
		}
		++i;
	}
	return 0;
}

// Returns the text of the line starting at pos, without the line ending
char *read_line(char *data, int pos, int end) {
	if (end > pos && data[end - 1] == '\r') {
		--end;
	}
	return substring(data, pos, end);
}

int line_end(char *data, int pos) {
	int end = string_index_of(data, "\n", pos);
	return end == -1 ? string_length(data) : end;
}

int count_lines(char *data) {
	int count  = 0;
	int length = string_length(data);
	int pos    = 0;
	while (pos < length) {
		pos = line_end(data, pos) + 1;
		++count;
	}
	return count;
}

int main() {
	char *dir            = "./paint/assets/locale";
	char *source_paths[] = {
	    "paint/sources",    "paint/sources/nodes_material", "paint/sources/nodes_brush", "paint/sources/nodes_neural", "paint/sources/nodes_mesh",
	    "paint/sources/io", "paint/sources/render",         "paint/sources/slots",       "paint/sources/traits",       "paint/sources/ui",
	    "paint/sources/util"};

	// Collect the source strings
	map_t *found = map_create();
	for (int i = 0; i < 11; ++i) {
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

				val = escape_line(val);
				if (map_get(found, val) != NULL) {
					continue;
				}
				if (keys_count == 8192) {
					print("Too many strings, increase the keys array!");
					return 1;
				}
				map_set(found, val, "1");
				keys[keys_count] = val;
				++keys_count;
			}
		}
	}

	// Sort
	for (int i = 1; i < keys_count; ++i) {
		char *key = keys[i];
		int   j   = i - 1;
		while (j >= 0 && compare(keys[j], key) > 0) {
			keys[j + 1] = keys[j];
			--j;
		}
		keys[j + 1] = key;
	}

	// Existing locales are matched against the source strings they were written for
	char *en_path  = string("%s/en.txt", dir);
	char *old_en   = fs_exists(en_path) ? fs_readfile(en_path) : "";
	int   old_size = count_lines(old_en);

	char *new_locale    = script_arg(0);
	bool  new_locale_ok = new_locale != NULL && !string_equals(new_locale, "en");

	string_array_t *files = fs_readdir(dir);

	// A locale file out of line with en.txt would get the wrong translations, write nothing until it is fixed
	bool aligned = true;
	for (int i = 0; i < files->length; ++i) {
		char *file = files->buffer[i];
		if (!ends_with(file, ".txt") || string_equals(file, "en.txt")) {
			continue;
		}
		int size = count_lines(fs_readfile(string("%s/%s", dir, file)));
		if (size != old_size) {
			print(string("%s has %d lines, en.txt has %d", file, size, old_size));
			aligned = false;
		}
	}
	if (!aligned) {
		print("Fix the line counts first, nothing was written!");
		return 1;
	}

	for (int i = 0; i <= files->length; ++i) {
		char *locale;
		if (i < files->length) {
			char *file = files->buffer[i];
			if (!ends_with(file, ".txt") || string_equals(file, "en.txt")) {
				continue;
			}
			locale = substring(file, 0, string_length(file) - 4);
			if (new_locale_ok && string_equals(locale, new_locale)) {
				new_locale_ok = false; // Already exists
			}
		}
		else {
			if (!new_locale_ok) {
				break;
			}
			locale = new_locale;
		}

		char  *locale_path = string("%s/%s.txt", dir, locale);
		map_t *old         = map_create();
		if (fs_exists(locale_path)) {
			char *data   = fs_readfile(locale_path);
			int   en_pos = 0;
			int pos    = 0;
			for (int j = 0; j < old_size; ++j) {
				int en_end = line_end(old_en, en_pos);
				int end    = line_end(data, pos);
				map_set(old, read_line(old_en, en_pos, en_end), read_line(data, pos, end));
				en_pos = en_end + 1;
				pos    = end + 1;
			}
		}

		char *out = "";
		for (int j = 0; j < keys_count; ++j) {
			char *translated = map_get(old, keys[j]);
			out              = string("%s%s\n", out, translated != NULL ? translated : "");
		}
		fs_writefile(locale_path, out);
	}

	char *out = "";
	for (int i = 0; i < keys_count; ++i) {
		out = string("%s%s\n", out, keys[i]);
	}
	fs_writefile(en_path, out);
	return 0;
}
