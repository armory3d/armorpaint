./glslangValidator -V --target-env vulkan1.3 raytrace_brute.comp -o ../raytrace_brute_core.spirv
./glslangValidator -V --target-env vulkan1.3 -D_FULL raytrace_brute.comp -o ../raytrace_brute_full.spirv
./glslangValidator -V --target-env vulkan1.3 -D_MULTI raytrace_brute.comp -o ../raytrace_brute_multi_core.spirv
./glslangValidator -V --target-env vulkan1.3 -D_MULTI -D_FULL raytrace_brute.comp -o ../raytrace_brute_multi_full.spirv
./glslangValidator -V --target-env vulkan1.3 raytrace_bake_ao.comp -o ../raytrace_bake_ao.spirv
./glslangValidator -V --target-env vulkan1.3 raytrace_bake_light.comp -o ../raytrace_bake_light.spirv
./glslangValidator -V --target-env vulkan1.3 raytrace_bake_bent.comp -o ../raytrace_bake_bent.spirv
./glslangValidator -V --target-env vulkan1.3 raytrace_bake_thick.comp -o ../raytrace_bake_thick.spirv
