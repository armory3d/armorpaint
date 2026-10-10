
#include "global.h"

bool  args_use                    = false;
char *args_asset_path             = "";
bool  args_export_textures        = false;
char *args_export_textures_type   = "";
char *args_export_textures_preset = "";
char *args_export_textures_path   = "";
bool  args_reimport_mesh          = false;
bool  args_export_mesh            = false;
char *args_export_mesh_path       = "";
bool  args_export_material        = false;
char *args_export_material_path   = "";
bool  args_api                    = false;
bool  args_script                 = false;
char *args_script_path            = "";

static char *args_path(char *path) {
	if (data_is_abs(path) || data_is_up(path) || starts_with(path, "./")) {
		return string_copy(path);
	}
	return string("./%s", path);
}

#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS)

static void args_sleep_ms(i32 ms) {
#ifdef IRON_WINDOWS
	Sleep(ms);
#else
	struct timespec t;
	t.tv_sec  = 0;
	t.tv_nsec = ms * 1000000;
	nanosleep(&t, NULL);
#endif
}

static char *args_agent_file(char *name) {
	return string("%sagent%s%s", iron_internal_save_path(), PATH_SEP, name);
}

static char *args_read_file(char *path, size_t *size) {
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	size_t n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *s = string_alloc(n + 1);
	n       = fread(s, 1, n, f);
	fclose(f);
	if (size != NULL) {
		*size = n;
	}
	return s;
}

static void args_write_file(char *path, char *s) {
	FILE *f = fopen(path, "wb");
	fwrite(s, 1, strlen(s), f);
	fclose(f);
}

static char *args_request(char *request, i32 pickup_ms) {
	char *req      = args_agent_file("request.txt");
	char *req_tmp  = args_agent_file("request.tmp");
	char *response = args_agent_file("response.txt");
	remove(response);
	args_write_file(req_tmp, request);
	remove(req);
	rename(req_tmp, req);

	const i32 timeout_ms = 300000;
	for (i32 t = 0; t < timeout_ms; t += 50) {
		args_sleep_ms(50);
		char *s = args_read_file(response, NULL);
		if (s != NULL) {
			remove(response);
			return s;
		}
		if (t >= pickup_ms && iron_file_exists(req)) {
			break;
		}
	}
	remove(req);
	return NULL;
}

static char *args_base64(unsigned char *data, size_t size) {
	const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	char       *r     = string_alloc((size + 2) / 3 * 4 + 1);
	char       *p     = r;
	for (size_t i = 0; i < size; i += 3) {
		u32 v = data[i] << 16;
		v |= i + 1 < size ? data[i + 1] << 8 : 0;
		v |= i + 2 < size ? data[i + 2] : 0;
		*p++ = table[(v >> 18) & 63];
		*p++ = table[(v >> 12) & 63];
		*p++ = i + 1 < size ? table[(v >> 6) & 63] : '=';
		*p++ = i + 2 < size ? table[v & 63] : '=';
	}
	return r;
}

static char *args_read_line(void) {
	buffer_t sb;
	string_buffer_init(&sb);
	char chunk[4096];
	while (fgets(chunk, sizeof(chunk), stdin) != NULL) {
		string_buffer_append(&sb, chunk);
		if (sb.length > 0 && sb.buffer[sb.length - 1] == '\n') {
			break;
		}
	}
	if (sb.length == 0) {
		string_buffer_free(&sb);
		return NULL;
	}
	return string_buffer_get(&sb);
}

static char *args_mcp_dir   = NULL;
static i32   args_mcp_count = 0;

static char *args_mcp_begin(char **error) {
	char *s = args_request("begin", 5000);
	if (s == NULL) {
		*error = "ArmorPaint is not running";
		return NULL;
	}
	i32 nl = string_index_of(s, "\n");
	if (nl <= 0) {
		*error = substring(s, nl + 1, string_length(s));
		return NULL;
	}
	args_mcp_dir   = substring(s, 0, nl);
	args_mcp_count = 0;
	return substring(s, nl + 1, string_length(s));
}

static char *args_mcp_script(char *name, char *code) {
	char *path = string("%s/%s%d.c", args_mcp_dir, name, ++args_mcp_count);
	args_write_file(path, code != NULL ? code : "");
	return path;
}

static void args_mcp_call(any_map_t *m) {
	char *name  = any_map_get(m, "params.name");
	char *text  = NULL;
	char *error = NULL;
	if (name == NULL) {
		name = "";
	}

	if (string_equals(name, "reference")) {
		text = args_mcp_begin(&error);
	}
	else if (args_mcp_dir == NULL && args_mcp_begin(&error) == NULL) {
		// Begins with the first call when reference was skipped, error is set
	}
	else if (string_equals(name, "step")) {
		text = args_request(string("step\n%s", args_mcp_script("step", any_map_get(m, "params.arguments.code"))), 5000);
	}
	else if (string_equals(name, "run")) {
		char *script = any_map_get(m, "params.arguments.name");
		text         = args_request(string("run\n%s", script != NULL ? script : ""), 5000);
	}
	else if (string_equals(name, "restore")) {
		text = args_request("restore", 5000);
	}
	else if (string_equals(name, "play")) {
		char *seconds = any_map_get(m, "params.arguments.seconds");
		char *test    = any_map_get(m, "params.arguments.test");
		char *request = string("play\n%s", seconds != NULL ? seconds : "5");
		if (test != NULL) {
			request = string("%s\n%s", request, args_mcp_script("test", test));
		}
		text = args_request(request, 5000);
	}
	else {
		error = string("Unknown tool '%s'", name);
	}
	if (text == NULL && error == NULL) {
		error = "No response from ArmorPaint";
	}

	char *image = NULL;
	if (text != NULL) {
		char *mark = "Screenshot saved to '";
		i32   i    = string_last_index_of(text, mark);
		if (i >= 0) {
			i += string_length(mark);
			i32    end  = string_index_of_pos(text, "'", i);
			size_t size = 0;
			char  *png  = end > i ? args_read_file(substring(text, i, end), &size) : NULL;
			if (png != NULL) {
				image = args_base64((unsigned char *)png, size);
			}
		}
	}

	json_encode_begin_object_key("result");
	json_encode_begin_array("content");
	json_encode_begin_object();
	json_encode_string("type", "text");
	json_encode_string("text", text != NULL ? text : error);
	json_encode_end_object();
	if (image != NULL) {
		json_encode_begin_object();
		json_encode_string("type", "image");
		json_encode_string("data", image);
		json_encode_string("mimeType", "image/png");
		json_encode_end_object();
	}
	json_encode_end_array();
	json_encode_bool("isError", error != NULL);
	json_encode_end_object();
}

static void args_mcp_prop(char *name, char *type, char *description) {
	json_encode_begin_object_key(name);
	json_encode_string("type", type);
	json_encode_string("description", description);
	json_encode_end_object();
}

static void args_mcp_tool_begin(char *name, char *description) {
	json_encode_begin_object();
	json_encode_string("name", name);
	json_encode_string("description", description);
	json_encode_begin_object_key("inputSchema");
	json_encode_string("type", "object");
	json_encode_begin_object_key("properties");
}

static void args_mcp_tool_end(char *required) {
	json_encode_end_object(); // properties
	if (required != NULL) {
		json_encode_string_array("required", any_array_create_from_raw((void *[]){required}, 1));
	}
	json_encode_end_object(); // inputSchema
	json_encode_end_object();
}

static void args_mcp_list(void) {
	json_encode_begin_object_key("result");
	json_encode_begin_array("tools");
	args_mcp_tool_begin("reference", "Call first. Starts a session from the project open in ArmorPaint and returns how to "
	                                 "work in steps and where the C scripting API reference is.");
	args_mcp_tool_end(NULL);
	args_mcp_tool_begin("step", "Runs a C script step that modifies the project. Returns the log and a screenshot. A "
	                            "failed step is rolled back, successful steps stack.");
	args_mcp_prop("code", "string", "C source with a 'void main()' function");
	args_mcp_tool_end("code");
	args_mcp_tool_begin("run", "Runs a task script from the Scripts tab like a step.");
	args_mcp_prop("name", "string", "Script name, such as 'scripts/bake.c'");
	args_mcp_tool_end("name");
	args_mcp_tool_begin("restore", "Restores the project to its state when the session began, drops all steps.");
	args_mcp_tool_end(NULL);
	args_mcp_tool_begin("play", "Plays the project, returns the log and a screenshot, then rolls the project back.");
	args_mcp_prop("seconds", "number", "How long to play, at most 30");
	args_mcp_prop("test", "string", "Optional C test script that runs alongside the game");
	args_mcp_tool_end("seconds");
	json_encode_end_array();
	json_encode_end_object();
}

void args_mcp() {
	// Model context protocol server over stdio
	bool mcp = false;
	for (i32 i = 1; i < iron_get_arg_count(); ++i) {
		mcp = mcp || string_equals(iron_get_arg(i), "--mcp");
	}
	if (!mcp) {
		return;
	}
	char *dir = string("%sagent", iron_internal_save_path());
	if (!iron_is_directory(dir)) {
		iron_create_directory(dir);
	}

	char *line;
	while ((line = args_read_line()) != NULL) {
		any_map_t *m      = json_parse_to_map(line);
		char      *method = any_map_get(m, "method");
		char      *id     = any_map_get(m, "id");
		if (method == NULL || id == NULL) {
			continue; // Notifications
		}

		json_encode_begin();
		json_encode_string("jsonrpc", "2.0");
		// The map keeps no types, digits are a number id
		bool number = string_length(id) > 0;
		for (i32 i = 0; id[i] != '\0'; ++i) {
			number = number && ((id[i] >= '0' && id[i] <= '9') || (i == 0 && id[i] == '-'));
		}
		number ? json_encode_i32("id", atoi(id)) : json_encode_string("id", id);

		if (string_equals(method, "initialize")) {
			char *version = any_map_get(m, "params.protocolVersion");
			json_encode_begin_object_key("result");
			json_encode_string("protocolVersion", version != NULL ? version : "2025-06-18");
			json_encode_begin_object_key("capabilities");
			json_encode_begin_object_key("tools");
			json_encode_end_object();
			json_encode_end_object();
			json_encode_begin_object_key("serverInfo");
			json_encode_string("name", "armorpaint");
			json_encode_string("version", manifest_version);
			json_encode_end_object();
			json_encode_end_object();
		}
		else if (string_equals(method, "tools/list")) {
			args_mcp_list();
		}
		else if (string_equals(method, "tools/call")) {
			args_mcp_call(m);
		}
		else if (string_equals(method, "ping")) {
			json_encode_begin_object_key("result");
			json_encode_end_object();
		}
		else {
			json_encode_begin_object_key("error");
			json_encode_i32("code", -32601);
			json_encode_string("message", string("Method not found: %s", method));
			json_encode_end_object();
		}

		fputs(json_encode_end(), stdout);
		fputc('\n', stdout);
		fflush(stdout);
	}
	exit(0);
}

#endif

void args_parse() {
	if (iron_get_arg_count() > 1) {
		args_use = true;

		i32 i = 1;
		while (i < iron_get_arg_count()) {
			// Process each arg
			char *current_arg = iron_get_arg(i);

			if (path_is_project(current_arg)) {
				g_project->_->filepath = args_path(current_arg);
			}
			else if (string_equals(current_arg, "--background")) {
				args_background = true;
			}
			else if (string_equals(current_arg, "--player")) {
				args_player = true;
			}
			else if (path_is_texture(current_arg)) {
				args_asset_path = args_path(current_arg);
			}
			else if (string_equals(current_arg, "--export-textures") && (i + 3) < iron_get_arg_count()) {
				args_export_textures = true;
				++i;
				args_export_textures_type = string_copy(iron_get_arg(i));
				++i;
				args_export_textures_preset = string_copy(iron_get_arg(i));
				++i;
				args_export_textures_path = args_path(iron_get_arg(i));
			}
			else if (string_equals(current_arg, "--reload-mesh")) {
				args_reimport_mesh = true;
			}
			else if (string_equals(current_arg, "--export-mesh") && (i + 1) < iron_get_arg_count()) {
				args_export_mesh = true;
				++i;
				args_export_mesh_path = args_path(iron_get_arg(i));
			}
			else if (path_is_mesh(current_arg) || iron_is_directory(current_arg)) {
				args_asset_path = args_path(current_arg);
			}
			else if (string_equals(current_arg, "--export-material") && (i + 1) < iron_get_arg_count()) {
				args_export_material = true;
				++i;
				args_export_material_path = args_path(iron_get_arg(i));
			}
			else if (string_equals(current_arg, "--script") && (i + 1) < iron_get_arg_count()) {
				args_script = true;
				++i;
				args_script_path = args_path(iron_get_arg(i));
			}
			else if (string_equals(current_arg, "--api")) {
				args_api        = true;
				args_background = true;
			}
			else if (string_equals(current_arg, "--help")) {
				printf("Usage: armorpaint [options] [file]\n");
				printf("Options:\n");
				printf("  --background                      Run without displaying the window\n");
				printf("  --export-textures <type> <preset> <path>\n");
				printf("                                    Export textures to path\n");
				printf("                                    type: png, jpg, exr16, exr32\n");
				printf("  --export-mesh <path>              Export mesh to path\n");
				printf("  --export-material <path>          Export material to path\n");
				printf("  --reload-mesh                     Reimport mesh on startup\n");
				printf("  --script <path>                   Run script on the opened project, or a script from its Scripts tab by name\n");
				printf("  --mcp                             Serve the open instance to agents over the model context protocol (stdio)\n");
				printf("  --api                             Print the scripting API reference\n");
				printf("                                    Contents of the opened project are included\n");
				printf("  --player                          Run in player mode\n");
				printf("  --help                            Show this help message\n");
				exit(1);
			}
			++i;
		}
	}
}

void args_run_export_queue(void *_) {
	export_texture_run(args_export_textures_path, false);
}

void args_run_api(void *_) {
	printf("%s", agent_reference());
	iron_stop();
}

static void args_run_script_quit(void *_) {
	iron_stop();
}

void args_run_script_stop(void *_) {
	if (script_is_running()) {
		sys_notify_on_next_frame(&args_run_script_stop, NULL);
		return;
	}
	sys_notify_on_next_frame(&args_run_script_quit, NULL);
}

void args_run_script(void *_) {
	char *data = NULL;
	if (!iron_file_exists(args_script_path) && starts_with(args_script_path, "./")) {
		data = tab_scripts_find(substring(args_script_path, 2, string_length(args_script_path)));
	}
	if (data != NULL) {
		minic_eval(data);
	}
	else {
		buffer_t *b = iron_load_blob(args_script_path);
		if (b == NULL) {
			iron_log(tr("Invalid script path"));
		}
		else {
			minic_eval(sys_buffer_to_string(b));
			iron_delete_blob(b);
		}
	}
	if (args_background) {
		sys_notify_on_next_frame(&args_run_script_stop, NULL);
	}
}

void args_run_on_next_frame(void *_) {
	if (!string_equals(g_project->_->filepath, "")) {
		import_arm_run_project(g_project->_->filepath);
	}
	else if (!string_equals(args_asset_path, "")) {
		import_asset_run(args_asset_path, -1, -1, false, true, NULL);
		if (path_is_texture(args_asset_path)) {
			ui_base_show_2d_view(VIEW_2D_TYPE_ASSET);
		}
	}
	else if (args_reimport_mesh) {
		project_reimport_mesh();
	}

	if (args_export_textures) {
		if (!path_is_folder(args_export_textures_path)) {
			iron_log(tr("Invalid export directory"));
		}

		if (string_equals(args_export_textures_type, "png")) {
			base_bits              = TEXTURE_BITS_BITS8;
			g_context->format_type = TEXTURE_LDR_FORMAT_PNG;
		}
		else if (string_equals(args_export_textures_type, "jpg")) {
			base_bits              = TEXTURE_BITS_BITS8;
			g_context->format_type = TEXTURE_LDR_FORMAT_JPG;
		}
		else if (string_equals(args_export_textures_type, "exr16")) {
			base_bits = TEXTURE_BITS_BITS16;
		}
		else if (string_equals(args_export_textures_type, "exr32")) {
			base_bits = TEXTURE_BITS_BITS32;
		}
		else {
			iron_log(tr("Invalid texture type"));
		}

		g_context->layers_export = EXPORT_MODE_VISIBLE;

		// Get export preset and apply the correct one from args
		box_export_files = file_read_directory(string("%s%sexport_presets", path_data(), PATH_SEP));
		for (i32 i = 0; i < box_export_files->length; ++i) {
			char *s                     = box_export_files->buffer[i];
			box_export_files->buffer[i] = substring(s, 0, string_length(s) - 5); // Strip .json
		}

		char *file = string("export_presets/%s.json", box_export_files->buffer[0]);
		for (i32 i = 0; i < box_export_files->length; ++i) {
			char *f = box_export_files->buffer[i];
			if (string_equals(f, args_export_textures_preset)) {
				file = string("export_presets/%s.json", box_export_files->buffer[array_index_of(box_export_files, f)]);
			}
		}

		buffer_t *blob    = data_get_blob(file);
		box_export_preset = json_parse(sys_buffer_to_string(blob));
		data_delete_blob(file);

		// Export queue
		sys_notify_on_next_frame(&args_run_export_queue, NULL);
	}
	else if (args_export_mesh) {
		if (!path_is_folder(args_export_mesh_path)) {
			iron_log(tr("Invalid export directory"));
		}

		char *f = ui_files_filename;
		if (string_equals(f, "")) {
			f = string_copy(tr("untitled"));
		}
		export_mesh_run(string("%s%s%s", args_export_mesh_path, PATH_SEP, f), NULL, true);
	}
	else if (args_export_material) {
		g_context->write_icon_on_export = true;
		export_arm_run_material(args_export_material_path);
	}

	if (args_api) {
		sys_notify_on_next_frame(&args_run_api, NULL);
	}
	else if (args_script) {
		sys_notify_on_next_frame(&args_run_script, NULL);
	}
	else if (args_background) {
		iron_stop();
	}
}

void args_run() {
	if (args_use) {
		sys_notify_on_next_frame(&args_run_on_next_frame, NULL);
	}
}
