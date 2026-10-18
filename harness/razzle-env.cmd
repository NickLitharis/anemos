@rem Minimal razzle environment for build.exe under Wine (razzle.cmd itself hangs on
@rem Microsoft-internal steps). Usage: razzle-env.cmd <dir relative to W:\NT> [build args]
@echo off
set _NTDRIVE=W:
set _NTROOT=\NT
set _NTBINDIR=W:\NT
set NTMAKEENV=W:\NT\tools
set _BuildArch=x86
set 386=1
set PROCESSOR_ARCHITECTURE=x86
set BUILD_DEFAULT=daytona -e -E -w -nmake -i
set BUILD_DEFAULT_TARGETS=-386
set BUILD_MAKE_PROGRAM=nmake.exe
set BUILD_PRODUCT=NT
set BUILD_PRODUCT_VER=500
set BUILD_OFFLINE=1
set NTDEBUG=ntsdnodbg
set NTDEBUGTYPE=windbg
set _NTTREE=W:\binaries.x86fre
set _NT386TREE=W:\binaries.x86fre
set BINPLACE_PLACEFILE_DEFAULT=W:\NT\public\sdk\lib\placefil.txt
set BINPLACE_FLAGS=-xa
set NO_BINPLACE=
set BUILD_PATH=%XP_HARNESS_BIN%;W:\NT\tools\x86\perl\bin;W:\NT\tools\x86;W:\NT\tools;C:\windows\system32;C:\windows
set PATH=%XP_HARNESS_BIN%;W:\NT\tools\x86\perl\bin;W:\NT\tools\x86;W:\NT\tools;%PATH%
cd /d W:\NT\%1
build %2 %3 %4 %5
echo BUILD_RC=%ERRORLEVEL%
