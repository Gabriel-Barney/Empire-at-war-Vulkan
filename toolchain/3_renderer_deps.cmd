@echo off
rem Fetch Vulkan headers, MSVC-compatible DXC binaries and Vulkan Memory Allocator.
setlocal EnableDelayedExpansion
cd /d "%~dp0"
if not exist downloads mkdir downloads
set VKH_URL=https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.3.296.0.zip
set VKH_ZIP=downloads\Vulkan-Headers-vulkan-sdk-1.3.296.0.zip
set VKH_SHA=533A8E17E4725A70213DDA1790E0C740794F4FA1E8FE3279B53406FCB5CFEC97
set DXC_URL=https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2407/dxc_2024_07_31.zip
set DXC_ZIP=downloads\dxc_2024_07_31.zip
set DXC_SHA=E2627F004F0F9424D8C71EA1314D04F38C5A5096884AE9217F1F18BD320267B5
set VMA_URL=https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/v3.3.0/include/vk_mem_alloc.h
set VMA_LIC=https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/v3.3.0/LICENSE.txt
set VMA_H=downloads\vk_mem_alloc-v3.3.0.h
set VMA_SHA=90CE12FC4A2466235A09AE02905DD0C13AEE80C1BBF11B331AB61230C2CEB112
> deps.log (
  if exist vulkan-headers\include\vulkan\vulkan.h (
    echo vulkan-headers present
  ) else (
    if not exist %VKH_ZIP% curl.exe -fsSL -o %VKH_ZIP% %VKH_URL% 2>&1
    call :verify %VKH_ZIP% %VKH_SHA% || goto :fail
    venv\Scripts\python.exe -I -m zipfile -e "%VKH_ZIP%" "downloads\vkh_tmp" 2>&1 || goto :fail
    move "downloads\vkh_tmp\Vulkan-Headers-vulkan-sdk-1.3.296.0" vulkan-headers 2>&1
    rmdir /s /q downloads\vkh_tmp
  )
  if exist dxc\bin\x64\dxc.exe (
    echo dxc present
  ) else (
    if not exist %DXC_ZIP% curl.exe -fsSL -o %DXC_ZIP% %DXC_URL% 2>&1
    call :verify %DXC_ZIP% %DXC_SHA% || goto :fail
    venv\Scripts\python.exe -I -m zipfile -e "%DXC_ZIP%" "dxc" 2>&1 || goto :fail
  )
  if exist vma\include\vk_mem_alloc.h (
    echo vma present
  ) else (
    if not exist %VMA_H% curl.exe -fsSL -o %VMA_H% %VMA_URL% 2>&1
    call :verify %VMA_H% %VMA_SHA% || goto :fail
    mkdir vma\include 2>nul
    copy /y %VMA_H% vma\include\vk_mem_alloc.h >nul
    curl.exe -fsSL -o vma\LICENSE.txt %VMA_LIC% 2>&1
  )
  dxc\bin\x64\dxc.exe --version 2>&1
  echo deps exit 0
)
echo DEPS_DONE>> deps.log
exit /b 0

:fail
echo deps exit 1>> deps.log
echo DEPS_DONE>> deps.log
exit /b 1

:verify
venv\Scripts\python.exe -I verify_sha256.py "%~1" "%~2"
exit /b %ERRORLEVEL%
