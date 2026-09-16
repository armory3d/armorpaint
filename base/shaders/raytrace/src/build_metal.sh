{ cat raytrace_brute.metal; } > ../raytrace_brute_core.metal
{ echo "#define _FULL"; cat raytrace_brute.metal; } > ../raytrace_brute_full.metal
{ echo "#define _MULTI"; cat raytrace_brute.metal; } > ../raytrace_brute_multi_core.metal
{ echo "#define _MULTI"; echo "#define _FULL"; cat raytrace_brute.metal; } > ../raytrace_brute_multi_full.metal
cp raytrace_bake_ao.metal ../raytrace_bake_ao.metal
cp raytrace_bake_light.metal ../raytrace_bake_light.metal
cp raytrace_bake_bent.metal ../raytrace_bake_bent.metal
cp raytrace_bake_thick.metal ../raytrace_bake_thick.metal
