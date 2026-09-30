AMAKE=../../../tools/bin/linux_x64/amake
$AMAKE --ashader spirv raytrace_brute.shader ../raytrace_brute_core.spirv
$AMAKE --ashader spirv raytrace_brute.shader ../raytrace_brute_full.spirv -D_FULL
$AMAKE --ashader spirv raytrace_bake_ao.shader ../raytrace_bake_ao.spirv
$AMAKE --ashader spirv raytrace_bake_light.shader ../raytrace_bake_light.spirv
$AMAKE --ashader spirv raytrace_bake_bent.shader ../raytrace_bake_bent.spirv
$AMAKE --ashader spirv raytrace_bake_thick.shader ../raytrace_bake_thick.spirv
