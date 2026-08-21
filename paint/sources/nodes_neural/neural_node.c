
#include "../global.h"

char *neural_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	gpu_texture_t *result = any_imap_get(neural_node_results, socket->id);
	if (result == NULL) {
		return "float3(0.0, 0.0, 0.0)";
	}
	char *tex_name = string_copy(parser_material_node_name(node, NULL));
	any_map_set(data_cached_textures, tex_name, result);
	bind_tex_t *tex      = parser_material_make_bind_tex(tex_name, tex_name);
	char       *texstore = parser_material_texture_store(node, tex, tex_name, COLOR_SPACE_AUTO);
	return string("%s.rgb", texstore);
}

char *neural_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	gpu_texture_t *result = any_imap_get(neural_node_results, socket->id);
	if (result == NULL) {
		return "0.0";
	}
	char *tex_name = string_copy(parser_material_node_name(node, NULL));
	any_map_set(data_cached_textures, tex_name, result);
	bind_tex_t *tex      = parser_material_make_bind_tex(tex_name, tex_name);
	char       *texstore = parser_material_texture_store(node, tex, tex_name, COLOR_SPACE_AUTO);
	return string("%s.r", texstore);
}

ui_node_t *neural_from_node(ui_node_socket_t *inp, i32 socket) {
	ui_node_t        *result = NULL;
	ui_node_canvas_t *canvas = ui_nodes_get_canvas(true);
	for (i32 i = 0; i < canvas->links->length; ++i) {
		ui_node_link_t *l = canvas->links->buffer[i];
		if (l->to_id == inp->node_id && l->to_socket == socket) {
			result = ui_get_node(canvas->nodes, l->from_id);
			break;
		}
	}
	return result;
}

void neural_node_button_on_next_frame(void *_) {
	box_preferences_htab->i = PREFERENCES_TAB_NEURAL;
	box_preferences_show();
}

bool neural_node_button(ui_node_t *node, char *model) {
	char *url       = box_preferneces_model_url_from_name(model);
	char *file_name = box_preferences_file_name_from_url(url);
	bool  found     = box_preferences_model_exists(file_name);
	if (iron_exec_async_done == 0) {
		if (node != neural_node_current) {
			g_ui->enabled = false;
		}
		ui_button(tr("Running..."), UI_ALIGN_CENTER, "");
		if (node != neural_node_current) {
			g_ui->enabled = true;
		}
	}
	else if (!found && ui_button(tr("Setup"), UI_ALIGN_CENTER, "")) {
		sys_notify_on_next_frame(&neural_node_button_on_next_frame, NULL);
	}
	else if (found && ui_button(tr("Run"), UI_ALIGN_CENTER, "")) {
		return true;
	}
	return false;
}

void neural_node_load_result(ui_node_t *node) {
	char *file = string("%s%soutput.png", neural_node_dir(), PATH_SEP);
	if (iron_file_exists(file)) {
		gpu_texture_t *result = iron_load_texture(file);
		any_imap_set(neural_node_results, node->outputs->buffer[0]->id, result);
		ui_nodes_hwnd->redraws   = 2;
		ui_view2d_hwnd->redraws  = 2;
		ui_node_canvas_t *canvas = ui_nodes_get_canvas(true);
		for (i32 i = 0; i < canvas->links->length; ++i) {
			ui_node_link_t *l = canvas->links->buffer[i];
			if (l->from_id == node->id) {
				ui_node_t *to_node = ui_get_node(canvas->nodes, l->to_id);
				if (to_node != NULL && string_equals(to_node->type, "NEURAL_SAVE_IMAGE")) {
					save_image_node_run(node, result);
					break;
				}
			}
		}
	}
}

void neural_node_check_result(ui_node_t *node) {
	neural_node_current = node;
	iron_delay_idle_sleep();
	if (iron_exec_async_done == 1) {
		neural_node_load_result(node);
		sys_remove_update(neural_node_check_result);
	}
}

char *neural_node_bin_ext() {
#ifdef IRON_WINDOWS
	return ".exe";
#else
	return "";
#endif
}

char *neural_node_iris_bin() {
	return string("iris%s", neural_node_bin_ext());
}

char *neural_node_llama_bin() {
#ifdef IRON_MACOS
	return "llama_metal";
#endif
	return string("llama_vulkan%s", neural_node_bin_ext());
}

char *neural_node_dir() {
	char *dir;
	if (path_is_protected()) {
		dir = string("%smodels", iron_internal_save_path());
	}
	else {
		dir = string("%s%smodels", iron_internal_files_location(), PATH_SEP);
	}
	return dir;
}

#ifdef WITH_COMPRESS

void neural_node_download_done_untar(void *_) {
	char *tar = string("%s/Hunyuan3D_win64.tar", neural_node_dir());
	untar_here(tar);
}

#endif

#ifdef IRON_LINUX
static void neural_node_chmod_x(char *url, char *bin_name) {
	if (ends_with(url, bin_name)) {
		char *bin = string("%s/%s", neural_node_dir(), bin_name);
		iron_sys_command(string("chmod +x \"%s\"", bin));
	}
}
#endif

void neural_node_download_done(char *url) {
	neural_node_downloading--;
	console_log(string("%s %s", tr("Downloaded file from"), url));

#ifdef IRON_LINUX
	neural_node_chmod_x(url, "iris");
	neural_node_chmod_x(url, "llama_vulkan");
#endif

#ifdef WITH_COMPRESS
	if (ends_with(url, "Hunyuan3D_win64.tar")) {
		console_toast(tr("Unpacking Hunyuan3D_win64.tar"));
		sys_notify_on_next_frame(&neural_node_download_done_untar, NULL);
	}
#endif
}

void neural_node_download(char *url) {
	char *file_name = substring(url, string_last_index_of(url, "/") + 1, string_length(url));
	char *file_path = string("%s%s%s", neural_node_dir(), PATH_SEP, file_name);
	bool  found     = iron_file_exists(file_path);
	if (found) {
		return;
	}

	neural_node_downloading++;
	file_download_to(url, file_path, &neural_node_download_done, 0);
}

void neural_node_download_models(string_array_t *models) {
	if (string_equals(file_read_directory(neural_node_dir())->buffer[0], "")) {
		iron_create_directory(neural_node_dir());
	}

#ifdef IRON_WINDOWS
	neural_node_download("https://huggingface.co/armory3d/iris_bin/resolve/main/windows_x64/iris.exe");
	neural_node_download("https://huggingface.co/armory3d/llamacpp_bin/resolve/main/windows_x64/llama_vulkan.exe");
#elif defined(IRON_LINUX)
	neural_node_download("https://huggingface.co/armory3d/iris_bin/resolve/main/linux_x64/iris");
	neural_node_download("https://huggingface.co/armory3d/llamacpp_bin/resolve/main/linux_x64/llama_vulkan");
#else
	neural_node_download("https://huggingface.co/armory3d/iris_bin/resolve/main/macos/iris");
	neural_node_download("https://huggingface.co/armory3d/llamacpp_bin/resolve/main/macos/llama_metal");
#endif

	iron_net_bytes_downloaded = 0;

	for (i32 i = 0; i < models->length; ++i) {
		neural_node_download(models->buffer[i]);
	}
}
