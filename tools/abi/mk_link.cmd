@rem mk_link.cmd — link the mixed executables with the tree's link.exe.
@rem Each pair is deliberately mixed: one object from clang, one from MSVC.
@rem The CRT is named explicitly because clang objects carry no /DEFAULTLIB
@rem directive: MSVC objects declare their own default libraries inside the
@rem object, clang's do not. Without these names the link fails on _printf and
@rem _mainCRTStartup — measured, not assumed.
@rem LNK4078 (.rdata attributes differ) and LNK4098 (LIBC vs LIBCMT) are noise
@rem from mixing the two toolchains in one image; both are recorded in
@rem docs/G1-ABI-PROOF.md.
set LIBP=/libpath:W:\NT\public\sdk\lib\i386
cl.exe /nologo out\layout_msvc.obj /Feout\layout_msvc.exe /link %LIBP% libcmt.lib oldnames.lib
if errorlevel 1 exit /b 1
cl.exe /nologo out\layout_probe_clang.obj /Feout\layout_clang.exe /link %LIBP% libcmt.lib oldnames.lib
if errorlevel 1 exit /b 1
cl.exe /nologo out\cross_main_msvc.obj out\cross_callee_clang.obj /Feout\cross_msvc_caller.exe /link %LIBP% libcmt.lib oldnames.lib
if errorlevel 1 exit /b 1
cl.exe /nologo out\cross_main_clang.obj out\cross_callee_msvc.obj /Feout\cross_clang_caller.exe /link %LIBP% libcmt.lib oldnames.lib
if errorlevel 1 exit /b 1
echo LINK_OK
