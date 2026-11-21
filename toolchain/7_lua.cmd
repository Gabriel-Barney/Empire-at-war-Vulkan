@echo off
rem Fetch the pinned Lua source used by the engine.
setlocal EnableDelayedExpansion
cd /d "%~dp0"
if not exist downloads mkdir downloads
set LUA_URL=https://www.lua.org/ftp/lua-5.0.2.tar.gz
set LUA_TGZ=downloads\lua-5.0.2.tar.gz
set LUA_SHA=A6C85D85F912E1C321723084389D63DEE7660B81B8292452B190EA7190DD73BC
> lua.log (
  if exist lua-5.0.2\src\lvm.c (
    echo lua-5.0.2 present
  ) else (
    if not exist %LUA_TGZ% curl.exe -fsSL -o %LUA_TGZ% %LUA_URL% 2>&1
    call :verify %LUA_TGZ% %LUA_SHA% || goto :fail
    rem test/lua and test/luac are Unix symlinks that Windows tar cannot create; they are not needed.
    tar.exe -xzf %LUA_TGZ% --exclude lua-5.0.2/test/lua --exclude lua-5.0.2/test/luac 2>&1
  )
  if not exist lua-5.0.2\src\lvm.c (echo lua-5.0.2\src\lvm.c missing after extract& goto :fail)
  echo lua exit 0
)
echo LUA_DONE>> lua.log
exit /b 0

:fail
echo lua exit 1>> lua.log
echo LUA_DONE>> lua.log
exit /b 1

:verify
venv\Scripts\python.exe -I verify_sha256.py "%~1" "%~2"
exit /b %ERRORLEVEL%
