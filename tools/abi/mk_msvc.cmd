@rem mk_msvc.cmd — build every MSVC side of the G1 proof with the tree's own
@rem cl.exe 13.00. Objects only; links happen in mk_link.cmd once the clang
@rem objects exist.
cl.exe /nologo /c layout_probe.c /Foout\layout_msvc.obj
if errorlevel 1 exit /b 1
cl.exe /nologo /c decor_probe.c /Foout\decor_msvc.obj
if errorlevel 1 exit /b 1
cl.exe /nologo /c cross_callee.c /Foout\cross_callee_msvc.obj
if errorlevel 1 exit /b 1
cl.exe /nologo /c cross_main.c /Foout\cross_main_msvc.obj
if errorlevel 1 exit /b 1
echo MSVC_OBJS_OK
