Brute path tracer is a custom built path-tracing engine used in iron. It is powered by hardware accelerated ray-tracing and runs on Vulkan and Metal. On WebGPU it runs via compute shaders.

In ArmorPaint, it is used for rendering the path-traced viewport mode and for ray-traced baking features.

By default, the path-tracer supports 64 unique objects and up to 16384 instances. It has two modes - fast, which has most of the features disabled, and quality, which has features like translucency and emission enabled.

- https://github.com/armory3d/armorpaint/tree/main/base/shaders/raytrace
- https://github.com/armory3d/armorpaint/blob/main/paint/sources/render/render_path_raytrace.c
- https://github.com/armory3d/armorpaint/blob/main/paint/sources/render/render_path_raytrace_bake.c
- https://github.com/armory3d/armorpaint/blob/main/base/sources/iron_gpu.h
