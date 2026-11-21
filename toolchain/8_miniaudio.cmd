@echo off
rem Fetch the pinned miniaudio source used by the engine.
setlocal EnableDelayedExpansion
cd /d "%~dp0"
if not exist downloads mkdir downloads
set MA_URL=https://github.com/mackron/miniaudio/archive/refs/tags/0.11.25.tar.gz
set MA_TGZ=downloads\miniaudio-0.11.25.tar.gz
set MA_SHA=B900EDCFFE979816E2560A0580B9B1216D674B4F17FBADECA8F777A7F8AB0274
> miniaudio.log (
  if exist miniaudio-0.11.25\miniaudio.c (
    echo miniaudio-0.11.25 present
  ) else (
    if not exist %MA_TGZ% curl.exe -fsSL -o %MA_TGZ% %MA_URL% 2>&1
    call :verify %MA_TGZ% %MA_SHA% || goto :fail
    tar.exe -xzf %MA_TGZ% 2>&1
  )
  if not exist miniaudio-0.11.25\miniaudio.c (echo miniaudio-0.11.25\miniaudio.c missing after extract& goto :fail)
  if not exist miniaudio-0.11.25\miniaudio.h (echo miniaudio-0.11.25\miniaudio.h missing after extract& goto :fail)
  echo miniaudio exit 0
)
echo MINIAUDIO_DONE>> miniaudio.log
exit /b 0

:fail
echo miniaudio exit 1>> miniaudio.log
echo MINIAUDIO_DONE>> miniaudio.log
exit /b 1

:verify
venv\Scripts\python.exe -I verify_sha256.py "%~1" "%~2"
exit /b %ERRORLEVEL%
