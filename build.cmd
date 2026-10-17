@echo off
rem Builds build\VlcAmbient.exe with CMake + Ninja and the installed Visual Studio C++ tools.
rem   build.cmd         configure (first time) and build
rem   build.cmd clean   delete build\ and rebuild from scratch
rem   build.cmd test    build, then run the tests (ctest)
setlocal
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo Visual Studio with C++ tools not found.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1

rem Use the CMake/Ninja bundled with Visual Studio when none is on PATH.
where cmake >nul 2>nul || set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
where ninja >nul 2>nul || set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

if /i "%~1"=="clean" if exist build rmdir /s /q build
if not exist build\CMakeCache.txt (
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
)
cmake --build build || exit /b 1

if /i "%~1"=="test" (
  ctest --test-dir build --output-on-failure || exit /b 1
)

echo Built: build\VlcAmbient.exe
