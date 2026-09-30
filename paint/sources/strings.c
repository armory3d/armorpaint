
#include "global.h"

char *strings_arm_file_expected() {
	return tr("Error: .arm file expected");
}

char *strings_unknown_asset_format() {
	return tr("Error: Unknown asset format");
}

char *strings_could_not_locate_texture() {
	return tr("Error: Could not locate texture");
}

char *strings_failed_to_read_mesh_data() {
	return tr("Error: Failed to read mesh data");
}

char *strings_check_internet_connection() {
	return tr("Error: Check internet connection to access the cloud");
}

char *strings_asset_already_imported() {
	return tr("Info: Asset already imported");
}

char *strings_number_ext(i32 i) {
	if (i < 10) {
		return string_tmp(".00%s", i32_to_string(i));
	}
	if (i < 100) {
		return string_tmp(".0%s", i32_to_string(i));
	}
	return string_tmp(".%s", i32_to_string(i));
}

i32 strings_split_number_ext(char *name, char **base) {
	*base   = name;
	i32 dot = string_last_index_of(name, ".");
	i32 len = string_length(name);
	if (dot <= 0 || len - dot - 1 < 3) {
		return 0;
	}
	for (i32 i = dot + 1; i < len; ++i) {
		i32 c = char_code_at(name, i);
		if (c < '0' || c > '9') {
			return 0;
		}
	}
	*base = string_tmp("%.*s", dot, name);
	return parse_int(name + dot + 1);
}

char *strings_graphics_api() {
#ifdef IRON_DIRECT3D12
	return "Direct3D12";
#elif defined(IRON_METAL)
	return "Metal";
#elif defined(IRON_VULKAN)
	return "Vulkan";
#else
	return "WebGPU";
#endif
}
