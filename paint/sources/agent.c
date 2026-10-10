
#include "global.h"
#if !defined(IRON_WINDOWS) && !defined(IRON_WASM)
#include <unistd.h>
#endif

static i32  agent_backend = CONSOLE_MODEL_QWEN;
static bool agent_console = false;
static bool agent_mcp     = false;

#define AGENT_RULES                                  \
	"Place the code inside 'void main()' function. " \
	"Do not use preprocessor. "                      \
	"Do not use multi-dimensional arrays. "          \
	"Use 'char *string(fmt, ...)' to format strings.\n"

#define AGENT_SCRIPTS                                                                                                    \
	"A failed step is rolled back, successful steps stack. Scripts that belong to the project, such as a game, are not " \
	"steps, they are stored in the Scripts tab and 'main.c' runs when the project is played. Repeatable work, such as "  \
	"rebaking the lightmap, belongs to a Scripts tab: it does not run when playing.\n"

static char *agent_code_guide = "Reply with C code only in a ```c fence. " AGENT_RULES;

static char *agent_qwen_guide = "Edit the ArmorPaint project in steps, each a C script that modifies the project. Reply with the "
                                "next step only, in a ```c fence. " AGENT_RULES AGENT_SCRIPTS
                                "To save a project script, start its fence "
                                "with ```c scripts/<name>.c instead. To run a task script as the next step, reply 'Run: scripts/<name>.c' "
                                "only. When finished, reply 'Done.'.\n";

static char *agent_prompt = NULL;
static char *agent_dir    = NULL;

#define AGENT_QWEN_MAX_STEPS 8
static char *agent_qwen_reference = NULL;
static char *agent_qwen_history   = NULL;
static char *agent_qwen_code      = NULL;
static i32   agent_qwen_step      = 0;

static char *agent_shapes(void) {
	string_array_t *shapes = script_shape_list();
	if (shapes == NULL || shapes->length == 0 || string_equals(shapes->buffer[0], "")) {
		return "";
	}

	buffer_t sb;
	string_buffer_init(&sb);
	string_buffer_append(&sb, "\nscript_shape_add() shapes:\n");
	for (i32 i = 0; i < shapes->length; ++i) {
		string_buffer_append(&sb, string("\"%s\"\n", shapes->buffer[i]));
	}

	char *result = string_copy(string_buffer_get(&sb));
	string_buffer_free(&sb);
	return result;
}

static char *agent_scene_bounds(void) {
	if (g_project == NULL || g_project->_ == NULL || g_project->_->paint_objects == NULL) {
		return "";
	}

	buffer_t sb;
	string_buffer_init(&sb);
	string_buffer_append(&sb, "\nScene objects in world space, z axis up:\n");

	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *o = g_project->_->paint_objects->buffer[i];
		if (o->data == NULL) {
			continue;
		}

		vec4_t local_min;
		vec4_t local_max;
		mesh_data_calculate_aabb_min_max(o->data, &local_min, &local_max);

		transform_t *t = o->base->transform;
		transform_update(t);
		vec4_t min = {0.0, 0.0, 0.0, 0.0};
		vec4_t max = {0.0, 0.0, 0.0, 0.0};
		for (i32 c = 0; c < 8; ++c) {
			vec4_t p;
			p.x = (c & 1) ? local_max.x : local_min.x;
			p.y = (c & 2) ? local_max.y : local_min.y;
			p.z = (c & 4) ? local_max.z : local_min.z;
			p.w = 1.0;
			p   = vec4_apply_mat4(p, t->world);
			if (c == 0) {
				min = p;
				max = p;
				continue;
			}
			min.x = p.x < min.x ? p.x : min.x;
			min.y = p.y < min.y ? p.y : min.y;
			min.z = p.z < min.z ? p.z : min.z;
			max.x = p.x > max.x ? p.x : max.x;
			max.y = p.y > max.y ? p.y : max.y;
			max.z = p.z > max.z ? p.z : max.z;
		}

		char *parent = o->base->parent != NULL ? string(", parent \"%s\"", o->base->parent->name) : "";
		string_buffer_append(&sb, string("\"%s\": location (%.3f, %.3f, %.3f), size (%.3f, %.3f, %.3f), "
		                                 "bounds min (%.3f, %.3f, %.3f) max (%.3f, %.3f, %.3f)%s%s\n",
		                                 o->base->name, t->loc.x, t->loc.y, t->loc.z, max.x - min.x, max.y - min.y, max.z - min.z, min.x, min.y, min.z, max.x,
		                                 max.y, max.z, parent, o->base->visible ? "" : ", hidden"));
	}

	char *result = string_copy(string_buffer_get(&sb));
	string_buffer_free(&sb);
	return result;
}

static char *agent_project_json(bool scripts) {
	swatch_color_t_array_t *swatches = g_project->swatches;
	string_array_t         *datas    = g_project->script_datas;
	g_project->swatches              = NULL;
	g_project->script_datas          = scripts ? datas : NULL;
	buffer_t *encoded                = util_encode_project(g_project);
	g_project->swatches              = swatches;
	g_project->script_datas          = datas;
	char *json                       = armpack_decode_to_json_omit_large_arrays(encoded);
	array_free(encoded);
	free(encoded);
	return json;
}

static char *agent_project_contents(void) {
	return string("/* Current project state:\n%s\n%s%s*/\n", agent_project_json(true), agent_scene_bounds(), agent_shapes());
}

static void agent_json_indent(buffer_t *sb, i32 depth) {
	string_buffer_append(sb, "\n");
	for (i32 i = 0; i < depth; ++i) {
		string_buffer_append(sb, "\t");
	}
}

static char *agent_json_pretty(char *json) {
	buffer_t sb;
	string_buffer_init(&sb);
	i32  depth   = 0;
	i32  inline_ = 0; // Nesting of the array kept on one line
	bool str     = false;
	char c[2]    = {0, 0};
	for (i32 i = 0; json[i] != '\0'; ++i) {
		c[0] = json[i];
		string_buffer_append(&sb, c);
		if (str) {
			if (c[0] == '\\' && json[i + 1] != '\0') {
				c[0] = json[++i];
				string_buffer_append(&sb, c);
				continue;
			}
			if (c[0] != '"') {
				continue;
			}
			str = false;
		}
		else if (c[0] == '"') {
			str = true;
			continue;
		}
		else if (inline_ > 0) {
			inline_ += c[0] == '[' ? 1 : c[0] == ']' ? -1 : 0;
		}
		else if (c[0] == '[' && json[i + 1] != '{' && json[i + 1] != '[' && json[i + 1] != '"') {
			inline_ = 1;
		}
		else if (c[0] == '{' && json[i + 1] == '}') {
			string_buffer_append(&sb, "}");
			++i;
		}
		else if (c[0] == '{' || c[0] == '[') {
			agent_json_indent(&sb, ++depth);
		}
		else if (c[0] == ',') {
			agent_json_indent(&sb, depth);
		}
		else if (c[0] == ':') {
			string_buffer_append(&sb, " ");
		}
		if (inline_ == 0 && (json[i + 1] == '}' || json[i + 1] == ']')) {
			agent_json_indent(&sb, --depth);
		}
	}
	char *result = string_copy(string_buffer_get(&sb));
	string_buffer_free(&sb);
	return result;
}

static void agent_write_sockets(buffer_t *sb, char *label, ui_node_socket_t_array_t *sockets) {
	if (sockets == NULL || sockets->length == 0) {
		return;
	}
	string_buffer_append(sb, string(" | %s:", label));
	for (i32 i = 0; i < sockets->length; ++i) {
		string_buffer_append(sb, string("%s %d %s", i > 0 ? "," : "", i, sockets->buffer[i]->name));
	}
}

static void agent_write_node(buffer_t *sb, ui_node_t *n) {
	string_buffer_append(sb, string("// %s", n->type));
	agent_write_sockets(sb, "in", n->inputs);
	agent_write_sockets(sb, "out", n->outputs);
	string_buffer_append(sb, "\n");

	for (i32 i = 0; i < n->buttons->length; ++i) {
		ui_node_button_t *b = n->buttons->buffer[i];
		string_buffer_append(sb, string("//     button %d %s %s", i, b->name, b->type));
		if (string_equals(b->type, "ENUM") && b->data != NULL) {
			any_array_t *options = string_split(u8_array_to_string(b->data), "\n");
			for (i32 j = 0; j < options->length; ++j) {
				string_buffer_append(sb, string("%s %d %s", j > 0 ? "," : ":", j, (char *)options->buffer[j]));
			}
		}
		string_buffer_append(sb, "\n");
	}

	if (string_equals(n->type, "SHADER_GPU")) {
		string_buffer_append(sb, shader_node_reference());
	}
}

static char *agent_nodes_reference(void) {
	buffer_t sb;
	string_buffer_init(&sb);

	nodes_material_init();

	string_buffer_append(&sb, "// Material nodes reference:\n");

	for (i32 i = 0; i < nodes_material_list->length; ++i) {
		ui_node_t_array_t *c = (ui_node_t_array_t *)nodes_material_list->buffer[i];
		for (i32 j = 0; j < c->length; ++j) {
			agent_write_node(&sb, c->buffer[j]);
		}
	}

	ui_node_canvas_t *canvas = g_context != NULL && g_context->material != NULL ? g_context->material->canvas : NULL;
	if (canvas != NULL) {
		for (i32 i = 0; i < canvas->nodes->length; ++i) {
			if (string_equals(canvas->nodes->buffer[i]->type, "OUTPUT_MATERIAL_PBR")) {
				string_buffer_append(&sb, "//\n// Pre-created material output node:\n");
				agent_write_node(&sb, canvas->nodes->buffer[i]);
				break;
			}
		}
	}

	char *result = string_copy(string_buffer_get(&sb));
	string_buffer_free(&sb);
	return result;
}

static string_array_t *agent_qwen_args(char *dir) {
	char           *prompt_file = string("%s%sprompt.txt", dir, PATH_SEP);
	string_array_t *argv        = any_array_create_from_raw(
        (void *[]){
            string("%s/%s", dir, neural_node_llama_bin()),
            "-m",
            string("%s/Qwen3.8-27B-UD-Q4_K_M.gguf", dir),
            "-ngl",
            "99",
            "-c",
            "65536",
            "--temp",
            "1.0",
            "--top-p",
            "0.95",
            "--top-k",
            "20",
            "--min-p",
            "0.0",
            "--single-turn",
            "--file",
            prompt_file,
            NULL,
        },
        19);
	return argv;
}

static char *agent_exe(void) {
#ifdef IRON_WINDOWS
	char    path[4096];
	wchar_t wpath[4096];
	if (GetModuleFileNameW(NULL, wpath, 4096) > 0 && WideCharToMultiByte(CP_UTF8, 0, wpath, -1, path, sizeof(path), NULL, NULL) > 0) {
		return string_replace_all(path, "\\", "/");
	}
#elif !defined(IRON_WASM)
	// A bare name was found through PATH and resolves the same from any directory
	char path[4096];
	char *exe = iron_get_arg(0);
	if (exe[0] != '/' && string_index_of(exe, "/") >= 0 && getcwd(path, sizeof(path)) != NULL) {
		return string("%s/%s", path, exe);
	}
#endif
	return string_replace_all(iron_get_arg(0), "\\", "/");
}

static char *agent_mcp_guide(char *dir) {
	return string("Edit the ArmorPaint project '%s' in steps with the armorpaint tools, each step a C script that modifies the "
	              "project. " AGENT_RULES AGENT_SCRIPTS
	              "Run a step with the step tool, it returns the log and a screenshot. The step files are kept in '%s'.\n"
	              "Start over from the original project with the restore tool, then replay the steps to keep.\n"
	              "Project scripts are '%s/scripts/<name>.c', edit them as files. Run a task script like a step with the run tool.\n"
	              "Test by playing with the play tool. It returns the log and a screenshot and rolls the project back. The test "
	              "script runs alongside the game, it can call script_screenshot(path) from script_timer() callbacks and must "
	              "not call script_notify_on_update() or script_notify_on_next_frame().\n",
	              g_project->_->filepath, dir, dir);
}

static char *agent_context(char *guide) {
	return string("%s\n%s\n%s\n%s\n", minic_api_header_generate(), agent_nodes_reference(), agent_project_contents(), guide);
}

static char *agent_result_path(void) {
	return string("%s%sresult.txt", neural_node_dir(), PATH_SEP);
}

static void agent_ensure_dir(char *dir) {
	if (string_equals(file_read_directory(dir)->buffer[0], "")) {
		iron_create_directory(dir);
	}
}

static char *agent_project_dir(void) {
	char *dir = path_base_dir(g_project->_->filepath);
	return substring(dir, 0, string_length(dir) - 1);
}

static char *agent_work_dir(void) {
	return string("%s%s%s.work", agent_project_dir(), PATH_SEP, path_base_name(g_project->_->filepath));
}

static char *agent_serve_file(char *name) {
	return string("%sagent%s%s", iron_internal_save_path(), PATH_SEP, name);
}

static string_array_t *agent_claude_args(char *dir, char *prompt) {
	string_array_t *argv = any_array_create_from_raw(
	    (void *[]){
	        "claude",
	        "--print",
	        "--output-format",
	        "text",
	        "--tools",
	        "Read,Write,Edit",
	        "--permission-mode",
	        "acceptEdits",
	        "--add-dir",
	        agent_project_dir(),
	        "--mcp-config",
	        string("{\"mcpServers\":{\"armorpaint\":{\"command\":\"%s\",\"args\":[\"--mcp\"]}}}", agent_exe()),
	        "--strict-mcp-config",
	        "--allowedTools",
	        "mcp__armorpaint",
	        "--append-system-prompt-file",
	        string("%s%sAGENTS.md", dir, PATH_SEP),
	        prompt,
	        NULL,
	    },
	    19);
	return argv;
}

static string_array_t *agent_grok_args(char *dir, char *prompt) {
	string_array_t *argv = any_array_create_from_raw(
	    (void *[]){
	        "grok",
	        "--single",
	        prompt,
	        "--output-format",
	        "plain",
	        "--cwd",
	        dir,
	        "--disable-web-search",
	        "--trust",
	        "--allow",
	        string("Write(%s/**)", agent_project_dir()),
	        "--allow",
	        string("Edit(%s/**)", agent_project_dir()),
	        "--allow",
	        "mcp__armorpaint",
	        NULL,
	    },
	    16);
	return argv;
}

static string_array_t *agent_codex_args(char *dir, char *prompt) {
	string_array_t *argv = any_array_create_from_raw(
	    (void *[]){
	        "codex",
	        "exec",
	        "--skip-git-repo-check",
	        "--sandbox",
	        "workspace-write",
	        "--color",
	        "never",
	        "--cd",
	        dir, // Pick up AGENTS.md
	        "--add-dir",
	        agent_project_dir(),
	        "--output-last-message",
	        agent_result_path(),
	        "-c",
	        string("mcp_servers.armorpaint.command=\"%s\"", agent_exe()),
	        "-c",
	        "mcp_servers.armorpaint.args=[\"--mcp\"]",
	        prompt,
	        NULL,
	    },
	    19);
	return argv;
}

static char *agent_read_result(void) {
	char *file = agent_result_path();
	if (!iron_file_exists(file)) {
		return NULL;
	}
	buffer_t *b = iron_load_blob(file);
	char     *s = sys_buffer_to_string(b);

	if (agent_backend == CONSOLE_MODEL_QWEN) {
		i32 think_end = string_last_index_of(s, "</think>\n\n");
		i32 eot       = string_last_index_of(s, "[end of text]");
		if (think_end >= 0 && eot > think_end) {
			s = substring(s, think_end + 10, eot);
		}
	}
	return trim_end(s);
}

static char *agent_file(char *name) {
	return string("%s%s%s", agent_dir, PATH_SEP, name);
}

static char *agent_reference_write(void) {
	char *api     = agent_file("api.h");
	char *project = agent_file("project.txt");
	iron_file_save_bytes(api, sys_string_to_buffer(string("%s\n%s", minic_api_header_generate(), agent_nodes_reference())), 0);
	char *state = string("Current project state, script_datas are the files in '%s':\n%s\n%s%s", agent_file("scripts"),
	                     agent_json_pretty(agent_project_json(false)), agent_scene_bounds(), agent_shapes());
	iron_file_save_bytes(project, sys_string_to_buffer(state), 0);
	return string("The C scripting API and the material nodes are in '%s', read it before the first step. The project state, as json with "
	              "the scene objects, is in '%s', read or search the parts you need.\n",
	              api, project);
}

static void agent_scripts_write(void) {
	char *dir = agent_file("scripts");
	agent_ensure_dir(dir);
	tab_scripts_prepare();
	for (i32 i = 0; i < g_project->script_names->length; ++i) {
		char *file = string("%s%s%s", dir, PATH_SEP, g_project->script_names->buffer[i]);
		iron_file_save_bytes(file, sys_string_to_buffer(g_project->script_datas->buffer[i]), 0);
	}
}

static void agent_scripts_sync(void) {
	char           *dir   = agent_file("scripts");
	string_array_t *files = file_read_directory(dir);
	tab_scripts_prepare();
	for (i32 i = 0; i < files->length; ++i) {
		char *name = files->buffer[i];
		if (!ends_with(name, ".c")) {
			continue;
		}
		buffer_t *b = iron_load_blob(string("%s%s%s", dir, PATH_SEP, name));
		if (b == NULL) {
			continue;
		}
		char *data = string_copy(sys_buffer_to_string(b));
		iron_delete_blob(b);

		i32 j = string_array_index_of(g_project->script_names, name);
		if (j < 0) {
			string_array_push(g_project->script_names, string_copy(name));
			string_array_push(g_project->script_datas, data);
			j = g_project->script_names->length - 1;
		}
		else {
			g_project->script_datas->buffer[j] = data;
		}
		// The player runs the first script
		if (string_equals(name, "main.c") && j > 0) {
			array_splice(g_project->script_names, j, 1);
			array_splice(g_project->script_datas, j, 1);
			array_insert(g_project->script_names, 0, string_copy(name));
			array_insert(g_project->script_datas, 0, data);
		}
	}
	tab_scripts_minimap_dirty                         = true;
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
}

static void agent_finish(char *s) {
	agent_scripts_sync();
	if (s == NULL || string_equals(s, "")) {
		console_error(tr("No response"));
	}
	else {
		string_array_t *lines = string_split(s, "\n");
		for (i32 i = 0; i < lines->length; ++i) {
			console_log(lines->buffer[i]);
		}
	}
	agent_console = false;
	agent_running = false;
	base_redraw_console();
}

static string_array_t *agent_steps      = NULL;
static void (*agent_eval_done)(bool ok) = NULL;
static bool      agent_eval_ok          = false;
static f32       agent_eval_start       = 0.0;
static const f32 agent_eval_timeout     = 60.0;

static void agent_eval_finish(void *_) {
	agent_eval_done(agent_eval_ok);
}

static void agent_eval_wait(void *_) {
	iron_delay_idle_sleep();
	bool timed_out = sys_time() - agent_eval_start > agent_eval_timeout;
	if (script_is_running() && !timed_out) {
		return;
	}
	sys_remove_update(agent_eval_wait);
	if (timed_out) {
		script_stop();
		console_log("Script timed out");
	}
	agent_eval_ok = !timed_out && minic_error_count == 0;
	sys_notify_on_next_frame(agent_eval_finish, NULL);
}

static void agent_eval(char *path, void (*done)(bool ok)) {
	agent_eval_done   = done;
	minic_error_count = 0;
	buffer_t *b       = iron_load_blob(path);
	if (b == NULL) {
		console_log(string("Could not load '%s'", path));
		minic_error_count++;
	}
	else {
		minic_eval(sys_buffer_to_string(b));
		iron_delete_blob(b);
	}
	agent_eval_start = sys_time();
	sys_notify_on_update(agent_eval_wait, NULL);
}

static void (*agent_restore_done)(void) = NULL;
static char *agent_restore_capture      = NULL;
static i32   agent_reload_frames        = 0;
static i32   agent_replay_index         = 0;

static void agent_replay_next(void *_);

static void agent_replay_evaluated(bool ok) {
	if (!ok) {
		i32 i                 = --agent_replay_index;
		agent_restore_capture = string_copy(string("%sReplaying '%s' failed, the step was dropped\n", agent_restore_capture, agent_steps->buffer[i]));
		array_splice(agent_steps, i, 1);
	}
	sys_notify_on_next_frame(agent_replay_next, NULL);
}

static void agent_replay_next(void *_) {
	if (agent_replay_index >= agent_steps->length) {
		agent_scripts_sync();
		console_capture = agent_restore_capture;
		agent_restore_done();
		return;
	}
	agent_eval(agent_steps->buffer[agent_replay_index++], agent_replay_evaluated);
}

static void agent_restore_reload(void *_) {
	iron_delay_idle_sleep();
	if (--agent_reload_frames > 0) {
		sys_notify_on_next_frame(agent_restore_reload, NULL);
		return;
	}
	import_arm_keep_script_trust = true;
	import_arm_run_project(g_project->_->filepath);
	player_runtime_capture_objects();
	player_runtime_restore_context();
	agent_replay_index = 0;
	sys_notify_on_next_frame(agent_replay_next, NULL);
}

static void agent_restore(void (*done)(void)) {
	agent_restore_done    = done;
	agent_restore_capture = console_capture != NULL ? console_capture : "";
	console_capture       = NULL;
	player_runtime_reset();
	agent_reload_frames = GPU_FRAMEBUFFER_COUNT + 1;
	sys_notify_on_next_frame(agent_restore_reload, NULL);
}

void agent_draw_overlay(void) {
	draw_set_color(0x55000000);
	draw_filled_rect(0, 0, iron_window_width(), iron_window_height());
	draw_set_color(0xffffffff);
}

static char *agent_step_path              = NULL;
static bool  agent_step_shot              = false;
static char *agent_step_shot_path         = NULL;
static void (*agent_step_done)(char *log) = NULL;

static void agent_step_finish(void) {
	char *log       = console_capture;
	console_capture = NULL;
	if (agent_step_shot) {
		log = string("%sScreenshot saved to '%s'\n", log, agent_step_shot_path);
	}
	agent_step_done(log);
}

static void agent_step_after(void) {
	if (!agent_step_shot) {
		agent_step_finish();
		return;
	}
	char *path = agent_step_path;
	if (ends_with(path, ".c")) {
		path = substring(path, 0, string_length(path) - 2);
	}
	agent_step_shot_path = string_copy(string("%s.png", path));
	script_screenshot_queue(agent_step_shot_path, 8, agent_step_finish);
}

static void agent_step_evaluated(bool ok) {
	if (ok) {
		any_array_push(agent_steps, agent_step_path);
		agent_step_after();
	}
	else {
		console_log("Step failed, the project was restored to the state before this step");
		agent_restore(agent_step_after);
	}
}

static void agent_step_run(char *path, bool screenshot, void (*done)(char *log)) {
	agent_step_path = string_copy(path);
	agent_step_shot = screenshot;
	agent_step_done = done;
	console_log(string("Running %s", path));
	console_capture = "";
	agent_eval(agent_step_path, agent_step_evaluated);
}

static i32   agent_play_count             = 0;
static f32   agent_play_start             = 0.0;
static f32   agent_play_seconds           = 0.0;
static char *agent_play_shot              = NULL;
static void (*agent_play_done)(char *log) = NULL;

static void agent_play_restored(void) {
	char *log       = console_capture;
	console_capture = NULL;
	agent_play_done(string("%sScreenshot saved to '%s', the project was restored to the state before playing\n", log, agent_play_shot));
}

static void agent_play_stop(void) {
	agent_restore(agent_play_restored);
}

static void agent_play_wait(void *_) {
	iron_delay_idle_sleep();
	if (sys_time() - agent_play_start < agent_play_seconds && minic_error_count == 0) {
		return;
	}
	sys_remove_update(agent_play_wait);
	if (minic_error_count > 0) {
		console_log("Stopped on error");
	}
	script_screenshot_queue(agent_play_shot, 1, agent_play_stop);
}

static void agent_play(f32 seconds, char *test, void (*done)(char *log)) {
	agent_play_done    = done;
	agent_play_seconds = seconds < 30.0 ? seconds : 30.0;
	agent_play_shot    = agent_file(string("play%d.png", ++agent_play_count));
	console_log(string("Playing for %.1f seconds", agent_play_seconds));
	console_capture   = "";
	minic_error_count = 0;

	player_set_workspace(true);
	player_run_scripts();
	if (test != NULL) {
		buffer_t *b = iron_load_blob(test);
		if (b == NULL) {
			console_log(string("Could not load '%s'", test));
		}
		else {
			minic_eval(sys_buffer_to_string(b));
			iron_delete_blob(b);
		}
	}
	agent_play_start = sys_time();
	sys_notify_on_update(agent_play_wait, NULL);
}

static i32 agent_task_count = 0;

static char *agent_task_snapshot(char *name) {
	char *data = tab_scripts_find(name);
	if (data == NULL) {
		return NULL;
	}
	char *base = name;
	i32   i    = string_last_index_of(base, "/");
	if (i >= 0) {
		base = substring(base, i + 1, string_length(base));
	}
	if (ends_with(base, ".c")) {
		base = substring(base, 0, string_length(base) - 2);
	}
	char *script = agent_file(string("run%d_%s.c", ++agent_task_count, base));
	iron_file_save_bytes(script, sys_string_to_buffer(data), 0);
	return script;
}

static bool agent_serve_busy = false;
static bool agent_serve_lock = false;

static void agent_serve_respond(char *log) {
	char *tmp = agent_serve_file("response.tmp");
	iron_file_save_bytes(tmp, sys_string_to_buffer(log), 0);
	rename(tmp, agent_serve_file("response.txt"));
	agent_serve_busy = false;
	if (agent_serve_lock) {
		agent_serve_lock = false;
		agent_running    = false;
		base_redraw_console();
	}
}

static void agent_serve_restored(void) {
	char *log       = console_capture;
	console_capture = NULL;
	agent_serve_respond(string("%sProject restored to the original\n", log));
}

static void agent_serve_request(void *data) {
	string_array_t *request = string_split(data, "\n");
	char           *command = request->buffer[0];
	agent_scripts_sync();
	if (string_equals(command, "restore")) {
		agent_steps->length = 0;
		console_capture     = "";
		agent_restore(agent_serve_restored);
	}
	else if (string_equals(command, "play") && request->length > 1) {
		char *test = request->length > 2 ? request->buffer[2] : NULL;
		agent_play(atof(request->buffer[1]), test, agent_serve_respond);
	}
	else if (string_equals(command, "step") && request->length > 1) {
		agent_step_run(request->buffer[1], true, agent_serve_respond);
	}
	else if (string_equals(command, "run") && request->length > 1) {
		char *script = agent_task_snapshot(request->buffer[1]);
		if (script == NULL) {
			agent_serve_respond(string("No script '%s' in the Scripts tab\n", request->buffer[1]));
		}
		else {
			agent_step_run(script, true, agent_serve_respond);
		}
	}
	else {
		agent_serve_respond("Invalid request\n");
	}
}

static void agent_console_finish(void *_) {
	agent_finish(agent_read_result());
}

static void agent_console_wait(void *_) {
	iron_delay_idle_sleep();
	if (agent_serve_busy || iron_exec_async_done != 1) {
		return;
	}
	sys_remove_update(agent_console_wait);
	sys_notify_on_next_frame(agent_console_finish, NULL);
}

static void agent_qwen_turn(void);

static void agent_qwen_step_done(char *log) {
	agent_qwen_history = string_copy(string("%s\nStep %d:\n```c\n%s```\nLog:\n%s", agent_qwen_history, agent_qwen_step, agent_qwen_code, log));
	agent_qwen_turn();
}

static void agent_qwen_reply(void *_) {
	char *s      = agent_read_result();
	i32   i      = s != NULL ? string_index_of(s, "```c") : -1;
	i32   run    = s != NULL ? string_index_of(s, "Run: ") : -1;
	bool  is_run = run >= 0 && (i < 0 || run < i);
	if (!is_run && i < 0) {
		agent_finish(s);
		return;
	}
	if (agent_qwen_step >= AGENT_QWEN_MAX_STEPS) {
		agent_finish(tr("Step limit reached"));
		return;
	}
	agent_qwen_step++;

	// Task script
	if (is_run) {
		i32   end    = string_index_of_pos(s, "\n", run);
		char *name   = trim_end(substring(s, run + 5, end >= 0 ? end : string_length(s)));
		char *script = agent_task_snapshot(name);
		if (script == NULL) {
			agent_qwen_history =
			    string_copy(string("%s\nStep %d:\nRun: %s\nLog:\nNo script '%s' in the Scripts tab\n", agent_qwen_history, agent_qwen_step, name, name));
			agent_qwen_turn();
			return;
		}
		buffer_t *b     = iron_load_blob(script);
		agent_qwen_code = string_copy(sys_buffer_to_string(b));
		iron_delete_blob(b);
		agent_step_run(script, false, agent_qwen_step_done);
		return;
	}

	i32 start = string_index_of_pos(s, "\n", i);
	start     = start >= 0 ? start + 1 : i + 4;
	i32 end   = string_last_index_of(s, "```");
	if (end < start) {
		end = string_length(s);
	}
	char *header    = trim_end(substring(s, i + 4, start));
	agent_qwen_code = string_copy(substring(s, start, end));

	// Project script
	if (starts_with(header, " scripts/")) {
		char *name = substring(header, 9, string_length(header));
		iron_file_save_bytes(agent_file(string("scripts%s%s", PATH_SEP, name)), sys_string_to_buffer(agent_qwen_code), 0);
		agent_scripts_sync();
		console_log(string("Saved scripts/%s", name));
		agent_qwen_history =
		    string_copy(string("%s\nStep %d:\n```c scripts/%s\n%s```\nSaved to the Scripts tab\n", agent_qwen_history, agent_qwen_step, name, agent_qwen_code));
		agent_qwen_turn();
		return;
	}

	char *script = agent_file(string("step%d.c", agent_qwen_step));
	iron_file_save_bytes(script, sys_string_to_buffer(agent_qwen_code), 0);
	agent_step_run(script, false, agent_qwen_step_done);
}

static void agent_qwen_check(void *_) {
	iron_delay_idle_sleep();
	if (iron_exec_async_done == 1) {
		sys_remove_update(agent_qwen_check);
		sys_notify_on_next_frame(agent_qwen_reply, NULL);
	}
}

static void agent_qwen_turn(void) {
	char *dir  = neural_node_dir();
	char *full = string("%s\nRequest: %s\n%s", agent_qwen_reference, agent_prompt, agent_qwen_history);
	iron_file_save_bytes(string("%s%sprompt.txt", dir, PATH_SEP), sys_string_to_buffer(full), 0);
	string_array_t *argv = agent_qwen_args(dir);

	char *res = agent_result_path();
	iron_delete_file(res);
	iron_exec_async_output_file = res;
	iron_exec_async(argv->buffer[0], argv->buffer);
	iron_exec_async_output_file = NULL;
	sys_notify_on_update(agent_qwen_check, NULL);
}

static void agent_clear_dir(char *dir) {
	string_array_t *files = file_read_directory(dir);
	for (i32 i = 0; i < files->length; ++i) {
		char *file = string("%s%s%s", dir, PATH_SEP, files->buffer[i]);
		if (string_equals(files->buffer[i], "")) {
			continue;
		}
		if (iron_is_directory(file)) {
			agent_clear_dir(file); // Kept
			continue;
		}
		iron_delete_file(file);
	}
}

void agent_clear(void) {
	char *dir = neural_node_dir();
	iron_delete_file(string("%s%sprompt.txt", dir, PATH_SEP));
	iron_delete_file(agent_result_path());
	agent_clear_dir(string("%sagent", iron_internal_save_path()));
	if (!string_equals(g_project->_->filepath, "")) {
		agent_clear_dir(agent_work_dir());
	}
}

char *agent_reference(void) {
	return agent_context(agent_code_guide);
}

static void agent_session_begin(void) {
	export_arm_run_project(g_project->_->filepath);
	agent_ensure_dir(agent_dir);
	agent_clear_dir(agent_dir);
	agent_scripts_write();
	player_runtime_capture();
	agent_steps      = any_array_create(0);
	agent_play_count = 0;
	agent_task_count = 0;
}

static void agent_start(void *_) {
	char *serve_dir = string("%sagent", iron_internal_save_path());
	agent_ensure_dir(serve_dir);
	agent_clear_dir(serve_dir);
	agent_mcp = false;
	agent_session_begin();
	agent_running = true;
	char *dir     = agent_dir;

	if (agent_backend == CONSOLE_MODEL_QWEN) {
		agent_qwen_reference = string_copy(agent_context(agent_qwen_guide));
		agent_qwen_history   = "";
		agent_qwen_step      = 0;
		agent_qwen_turn();
		return;
	}

	string_array_t *argv;
	char           *guide = string("%sThe session is already begun, do not call the reference tool. When finished, reply 'Done.'.\n", agent_mcp_guide(dir));
	iron_file_save_bytes(agent_file("AGENTS.md"), sys_string_to_buffer(string("%s%s", guide, agent_reference_write())), 0);
	argv = agent_backend == CONSOLE_MODEL_CLAUDE ? agent_claude_args(dir, agent_prompt)
	       : agent_backend == CONSOLE_MODEL_GROK ? agent_grok_args(dir, agent_prompt)
	                                             : agent_codex_args(dir, agent_prompt);
	if (agent_backend == CONSOLE_MODEL_GROK) {
		agent_ensure_dir(agent_file(".grok"));
		char *toml = string("[mcp_servers.armorpaint]\ncommand = \"%s\"\nargs = [\"--mcp\"]\n", agent_exe());
		iron_file_save_bytes(agent_file(string(".grok%sconfig.toml", PATH_SEP)), sys_string_to_buffer(toml), 0);
	}
	agent_console = true;

	char *res = agent_result_path();
	iron_delete_file(res);
	iron_exec_async_output_file = agent_backend == CONSOLE_MODEL_CODEX ? NULL : res;
	iron_exec_async(argv->buffer[0], argv->buffer);
	iron_exec_async_output_file = NULL;
	sys_notify_on_update(agent_console_wait, NULL);
}

static void agent_mcp_request(void *data) {
	if (agent_running && !agent_console) {
		agent_serve_respond("\nArmorPaint is busy with a console agent run\n");
		return;
	}
	if (!agent_running) {
		agent_running    = true;
		agent_serve_lock = true;
	}
	if (!string_equals(data, "begin")) {
		if (agent_console || agent_mcp) {
			agent_serve_request(data);
		}
		else {
			agent_serve_respond("No session, call the reference tool to begin one\n");
		}
		return;
	}
	if (agent_console) {
		agent_serve_respond(string("%s\nThe session was begun by ArmorPaint, the reference is in your instructions.\n", agent_dir));
		return;
	}
	if (string_equals(g_project->_->filepath, "")) {
		agent_mcp = false;
		agent_serve_respond("\nSave the project in ArmorPaint first\n");
		return;
	}
	agent_dir = string_copy(agent_work_dir());
	agent_session_begin();
	agent_mcp = true;
	agent_serve_respond(string("%s\n%s%s", agent_dir, agent_mcp_guide(agent_dir), agent_reference_write()));
}

static bool agent_mcp_pending(void) {
	return iron_file_exists(agent_serve_file("request.txt"));
}

static void agent_listen(void *_) {
	if (agent_serve_busy || !agent_mcp_pending()) {
		return;
	}
	char     *request = agent_serve_file("request.txt");
	buffer_t *b       = iron_load_blob(request);
	char     *s       = string_copy(trim_end(sys_buffer_to_string(b)));
	iron_delete_blob(b);
	iron_delete_file(request);
	agent_serve_busy = true;
	iron_delay_idle_sleep();
	sys_notify_on_next_frame(agent_mcp_request, s);
}

void agent_init(void) {
#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS)
	sys_notify_on_update(agent_listen, NULL);
	iron_idle_wake = agent_mcp_pending;
#endif
}

void agent_run(char *prompt) {
	if (string_equals(g_project->_->filepath, "")) {
		console_error(tr("Save project first"));
		return;
	}
	char *dir = neural_node_dir();
	agent_ensure_dir(dir);
	agent_backend = g_config->console_model;
	agent_prompt  = string_copy(prompt);
	agent_dir     = string_copy(agent_work_dir());
	sys_notify_on_next_frame(agent_start, NULL);
}
