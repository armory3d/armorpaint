// Extracts localizable strings from a set of source files and writes them to JSON files.
// This script can create new translations or update existing ones.
// Usage:
// `../make --js extract_locales.js <locale code>`
// Generates a `paint/assets/locale/<locale code>.json` file

let locale = scriptArgs[4];

if (!locale) {
	console.log("Locale code not set!");
	std.exit();
}

let locale_path = "./paint/assets/locale/" + locale + ".json";

let out = {};
let old = {};
if (fs_exists(locale_path)) {
	old = JSON.parse(fs_readfile(locale_path).toString());
}

function unescape_char(c) {
	if (c == "n") { return "\n"; }
	if (c == "t") { return "\t"; }
	if (c == "r") { return "\r"; }
	return c; // covers \" \\ and anything else
}

let source_paths = [ "paint/sources", "paint/sources/nodes_material", "paint/sources/nodes_brush", "paint/sources/nodes_neural", "paint/sources/io", "paint/sources/render", "paint/sources/slots", "paint/sources/traits", "paint/sources/ui", "paint/sources/util" ];

for (let path of source_paths) {
	if (!fs_exists(path)) {
		continue;
	}

	let files = fs_readdir(path);
	for (let file of files) {
		if (!file.endsWith(".c")) {
			continue;
		}

		let data  = fs_readfile(path + "/" + file).toString();
		let start = 0;
		while (true) {
			start = data.indexOf('tr("', start);
			if (start == -1) {
				break;
			}
			start += 3; // tr

			let val = "";
			while (start < data.length && data.charAt(start) == '"') {
				++start; // opening quote
				while (start < data.length) {
					let c = data.charAt(start);
					if (c == '\\') {
						val += unescape_char(data.charAt(start + 1));
						start += 2;
						continue;
					}
					if (c == '"') {
						++start; // closing quote
						break;
					}
					val += c;
					++start;
				}
				while (start < data.length && " \t\r\n".indexOf(data.charAt(start)) != -1) {
					++start;
				}
			}

			if (old.hasOwnProperty(val)) {
				out[val] = old[val];
			}
			else {
				out[val] = "";
			}
		}
	}
}

fs_writefile(locale_path, JSON.stringify(out, Object.keys(out).sort(), 4));
