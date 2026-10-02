AMAKE=../../../tools/bin/linux_x64/amake
$AMAKE --ashader wgsl raytrace_brute.shader ../raytrace_brute_core.wgsl -D_LOW_SAMPLES
$AMAKE --ashader wgsl raytrace_brute.shader ../raytrace_brute_full.wgsl -D_LOW_SAMPLES -D_FULL
$AMAKE --ashader wgsl raytrace_bake_ao.shader ../raytrace_bake_ao.wgsl -D_LOW_SAMPLES
$AMAKE --ashader wgsl raytrace_bake_light.shader ../raytrace_bake_light.wgsl -D_LOW_SAMPLES
$AMAKE --ashader wgsl raytrace_bake_bent.shader ../raytrace_bake_bent.wgsl -D_LOW_SAMPLES
$AMAKE --ashader wgsl raytrace_bake_thick.shader ../raytrace_bake_thick.wgsl -D_LOW_SAMPLES
