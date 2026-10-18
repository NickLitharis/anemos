@rem Substitute for broken tools\x86\mc.exe; filters unsupported -A/-v flags.
@set args=
:loop
@if "%~1"=="" goto done
@set arg=%~1
@if /I "%arg%"=="-A" goto skip
@if /I "%arg%"=="-v" goto skip
@set args=%args% %arg%
:skip
@shift
@goto loop
:done
@"W:\NT\base\mvdm\tools\mc.exe" %args%
