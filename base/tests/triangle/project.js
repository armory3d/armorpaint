
let flags = globalThis.flags;

let project = new Project("test");
project.add_project("../../");
project.add_cfiles("main.c");
project.add_shaders("./*.shader");
return project;
