@rem Razzle environment for an AMD64 kernel build under Wine.
@rem Same shape as razzle-env.cmd, but targets the AMD64 cross-compiler in
@rem NT/tools/win64/x86/amd64 and sets AMD64=1 so makefile.plt applies
@rem TARGET_DEFINES=-D_AMD64_ -D_WIN64 and TARGET_DIRECTORY=amd64.
@echo off
set _NTDRIVE=W:
set _NTROOT=\NT
set _NTBINDIR=W:\NT
set NTMAKEENV=W:\NT\tools
set _BuildArch=amd64
set AMD64=1
set _AMD64_=1
set _WIN64=1
set PROCESSOR_ARCHITECTURE=AMD64
set BUILD_DEFAULT=daytona -e -E -w -nmake -i
set BUILD_DEFAULT_TARGETS=-amd64
set BUILD_MAKE_PROGRAM=nmake.exe
set BUILD_PRODUCT=NT
set BUILD_PRODUCT_VER=500
set BUILD_OFFLINE=1
set NTDEBUG=ntsdnodbg
set NTDEBUGTYPE=windbg
set _NTTREE=W:\binaries.amd64fre
set _NTAMD64TREE=W:\binaries.amd64fre
set BINPLACE_PLACEFILE_DEFAULT=W:\NT\public\sdk\lib\placefil.txt
set BINPLACE_FLAGS=-xa
set NO_BINPLACE=
set BUILD_PATH=%XP_HARNESS_BIN%;W:\NT\tools\win64\x86\amd64;W:\NT\tools\x86\perl\bin;W:\NT\tools\x86;W:\NT\tools;C:\windows\system32;C:\windows
set PATH=%XP_HARNESS_BIN%;W:\NT\tools\win64\x86\amd64;W:\NT\tools\x86\perl\bin;W:\NT\tools\x86;W:\NT\tools;%PATH%
cd /d W:\NT\%1
build %2 %3 %4 %5
echo BUILD_RC=%ERRORLEVEL%
