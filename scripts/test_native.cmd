@echo off
setlocal
cd /d "%~dp0.."
if not defined VSCMD_VER (
  for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%i\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
)
if not exist .pio\native mkdir .pio\native
cl /nologo /std:c++17 /EHsc /D_CRT_SECURE_NO_WARNINGS /Itest\native /Isrc /Ilib\AxeFxControl\src test\native\main.cpp lib\AxeFxControl\src\interface\private\*.cpp /Fo.pio\native\ /Fe.pio\native\controller-tests.exe
if errorlevel 1 exit /b 1
.pio\native\controller-tests.exe
