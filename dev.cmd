@echo off
setlocal
cd /d "%~dp0"
if exist dev.local.cmd call dev.local.cmd
if /i "%~1"=="launcher" goto launcher
if /i "%~1"=="launcher-test" goto launcher_test
if /i "%~1"=="test" goto env
if /i "%~1"=="build" goto env
if /i "%~1"=="configure" goto env
echo Usage: dev.cmd configure ^| build [target] ^| test [CTest options] ^| launcher ^| launcher-test
exit /b 2
:env
call toolchain\msvc\setup_x64.bat || exit /b 1
set "PATH=%CD%\toolchain\cmake\bin;%CD%\toolchain\venv\Scripts;%PATH%"
if /i "%~1"=="test" goto test
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl || exit /b 1
if /i "%~1"=="configure" exit /b 0
if "%~2"=="" (cmake --build build) else (cmake --build build --target "%~2")
exit /b %ERRORLEVEL%
:test
if not exist build\CTestTestfile.cmake (echo Run dev.cmd build first.& exit /b 1)
if not exist "build\tmp" mkdir "build\tmp"
set "TEMP=%CD%\build\tmp"
set "TMP=%TEMP%"
toolchain\venv\Scripts\python.exe -I -c "import subprocess, sys; sys.exit(subprocess.call(['ctest', '--test-dir', 'build', '--output-on-failure', '--no-tests=error', *sys.argv[2:]]))" %*
exit /b %ERRORLEVEL%
:launcher
call launcher\cargo.cmd build --release --locked
exit /b %ERRORLEVEL%
:launcher_test
call launcher\cargo.cmd test --release --locked
exit /b %ERRORLEVEL%
