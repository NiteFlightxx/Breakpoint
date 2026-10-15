@echo off
setlocal
set "BREAKPOINT_ROOT=%~dp0"
set "BREAKPOINT_EXE=%BREAKPOINT_ROOT%build\x64-Release\bin\Breakpoint.exe"

if not exist "%BREAKPOINT_EXE%" (
    echo Breakpoint Release executable was not found:
    echo "%BREAKPOINT_EXE%"
    echo Build Breakpoint.sln with Release and x64 first.
    pause
    exit /b 1
)

if not exist "%BREAKPOINT_ROOT%build\x64-Release\bin\g2p2gComputeShader.cso" (
    echo Compiled shaders are missing. Rebuild Breakpoint.sln with Release and x64.
    pause
    exit /b 1
)

if not exist "%BREAKPOINT_ROOT%app\objs\cube.obj" (
    echo Runtime assets are missing from "%BREAKPOINT_ROOT%app\objs".
    pause
    exit /b 1
)

start "" /D "%BREAKPOINT_ROOT%app" "%BREAKPOINT_EXE%" %*
exit /b %errorlevel%
