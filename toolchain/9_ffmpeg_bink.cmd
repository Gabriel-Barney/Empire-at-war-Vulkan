@echo off
rem Build LGPL FFmpeg Bink DLLs with MSVC. ALAMO_FFMPEG_WORK can name an existing scratch parent without spaces. Each build creates its own subfolder.
setlocal EnableDelayedExpansion
cd /d "%~dp0"
if not exist downloads mkdir downloads
set FF_VER=8.1.3
set FF_DIR=ffmpeg-%FF_VER%
set FF_URL=https://ffmpeg.org/releases/ffmpeg-%FF_VER%.tar.xz
set FF_TXZ=downloads\ffmpeg-%FF_VER%.tar.xz
set FF_SHA=7138D28C96D9D3E3AF4EE3D8CAD72741F8FFB40DA90C1112235DEA3ECD3178A3
set MS_URL=https://repo.msys2.org/distrib/x86_64/msys2-base-x86_64-20260611.tar.xz
set MS_TXZ=downloads\msys2-base-x86_64-20260611.tar.xz
set MS_SHA=A2D047E8EE213C3C6A49A8DE427EB1069DF12207C0422FF1B3CBB5C905C34221
set MK_URL=https://repo.msys2.org/msys/x86_64/make-4.4.1-3-x86_64.pkg.tar.zst
set MK_PKG=downloads\make-4.4.1-3-x86_64.pkg.tar.zst
set MK_SHA=AF0BDBA17F06FE037F0194069ADAA31A8FE45F1A11381501896AEA1FAE37BD5D
set DU_URL=https://repo.msys2.org/msys/x86_64/diffutils-3.12-1-x86_64.pkg.tar.zst
set DU_PKG=downloads\diffutils-3.12-1-x86_64.pkg.tar.zst
set DU_SHA=7902C8CE3D4DD69A0F5E98DC9D5C83C17B23314BA486169DB57EF6E2835CE3B6
set "WORK_PARENT=%TEMP%"
if defined ALAMO_FFMPEG_WORK set "WORK_PARENT=%ALAMO_FFMPEG_WORK%"
call :main > ffmpeg.log 2>&1
set RC=%ERRORLEVEL%
>> ffmpeg.log echo ffmpeg exit %RC%
echo FFMPEG_DONE>> ffmpeg.log
exit /b %RC%

:main
if exist %FF_DIR%\bin\avcodec-62.dll if exist %FF_DIR%\include\libavcodec\avcodec.h (
  echo %FF_DIR% present
  goto :check
)
if not exist msvc\setup_x64.bat (echo toolchain\msvc missing: run 2_setup.cmd first& exit /b 1)
set "CMAKE_EXE=%~dp0cmake\bin\cmake.exe"
if not exist "!CMAKE_EXE!" set "CMAKE_EXE=%~dp0venv\Scripts\cmake.exe"
if not exist "!CMAKE_EXE!" (echo Local CMake missing. Run 2_setup.cmd.& exit /b 1)
if not "!WORK_PARENT: =!"=="!WORK_PARENT!" (echo Set ALAMO_FFMPEG_WORK to a scratch parent directory without spaces.& exit /b 1)
if not exist %FF_TXZ% curl.exe -fsSL -o %FF_TXZ% %FF_URL%
call :verify %FF_TXZ% %FF_SHA% || exit /b 1
if exist msys64\usr\bin\make.exe if exist msys64\usr\bin\cmp.exe goto :have_msys
if not exist %MS_TXZ% curl.exe -fsSL -o %MS_TXZ% %MS_URL%
if not exist %MK_PKG% curl.exe -fsSL -o %MK_PKG% %MK_URL%
if not exist %DU_PKG% curl.exe -fsSL -o %DU_PKG% %DU_URL%
call :verify %MS_TXZ% %MS_SHA% || exit /b 1
call :verify %MK_PKG% %MK_SHA% || exit /b 1
call :verify %DU_PKG% %DU_SHA% || exit /b 1
if not exist msys64\usr\bin\bash.exe tar.exe -xJf %MS_TXZ%
pushd msys64
"!CMAKE_EXE!" -E tar xf ..\%MK_PKG%
"!CMAKE_EXE!" -E tar xf ..\%DU_PKG%
del /q .BUILDINFO .MTREE .PKGINFO 2>nul
popd
:have_msys
if exist msys64\usr\bin\link.exe del /q msys64\usr\bin\link.exe
if not exist msys64\usr\bin\make.exe (echo msys64 make missing after extract& exit /b 1)
if not exist msys64\usr\bin\cmp.exe (echo msys64 cmp missing after extract& exit /b 1)
set "WORK="
for /f "delims=" %%W in ('venv\Scripts\python.exe -c "import os,tempfile; print(tempfile.mkdtemp(prefix='eaw-ffmpeg-',dir=os.environ['WORK_PARENT']))"') do set "WORK=%%W"
if not defined WORK (echo Cannot create FFmpeg scratch directory.& exit /b 1)
tar.exe -xJf %FF_TXZ% -C "!WORK!"
if not exist "!WORK!\%FF_DIR%\configure" (echo ffmpeg source missing after extract& exit /b 1)
call msvc\setup_x64.bat >nul
msys64\usr\bin\bash.exe --noprofile --norc 9_ffmpeg_bink.sh "!WORK!\%FF_DIR%" "!WORK!\build" "!WORK!\install"
if errorlevel 1 exit /b 1
if not exist "!WORK!\install\bin\avcodec-62.dll" (echo build failed: see !WORK!\build\ffbuild\config.log& exit /b 1)
if exist %FF_DIR% rd /s /q %FF_DIR%
xcopy /e /i /q /y "!WORK!\install" %FF_DIR%
copy /y "!WORK!\%FF_DIR%\COPYING.LGPLv2.1" %FF_DIR%\ >nul
copy /y "!WORK!\%FF_DIR%\LICENSE.md" %FF_DIR%\ >nul
copy /y "!WORK!\build\config.h" %FF_DIR%\BUILD_CONFIG.txt >nul
rd /s /q "!WORK!"
:check
for %%f in (bin\avcodec-62.dll bin\avformat-62.dll bin\avutil-60.dll bin\avcodec.lib include\libavformat\avformat.h COPYING.LGPLv2.1) do (
  if not exist %FF_DIR%\%%f (echo %FF_DIR%\%%f missing& exit /b 1)
)
findstr /c:"--enable-gpl" /c:"--enable-nonfree" %FF_DIR%\BUILD_CONFIG.txt >nul && (echo GPL/nonfree option found in build config& exit /b 1)
findstr /b /c:"#define FFMPEG_LICENSE" %FF_DIR%\BUILD_CONFIG.txt
exit /b 0

:verify
venv\Scripts\python.exe -I verify_sha256.py "%~1" "%~2"
exit /b %ERRORLEVEL%
