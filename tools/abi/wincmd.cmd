@rem wincmd.cmd — run one .cmd file inside the tree's own tool environment.
@rem usage (from bash): wine cmd /c wincmd.cmd <workdir-windows-path> <script-name>
@echo off
set _NTDRIVE=W:
set _NTROOT=\NT
set _NTBINDIR=W:\NT
set NTMAKEENV=W:\NT\tools
set PATH=W:\NT\tools\x86;%PATH%
set LIB=
set INCLUDE=
cd /d %1
call %2
echo CMD_RC=%ERRORLEVEL%
