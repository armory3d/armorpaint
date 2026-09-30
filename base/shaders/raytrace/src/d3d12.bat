..\..\..\tools\bin\windows_x64\amake.exe --ashader hlsl raytrace_brute.shader raytrace_brute_core.hlsl
..\..\..\tools\bin\windows_x64\amake.exe --ashader hlsl raytrace_brute.shader raytrace_brute_full.hlsl -D_FULL
..\..\..\tools\bin\windows_x64\amake.exe --ashader hlsl raytrace_bake_ao.shader raytrace_bake_ao.hlsl
..\..\..\tools\bin\windows_x64\amake.exe --ashader hlsl raytrace_bake_light.shader raytrace_bake_light.hlsl
..\..\..\tools\bin\windows_x64\amake.exe --ashader hlsl raytrace_bake_bent.shader raytrace_bake_bent.hlsl
..\..\..\tools\bin\windows_x64\amake.exe --ashader hlsl raytrace_bake_thick.shader raytrace_bake_thick.hlsl
.\dxc.exe -Fo ..\raytrace_brute_core.cso -T cs_6_5 .\raytrace_brute_core.hlsl
.\dxc.exe -Fo ..\raytrace_brute_full.cso -T cs_6_5 .\raytrace_brute_full.hlsl
.\dxc.exe -Fo ..\raytrace_bake_ao.cso -T cs_6_5 .\raytrace_bake_ao.hlsl
.\dxc.exe -Fo ..\raytrace_bake_light.cso -T cs_6_5 .\raytrace_bake_light.hlsl
.\dxc.exe -Fo ..\raytrace_bake_bent.cso -T cs_6_5 .\raytrace_bake_bent.hlsl
.\dxc.exe -Fo ..\raytrace_bake_thick.cso -T cs_6_5 .\raytrace_bake_thick.hlsl
del *.hlsl
