@echo off
rem Builds MkUI (Release, Ninja) into build\ and runs the tests. Same as MkQA's build.bat.
rem Usage: build.bat            build + tests
rem        build.bat install    also copy to %LOCALAPPDATA%\MealKit\tools\MkUI (the editor's [Tools] menu finds it there)
setlocal
if defined VCINSTALLDIR goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS=%%i"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

:build
cd /d "%~dp0"
rem An existing build folder keeps its generator (older ones were made with Visual Studio)
if not exist build\CMakeCache.txt cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build --config Release || exit /b 1
ctest --test-dir build -C Release --output-on-failure || exit /b 1
if /i "%~1"=="install" cmake --install build --config Release --prefix "%LOCALAPPDATA%\MealKit\tools\MkUI" || exit /b 1
echo [MkUI] Build OK: %~dp0build\bin\MkUI.exe
