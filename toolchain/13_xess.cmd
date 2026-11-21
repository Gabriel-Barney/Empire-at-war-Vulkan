@echo off
rem Fetch the XeSS Vulkan headers, runtime and notices.
setlocal EnableDelayedExpansion
cd /d "%~dp0"
set XS=xess-3.0.2
set XS_URL=https://raw.githubusercontent.com/intel/xess/8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0
> xess.log (
  for %%d in (%XS% %XS%\inc %XS%\inc\xess %XS%\bin) do if not exist %%d mkdir %%d
  call :get inc/xess/xess.h           %XS%\inc\xess\xess.h           98DC435F5AED252D83A161FC1F86223F20C67EB863214FE3B5CB215A3F6E3319 || goto :fail
  call :get inc/xess/xess_vk.h        %XS%\inc\xess\xess_vk.h        859086DF3C531CECCAB628B287F69BD9A4528045BCBDD8C639589082DCBF8977 || goto :fail
  call :get LICENSE.txt               %XS%\LICENSE.txt               784BE42C39A4A4D03CF85D88E15E59D75828FCA773CE45B8DBFFFBD7EAF208DF || goto :fail
  call :get third-party-programs.txt  %XS%\third-party-programs.txt  E8D8899DBFC4DE0E8CA25560BE9358E03C1405D7C956D20A1838C3C709B9B51C || goto :fail
  call :get bin/libxess.dll           %XS%\bin\libxess.dll           251659DD84A3E84DE67C886A4186E01F3ECA49B00641906FE38BB6B807E5D5B7 || goto :fail
  echo xess exit 0
)
echo XESS_DONE>> xess.log
exit /b 0

:fail
echo xess exit 1>> xess.log
echo XESS_DONE>> xess.log
exit /b 1

rem :get <path in the repository> <local file> <sha256>: download unless the local file already verifies.
:get
if exist "%~2" (
  call :verify "%~2" %~3 && exit /b 0
)
curl.exe -fsSL -o "%~2.partial" "%XS_URL%/%~1" 2>&1 || exit /b 1
call :verify "%~2.partial" %~3 || exit /b 1
move /y "%~2.partial" "%~2" >nul
exit /b %ERRORLEVEL%

:verify
venv\Scripts\python.exe -I verify_sha256.py "%~1" "%~2"
exit /b %ERRORLEVEL%
