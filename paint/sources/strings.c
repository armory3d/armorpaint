
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

char *strings_udim_face_spans_tiles() {
	return tr("Error: A face spans multiple UDIM tiles");
}

char *strings_check_internet_connection() {
	return tr("Error: Check internet connection to access the cloud");
}

char *strings_asset_already_imported() {
	return tr("Info: Asset already imported");
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
