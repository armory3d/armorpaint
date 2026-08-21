
#include "../global.h"

static i32 text_to_text_node_backend = CONSOLE_MODEL_QWEN;

static char *text_to_text_node_guide = "Reply with C code only wrapped in a ```c markdown fence. Place the code inside 'void main()' function. "
                                       "Do not chain statements - declare intermediary variables. Do not use preprocessor. "
									   "Do not use casts - casting is implicit. Do not use comma operator. Do not use multi-dimensional arrays.\n";

static char *text_to_text_node_grok_dir(void) {
#ifndef NDEBUG
	// Prevent running from git repository
	return string("%sgrok", iron_internal_save_path());
#else
	return neural_node_dir();
#endif
}

static char *text_to_text_node_shapes(void) {
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

static char *text_to_text_node_scene_bounds(void) {
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
		                                 o->base->name, t->loc.x, t->loc.y, t->loc.z, max.x - min.x, max.y - min.y, max.z - min.z, min.x, min.y, min.z,
		                                 max.x, max.y, max.z, parent, o->base->visible ? "" : ", hidden"));
	}

	char *result = string_copy(string_buffer_get(&sb));
	string_buffer_free(&sb);
	return result;
}

static char *text_to_text_node_project_contents(void) {
	buffer_t *encoded = util_encode_project(g_project);
	char     *json    = armpack_decode_to_json_omit_large_arrays(encoded);
	return string("/* Current project state:\n%s\n%s%s*/\n", json, text_to_text_node_scene_bounds(), text_to_text_node_shapes());
}

static void text_to_text_node_write_sockets(buffer_t *sb, char *label, ui_node_socket_t_array_t *sockets) {
	if (sockets == NULL || sockets->length == 0) {
		return;
	}
	string_buffer_append(sb, string(" | %s:", label));
	for (i32 i = 0; i < sockets->length; ++i) {
		string_buffer_append(sb, string("%s %d %s", i > 0 ? "," : "", i, sockets->buffer[i]->name));
	}
}

static void text_to_text_node_write_node(buffer_t *sb, ui_node_t *n) {
	string_buffer_append(sb, string("// %s", n->type));
	text_to_text_node_write_sockets(sb, "in", n->inputs);
	text_to_text_node_write_sockets(sb, "out", n->outputs);
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
}

static char *text_to_text_node_nodes_reference(void) {
	buffer_t sb;
	string_buffer_init(&sb);

	nodes_material_init();

	string_buffer_append(&sb, "// Material nodes reference:\n");

	for (i32 i = 0; i < nodes_material_list->length; ++i) {
		ui_node_t_array_t *c = (ui_node_t_array_t *)nodes_material_list->buffer[i];
		for (i32 j = 0; j < c->length; ++j) {
			text_to_text_node_write_node(&sb, c->buffer[j]);
		}
	}

	ui_node_canvas_t *canvas = g_context != NULL && g_context->material != NULL ? g_context->material->canvas : NULL;
	if (canvas != NULL) {
		for (i32 i = 0; i < canvas->nodes->length; ++i) {
			if (string_equals(canvas->nodes->buffer[i]->type, "OUTPUT_MATERIAL_PBR")) {
				string_buffer_append(&sb, "//\n// Pre-created material output node:\n");
				text_to_text_node_write_node(&sb, canvas->nodes->buffer[i]);
				break;
			}
		}
	}

	char *result = string_copy(string_buffer_get(&sb));
	string_buffer_free(&sb);
	return result;
}

string_array_t *text_to_text_node_qwen_args(char *dir) {
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

string_array_t *text_to_text_node_grok_args(char *dir) {
	char           *prompt_file = string("%s%sprompt.txt", dir, PATH_SEP);
	string_array_t *argv        = any_array_create_from_raw(
        (void *[]){
            "grok",
            "--prompt-file",
            prompt_file,
            "--output-format",
            "plain",
            "--tools",
            "todo_write", // An empty list is ignored
            "--cwd",
            dir, // Pick up AGENTS.md
            NULL,
        },
        10);
	return argv;
}

string_array_t *text_to_text_node_claude_args(char *dir, char *prompt) {
	string_array_t *argv = any_array_create_from_raw(
	    (void *[]){
	        "claude",
	        "--print",
	        "--output-format",
	        "text",
	        "--tools",
	        "",
	        "--append-system-prompt-file",
	        string("%s%sapi.h", dir, PATH_SEP),
	        prompt,
	        NULL,
	    },
	    10);
	return argv;
}

void text_to_text_node_check_result(void (*done)(char *)) {
	iron_delay_idle_sleep();
	if (iron_exec_async_done == 1) {
		char *file = string("%s%soutput.txt", neural_node_dir(), PATH_SEP);
		if (iron_file_exists(file)) {
			buffer_t *b = iron_load_blob(file);
			char     *s = sys_buffer_to_string(b);

			if (text_to_text_node_backend == CONSOLE_MODEL_QWEN) {
				i32 think_end = string_last_index_of(s, "</think>\n\n");
				i32 eot       = string_last_index_of(s, "[end of text]");
				if (think_end >= 0 && eot > think_end) {
					s = substring(s, think_end + 10, eot);
				}
			}
			s = trim_end(s);

			done(s);
			base_redraw_console();
		}
		sys_remove_update(text_to_text_node_check_result);
	}
}

void text_to_text_node_clear(void) {
	char *dir = neural_node_dir();
	iron_delete_file(string("%s%sprompt.txt", dir, PATH_SEP));
	iron_delete_file(string("%s%soutput.txt", dir, PATH_SEP));
	iron_delete_file(string("%s%sapi.h", dir, PATH_SEP));
	char *gdir = text_to_text_node_grok_dir();
	iron_delete_file(string("%s%sAGENTS.md", gdir, PATH_SEP));
	iron_delete_file(string("%s%sprompt.txt", gdir, PATH_SEP));
}

void text_to_text_node_run(char *prompt, void (*done)(char *)) {
	char *dir = neural_node_dir();
	if (string_equals(file_read_directory(dir)->buffer[0], "")) {
		iron_create_directory(dir);
	}
	text_to_text_node_backend = g_config->console_model;

	char *api       = minic_api_header_generate();
	char *nodes     = text_to_text_node_nodes_reference();
	char *contents  = text_to_text_node_project_contents();
	char *reference = string("%s\n%s\n%s\n%s\n", api, nodes, contents, text_to_text_node_guide);

	string_array_t *argv;
	if (text_to_text_node_backend == CONSOLE_MODEL_CLAUDE) {
		iron_file_save_bytes(string("%s%sapi.h", dir, PATH_SEP), sys_string_to_buffer(reference), 0);
		argv = text_to_text_node_claude_args(dir, prompt);
	}
	else if (text_to_text_node_backend == CONSOLE_MODEL_GROK) {
		char *gdir = text_to_text_node_grok_dir();
		if (string_equals(file_read_directory(gdir)->buffer[0], "")) {
			iron_create_directory(gdir);
		}
		char *rules = string("%s\n%s\n", api, text_to_text_node_guide);
		iron_file_save_bytes(string("%s%sAGENTS.md", gdir, PATH_SEP), sys_string_to_buffer(rules), 0);

		char *full = string("%s\n%s\n\n%s", contents, prompt, text_to_text_node_guide);
		iron_file_save_bytes(string("%s%sprompt.txt", gdir, PATH_SEP), sys_string_to_buffer(full), 0);
		argv = text_to_text_node_grok_args(gdir);
	}
	else {
		char *full = string("%s%s", reference, prompt);
		iron_file_save_bytes(string("%s%sprompt.txt", dir, PATH_SEP), sys_string_to_buffer(full), 0);
		argv = text_to_text_node_qwen_args(dir);
	}

	iron_exec_async_output_file = string("%s%soutput.txt", dir, PATH_SEP);
	iron_exec_async(argv->buffer[0], argv->buffer);
	sys_notify_on_update(text_to_text_node_check_result, done);
}
