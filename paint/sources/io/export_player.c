
#include "../global.h"

typedef enum {
	PLAYER_TARGET_WEB     = 0,
	PLAYER_TARGET_WINDOWS = 1,
	PLAYER_TARGET_LINUX   = 2,
	PLAYER_TARGET_MACOS   = 3,
} player_target_t;

static char *export_player_readme = "\
To start the player locally, use 'python -m http.server'\
and open http://localhost:8000/ in the browser.\n\
";

static char *export_player_index = "\
<!DOCTYPE html>\n\
<html lang=\"en\">\n\
<head>\n\
	<meta charset=\"UTF-8\">\n\
	<title>Player</title>\n\
	<style>\n\
		body, html {\n\
			margin: 0;\n\
			padding: 0;\n\
			width: 100%;\n\
			height: 100%;\n\
			overflow: hidden;\n\
			background-color: #222;\n\
			font-family: system-ui, -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, sans-serif;\n\
		}\n\
		#iron {\n\
			display: block;\n\
			width: 100vw;\n\
			height: 100vh;\n\
		}\n\
		.modal {\n\
			position: fixed;\n\
			top: 0;\n\
			left: 0;\n\
			width: 100%;\n\
			height: 100%;\n\
			background: rgba(0, 0, 0, 0.7);\n\
			display: none;\n\
			align-items: center;\n\
			justify-content: center;\n\
			z-index: 9999;\n\
		}\n\
		.modal .modal-content {\n\
			background: #2a2a2a;\n\
			padding: 32px 40px;\n\
			border-radius: 12px;\n\
			max-width: 420px;\n\
			text-align: center;\n\
			color: #fff;\n\
			box-shadow: 0 20px 40px rgba(0, 0, 0, 0.6);\n\
			border: 1px solid #444;\n\
		}\n\
		.modal button {\n\
			margin-top: 24px;\n\
			padding: 10px 24px;\n\
			background: #444;\n\
			color: #fff;\n\
			border: none;\n\
			border-radius: 6px;\n\
			font-size: 15px;\n\
			cursor: pointer;\n\
			transition: background 0.2s;\n\
		}\n\
		.modal button:hover {\n\
			background: #555;\n\
		}\n\
	</style>\n\
</head>\n\
<body>\n\
	<canvas id=\"iron\" tabindex=\"0\"></canvas>\n\
	<div id=\"webgpu-modal\" class=\"modal\">\n\
		<div class=\"modal-content\">\n\
			<h2>Player</h2>\n\
			<p>This browser does not support WebGPU or it is disabled.</p>\n\
		</div>\n\
	</div>\n\
	<script src=\"start.js\"></script>\n\
	<script>\n\
		async function check_webgpu() {\n\
			const modal = document.getElementById('webgpu-modal');\n\
			if (!navigator.gpu) {\n\
				modal.style.display = 'flex';\n\
				return;\n\
			}\n\
			const adapter = await navigator.gpu.requestAdapter();\n\
			if (!adapter) {\n\
				modal.style.display = 'flex';\n\
				return;\n\
			}\n\
			const device = await adapter.requestDevice();\n\
			if (!device) {\n\
				modal.style.display = 'flex';\n\
			}\n\
		}\n\
		window.addEventListener('DOMContentLoaded', () => {\n\
			check_webgpu();\n\
		});\n\
	</script>\n\
</body>\n\
</html>\n\
";

static void export_player_run_on_download(char *url, buffer_t *ab) {}

void export_player_run(char *path) {
#ifndef NDEBUG
	console_error(tr("Not available in debug build")); // Requires --embed
	return;
#endif

	char *path_base = path_base_dir(path);

	bool _pack_assets_on_save      = g_context->pack_assets_on_save;
	g_context->pack_assets_on_save = true;
	export_arm_run_project(string("%s/%s", path_base, "start.arm"));
	g_context->pack_assets_on_save = _pack_assets_on_save;

	if (box_export_player_target == PLAYER_TARGET_WEB) {
		char *start_js = string("%s/start.js", path_base);
		iron_file_download("https://armorpaint.app/start.js", &export_player_run_on_download, 0, start_js);

		char *start_wasm = string("%s/start.wasm", path_base);
		iron_file_download("https://armorpaint.app/start.wasm", &export_player_run_on_download, 0, start_wasm);

		char *readme_txt = string("%s/readme.txt", path_base);
		iron_file_save_bytes(readme_txt, sys_string_to_buffer(export_player_readme), 0);

		char *index_html = string("%s/index.html", path_base);
		iron_file_save_bytes(index_html, sys_string_to_buffer(export_player_index), 0);
	}
	else if (box_export_player_target == PLAYER_TARGET_WINDOWS) {
		char *player_bin_src = string("%s%s%s", iron_internal_files_location(), PATH_SEP, "ArmorPaint.exe");
		char *player_bin_to  = string("%s/player.exe", path_base);
		file_copy(player_bin_src, player_bin_to);
	}
	else if (box_export_player_target == PLAYER_TARGET_LINUX) {
		char *player_bin_src = string("%s%s%s", iron_internal_files_location(), PATH_SEP, "ArmorPaint");
		char *player_bin_to  = string("%s/player", path_base);
		file_copy(player_bin_src, player_bin_to);
	}
	else if (box_export_player_target == PLAYER_TARGET_MACOS) {
		console_error(tr("Not implemented"));
	}
}
