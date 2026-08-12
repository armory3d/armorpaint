
#include "../global.h"

static i32 text_to_text_node_backend = CONSOLE_MODEL_QWEN;

static char *text_to_text_node_guide = "Reply with C code only wrapped in a ```c markdown fence. Place the code inside 'void main()' function. "
                                       "Do not chain statements - declare intermediary variables. Do not use preprocessor.";

static char *text_to_text_node_grok_dir(void) {
#ifndef NDEBUG
	// Prevent running from git repository
	return string("%sgrok", iron_internal_save_path());
#else
	return neural_node_dir();
#endif
}

static char *text_to_text_node_project_contents(void) {
	buffer_t *encoded = util_encode_project(g_project);
	char     *json    = armpack_decode_to_json_omit_buffers(encoded);
	return string("/* Current project state:\n%s\n*/\n", json);
}

string_array_t *text_to_text_node_qwen_args(char *dir) {
	char           *prompt_file = string("%s%sprompt.txt", dir, PATH_SEP);
	string_array_t *argv        = any_array_create_from_raw(
        (void *[]){
            string("%s/%s", dir, neural_node_llama_bin()),
            "-m",
            string("%s/Qwen3.6-27B-Q4_K_M.gguf", dir),
            "-ngl",
            "99",
            "-c",
            "20000",
            "--single-turn",
            "--prompt-cache",
            string("%s/context.bin", dir),
            "--file",
            prompt_file,
            NULL,
        },
        13);
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
	iron_delete_file(string("%s%scontext.bin", dir, PATH_SEP));
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
	char *contents  = text_to_text_node_project_contents();
	char *reference = string("%s\n%s\n%s\n", api, contents, text_to_text_node_guide);

	string_array_t *argv;
	if (text_to_text_node_backend == CONSOLE_MODEL_CLAUDE) {
		iron_file_save_bytes(string("%s%sapi.h", dir, PATH_SEP), (buffer_t *)u8_array_create_from_string(reference), 0);
		argv = text_to_text_node_claude_args(dir, prompt);
	}
	else if (text_to_text_node_backend == CONSOLE_MODEL_GROK) {
		char *gdir = text_to_text_node_grok_dir();
		if (string_equals(file_read_directory(gdir)->buffer[0], "")) {
			iron_create_directory(gdir);
		}
		char *rules = string("%s\n%s\n", api, text_to_text_node_guide);
		iron_file_save_bytes(string("%s%sAGENTS.md", gdir, PATH_SEP), (buffer_t *)u8_array_create_from_string(rules), 0);

		char *full = string("%s\n%s\n\n%s", contents, prompt, text_to_text_node_guide);
		iron_file_save_bytes(string("%s%sprompt.txt", gdir, PATH_SEP), (buffer_t *)u8_array_create_from_string(full), 0);
		argv = text_to_text_node_grok_args(gdir);
	}
	else {
		char *full = string("%s%s", reference, prompt);
		iron_file_save_bytes(string("%s%sprompt.txt", dir, PATH_SEP), (buffer_t *)u8_array_create_from_string(full), 0);
		argv = text_to_text_node_qwen_args(dir);
	}

	iron_exec_async_output_file = string("%s%soutput.txt", dir, PATH_SEP);
	iron_exec_async(argv->buffer[0], argv->buffer);
	sys_notify_on_update(text_to_text_node_check_result, done);
}
