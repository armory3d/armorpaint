
let project = new Project("amake");
project.add_define('AMAKE');

project.add_include_dir("../../sources");
project.add_cfiles("../../sources/iron_string.c");
project.add_cfiles("../../sources/iron_array.c");

project.add_cfiles("quickjs-amalgam.c");
project.add_define("JS_DEFAULT_STACK_SIZE=8388608"); // 8 * 1024 * 1024
project.add_define("QJS_BUILD_LIBC");
project.add_cfiles("main.c");
project.add_cfiles("aimage.c");

project.add_cfiles("ashader.c");

project.add_cfiles("../../sources/kong/dir.c");
project.add_cfiles("../../sources/kong/kong.c");
project.add_cfiles("../../sources/kong/kong_cstyle.c");
project.add_cfiles("../../sources/kong/stb_ds.c");
project.add_cfiles("../../sources/kong/kong_spirv.c");
project.add_cfiles("../../sources/kong/kong_wgsl.c");
if (platform == "windows") {
	project.add_cfiles("../../sources/kong/kong_hlsl.c");
}
if (platform == "macos" || platform == "ios") {
	project.add_cfiles("../../sources/kong/kong_metal.c");
}

if (platform === "windows") {
	project.add_define('_CRT_SECURE_NO_WARNINGS');
	project.add_lib('d3dcompiler');
	project.add_lib("dxguid");
}
else if (platform === "macos") {
	project.add_cfiles("../../sources/backends/data/mac.plist");
}
else if (platform === "linux") {
	project.add_lib('dl');
}

return project;
