@echo off
rem Runs one command inside the Visual Studio x64 developer environment, so
rem cmake, ninja and cl are on PATH.   Usage:  tools\dev.cmd cmake --preset release
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo error: vswhere.exe not found - is Visual Studio installed?
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo error: no Visual Studio installation with the C++ tools was found
    exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
where cl >nul 2>&1 || (
    echo error: vcvars64.bat did not put cl on PATH
    exit /b 1
)
%*
exit /b %errorlevel%
