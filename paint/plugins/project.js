let project = new Project("plugins");

project.add_cfiles("plugins.c");
project.add_cfiles("uv_unwrap/**");
project.add_cfiles("io_svg/**");
project.add_cfiles("io_exr/**");
project.add_cfiles("io_psd/**");
project.add_cfiles("io_gltf/**");
project.add_cfiles("io_fbx/**");
project.add_cfiles("io_tiff/**");

if (fs_exists(os_cwd() + "/../paint/plugins/external")) {
    project.add_cfiles("external/**");
    project.add_shaders("external/*.kong");
    project.add_assets("external/assets/*", {destination : "data/{name}"});
    project.add_define("WITH_EXTERNAL");
}

return project;
