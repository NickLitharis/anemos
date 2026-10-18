@rem mk_kernellink.cmd — link the clang-only probe against XP's real RTL
@rem library, as a native subsystem image, with no CRT and no default libs.
@rem If clang's symbol decoration disagrees with the tree's, this fails with an
@rem unresolved external for _RtlInitString@8.
link.exe /nologo /nodefaultlib /subsystem:native /entry:AbiKernelProbe ^
    /libpath:W:\NT\base\ntos\rtl\up\obj\i386 ^
    out\kernellink_probe_clang.obj ntosrtl.lib /out:out\kernellink.sys
if errorlevel 1 exit /b 1
echo KERNELLINK_OK
