@echo off
setlocal
cd /d "%~dp0"
rem Full path: with NoDefaultCurrentDirectoryInExePath set, a bare "call dev.local.cmd" is not found and the run
rem would silently go on without this machine's game-data settings.
if exist "%~dp0dev.local.cmd" call "%~dp0dev.local.cmd"
if /i "%~1"=="launcher" goto launcher
if /i "%~1"=="launcher-test" goto launcher_test
if /i "%~1"=="test" goto env
if /i "%~1"=="suite" goto env
if /i "%~1"=="build" goto env
if /i "%~1"=="configure" goto env
echo Usage: dev.cmd configure ^| build [target] ^| test [CTest options] ^| suite [synthetic^|retail^|vulkan^|traces^|all] [options] ^| launcher ^| launcher-test
exit /b 2
:env
call toolchain\msvc\setup_x64.bat || exit /b 1
set "PATH=%CD%\toolchain\cmake\bin;%CD%\toolchain\venv\Scripts;%PATH%"
if /i "%~1"=="test" goto test
if /i "%~1"=="suite" goto suite
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl || exit /b 1
if /i "%~1"=="configure" exit /b 0
if "%~2"=="" (cmake --build build) else (cmake --build build --target "%~2")
exit /b %ERRORLEVEL%
:test
if not exist build\CTestTestfile.cmake (echo Run dev.cmd build first.& exit /b 1)
call :tmp
toolchain\venv\Scripts\python.exe -I -c "import subprocess, sys; sys.exit(subprocess.call(['ctest', '--test-dir', 'build', '--output-on-failure', '--no-tests=error', *sys.argv[2:]]))" %*
exit /b %ERRORLEVEL%
:suite
if not exist build\CTestTestfile.cmake (echo Run dev.cmd build first.& exit /b 1)
call :tmp
rem dev.cmd suite [name] [test_suite.py options] [-- ctest args]; the name defaults to synthetic.
set "_suite=%~2"
if "%_suite%"=="" set "_suite=synthetic"
if "%_suite:~0,1%"=="-" (set "_suite=synthetic") else (shift)
shift
set "_rest="
:suite_args
if "%~1"=="" goto suite_run
set _rest=%_rest% %1
shift
goto suite_args
:suite_run
toolchain\venv\Scripts\python.exe -I tools\test_suite.py --suite %_suite% %_rest%
exit /b %ERRORLEVEL%
:tmp
if not exist "build\tmp" mkdir "build\tmp"
set "TEMP=%CD%\build\tmp"
set "TMP=%TEMP%"
exit /b 0
:launcher
call launcher\cargo.cmd build --release --locked
exit /b %ERRORLEVEL%
:launcher_test
call launcher\cargo.cmd test --release --locked
exit /b %ERRORLEVEL%
