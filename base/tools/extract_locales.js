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

let source_paths = [ "paint/sources", "paint/sources/nodes_material", "paint/sources/nodes_brush", "paint/sources/nodes_neural", "paint/sources/io", "paint/sources/render", "paint/sources/traits", "paint/sources/ui", "paint/sources/util" ];

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
			start += 4; // tr("

			let end_a = data.indexOf('")', start);
			let end_b = data.indexOf('",', start);
			if (end_a == -1) {
				end_a = end_b;
			}
			if (end_b == -1) {
				end_b = end_a;
			}
			let end = end_a < end_b ? end_a : end_b;

			let val = data.substring(start, end);
			val     = val.replaceAll("\\n", "\n");
			if (old.hasOwnProperty(val)) {
				out[val] = old[val];
			}
			else {
				out[val] = "";
			}
			start = end;
		}
	}
}

fs_writefile(locale_path, JSON.stringify(out, Object.keys(out).sort(), 4));
