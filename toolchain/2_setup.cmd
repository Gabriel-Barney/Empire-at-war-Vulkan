@echo off
setlocal
cd /d "%~dp0"
if /i "%~1"=="--check" goto check
if not "%~1"=="" (echo Usage: 2_setup.cmd [--check]& exit /b 2)

if not exist "venv\Scripts\python.exe" (
  python -c "import struct, sys; sys.exit(0 if sys.version_info >= (3, 10) and struct.calcsize('P') == 8 else 1)" || (echo Install 64-bit Python 3.10 or newer.& exit /b 1)
  python -m venv venv || exit /b 1
)
call :cmake_ready
if errorlevel 1 (
  venv\Scripts\python.exe -m ensurepip --upgrade || exit /b 1
  venv\Scripts\python.exe -m pip install cmake==4.4.4 ninja==1.13.2 || exit /b 1
)
set "PATH=%CD%\cmake\bin;%CD%\venv\Scripts;%PATH%"
if not exist "msvc\setup_x64.bat" (
  venv\Scripts\python.exe portable-msvc.py --vs 2026 --msvc-version 14.51 --sdk-version 28000 --host x64 --target x64 || exit /b 1
)
if not exist "msvc\setup_x64.bat" (echo MSVC setup failed.& exit /b 1)
for %%S in (3_renderer_deps 4_validation_layers 7_lua 8_miniaudio 9_ffmpeg_bink 11_fidelityfx 12_rust 13_xess) do (
  echo Checking %%S
  call %%S.cmd || exit /b 1
)

:check
call :cmake_ready || (echo Missing local CMake, CTest, or Ninja.& exit /b 1)
for %%F in (
  "venv\Scripts\python.exe"
  "msvc\setup_x64.bat"
  "vulkan-headers\include\vulkan\vulkan.h"
  "vma\include\vk_mem_alloc.h"
  "dxc\bin\x64\dxc.exe"
  "dxc\bin\x64\dxcompiler.dll"
  "vvl\bin\VkLayer_khronos_validation.dll"
  "vvl\bin\VkLayer_khronos_validation.json"
  "lua-5.0.2\src\lvm.c"
  "miniaudio-0.11.25\miniaudio.c"
  "ffmpeg-8.1.3\bin\avcodec-62.dll"
  "ffmpeg-8.1.3\bin\avformat-62.dll"
  "ffmpeg-8.1.3\bin\avutil-60.dll"
  "fidelityfx-2.4.0\bin\amd_fidelityfx_upscaler_vk.dll"
  "xess-3.0.2\bin\libxess.dll"
  "rust\cargo\bin\cargo.exe"
  "rust\rustup\toolchains\1.100.0-x86_64-pc-windows-msvc\bin\rustc.exe"
) do if not exist %%F (echo Missing %%F. Run toolchain\2_setup.cmd.& exit /b 1)
call msvc\setup_x64.bat
if not exist "%VCToolsInstallDir%bin\Hostx64\x64\cl.exe" (echo Missing x64 MSVC compiler.& exit /b 1)
if not exist "%WindowsSdkBinPath%%WindowsSDKVersion%x64\rc.exe" (echo Missing x64 Windows SDK.& exit /b 1)
"%BUILD_BIN%\cmake.exe" --version || exit /b 1
"%BUILD_BIN%\ninja.exe" --version || exit /b 1
"rust\rustup\toolchains\1.100.0-x86_64-pc-windows-msvc\bin\rustc.exe" --version || exit /b 1
echo MSVC %VCToolsVersion% and Windows SDK %WindowsSDKVersion% are ready.
exit /b 0

:cmake_ready
for %%D in ("cmake\bin" "venv\Scripts") do (
  if exist "%%~D\cmake.exe" if exist "%%~D\ctest.exe" if exist "%%~D\ninja.exe" (
    set "BUILD_BIN=%%~D"
    exit /b 0
  )
)
exit /b 1
