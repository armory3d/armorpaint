.\dxc.exe -Zpr -Fo ..\raytrace_brute_core.cso -T cs_6_5 .\raytrace_brute.hlsl
.\dxc.exe -Zpr -Fo ..\raytrace_brute_full.cso -T cs_6_5 .\raytrace_brute.hlsl -D _FULL
.\dxc.exe -Zpr -Fo ..\raytrace_brute_multi_core.cso -T cs_6_5 .\raytrace_brute.hlsl -D _MULTI
.\dxc.exe -Zpr -Fo ..\raytrace_brute_multi_full.cso -T cs_6_5 .\raytrace_brute.hlsl -D _MULTI -D _FULL
.\dxc.exe -Zpr -Fo ..\raytrace_bake_ao.cso -T cs_6_5 .\raytrace_bake_ao.hlsl
.\dxc.exe -Zpr -Fo ..\raytrace_bake_light.cso -T cs_6_5 .\raytrace_bake_light.hlsl
.\dxc.exe -Zpr -Fo ..\raytrace_bake_bent.cso -T cs_6_5 .\raytrace_bake_bent.hlsl
.\dxc.exe -Zpr -Fo ..\raytrace_bake_thick.cso -T cs_6_5 .\raytrace_bake_thick.hlsl
