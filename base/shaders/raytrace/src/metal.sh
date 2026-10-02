AMAKE=../../../tools/bin/macos/amake
$AMAKE --ashader metal raytrace_brute.shader ../raytrace_brute_core.metal -D_LOW_SAMPLES
$AMAKE --ashader metal raytrace_brute.shader ../raytrace_brute_full.metal -D_LOW_SAMPLES -D_FULL
$AMAKE --ashader metal raytrace_bake_ao.shader ../raytrace_bake_ao.metal -D_LOW_SAMPLES
$AMAKE --ashader metal raytrace_bake_light.shader ../raytrace_bake_light.metal -D_LOW_SAMPLES
$AMAKE --ashader metal raytrace_bake_bent.shader ../raytrace_bake_bent.metal -D_LOW_SAMPLES
$AMAKE --ashader metal raytrace_bake_thick.shader ../raytrace_bake_thick.metal -D_LOW_SAMPLES
