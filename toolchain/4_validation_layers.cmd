@echo off
rem Build the Vulkan validation layer with the local x64 MSVC toolchain.
setlocal EnableDelayedExpansion
cd /d "%~dp0"
set TC=%~dp0
set TC=%TC:~0,-1%
if not exist downloads mkdir downloads
set GIT_URL=https://github.com/git-for-windows/git/releases/download/v2.47.0.windows.1/MinGit-2.47.0-64-bit.zip
set GIT_ZIP=downloads\MinGit-2.47.0-64-bit.zip
set GIT_SHA=6B175D4675BFA7014BA649E3C0976DA2A281D89BD9A096F1F41437F9995497A8
set VVL_URL=https://github.com/KhronosGroup/Vulkan-ValidationLayers.git
set VVL_TAG=vulkan-sdk-1.3.296.0
set VVL_SRC=%TC%\src\Vulkan-ValidationLayers
set VVL_BLD=%TC%\build\vvl
set VVL_INS=%TC%\vvl

if exist vvl\bin\VkLayer_khronos_validation.json (
  findstr /x /c:"%VVL_TAG%" vvl\VERSION.txt >nul 2>&1 && (
    > vvl.log echo vvl present %VVL_TAG%
    echo VVL_DONE>> vvl.log
    exit /b 0
  )
)

rem Process-local environment only: MSVC, venv (cmake, ninja, python), MinGit.
call "%TC%\msvc\setup_x64.bat"
set PATH=%TC%\venv\Scripts;%TC%\mingit\cmd;%PATH%
rem git config for this process only (deep dependency paths can exceed MAX_PATH).
set GIT_CONFIG_COUNT=2
set GIT_CONFIG_KEY_0=core.longpaths
set GIT_CONFIG_VALUE_0=true
set GIT_CONFIG_KEY_1=advice.detachedHead
set GIT_CONFIG_VALUE_1=false
set GIT_CONFIG_NOSYSTEM=1
rem CMake 4.x rejects cmake_minimum_required below 3.5 in some pinned deps.
set CMAKE_POLICY_VERSION_MINIMUM=3.5

> vvl.log (
  echo === start %DATE% %TIME%
  if exist mingit\cmd\git.exe (
    echo mingit present
  ) else (
    if not exist %GIT_ZIP% curl.exe -fsSL -o %GIT_ZIP% %GIT_URL% 2>&1
    call :verify %GIT_ZIP% %GIT_SHA% || goto :fail
    venv\Scripts\python.exe -I -m zipfile -e "%GIT_ZIP%" "mingit" 2>&1 || goto :fail
  )
  git --version 2>&1 || goto :fail
  cmake --version 2>&1
  ninja --version 2>&1
  python --version 2>&1
  echo === source %VVL_TAG%
  if exist "%VVL_SRC%" rmdir /s /q "%VVL_SRC%"
  if exist "%VVL_BLD%" rmdir /s /q "%VVL_BLD%"
  if not exist src mkdir src
  git clone --depth 1 --branch %VVL_TAG% %VVL_URL% "%VVL_SRC%" 2>&1 || goto :fail
  git -C "%VVL_SRC%" describe --tags 2>&1
  echo === configure
  cmake -S "%VVL_SRC%" -B "%VVL_BLD%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DUPDATE_DEPS=ON -DBUILD_TESTS=OFF -DBUILD_WERROR=OFF -DPython3_EXECUTABLE="%TC%\venv\Scripts\python.exe" -DCMAKE_INSTALL_PREFIX="%VVL_INS%" 2>&1 || goto :fail
  echo === build
  cmake --build "%VVL_BLD%" --config Release 2>&1 || goto :fail
  echo === install
  cmake --install "%VVL_BLD%" --config Release 2>&1 || goto :fail
  dir /b "%VVL_INS%\bin" 2>&1
  >"%VVL_INS%\VERSION.txt" echo %VVL_TAG%
  echo === cleanup source and build trees
  rmdir /s /q "%VVL_SRC%" 2>&1
  rmdir /s /q "%VVL_BLD%" 2>&1
  rmdir src 2>nul
  echo === end !DATE! !TIME!
  echo vvl exit 0
)
echo VVL_DONE>> vvl.log
exit /b 0

:fail
echo vvl exit 1>> vvl.log
echo VVL_DONE>> vvl.log
exit /b 1

:verify
venv\Scripts\python.exe -I verify_sha256.py "%~1" "%~2"
exit /b %ERRORLEVEL%
