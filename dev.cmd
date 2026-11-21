@echo off
rem The one development command. A thin shim: the logic is tools\dev.py (docs\DECISIONS.md "One development entry
rem point"); run "dev.cmd help" for the commands. Every argument is passed on unchanged.
setlocal
cd /d "%~dp0"
rem Full path: with NoDefaultCurrentDirectoryInExePath set, a bare "call dev.local.cmd" is not found and the run
rem would silently go on without this machine's game-data settings.
if exist "%~dp0dev.local.cmd" call "%~dp0dev.local.cmd"
set "ALAMO_DEV_PY=%~dp0toolchain\venv\Scripts\python.exe"
if not exist "%ALAMO_DEV_PY%" (
  echo dev.cmd: %ALAMO_DEV_PY% not found.
  echo Install 64-bit Python 3.10 or newer, then run toolchain\2_setup.cmd ^(README.md "Build and test"^).
  exit /b 1
)
"%ALAMO_DEV_PY%" -I "%~dp0tools\dev.py" %*
exit /b %ERRORLEVEL%
