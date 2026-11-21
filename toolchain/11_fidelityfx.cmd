@echo off
rem Fetch the FidelityFX Vulkan headers, libraries and license.
setlocal EnableDelayedExpansion
cd /d "%~dp0"
set FX=fidelityfx-2.4.0
set FX_URL=https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/7539ae8b6568ad29ed78a753404351daf3b5cca7/Kits/FidelityFX
> fidelityfx.log (
  for %%d in (%FX% %FX%\api %FX%\api\include %FX%\api\include\vk %FX%\upscalers %FX%\upscalers\include %FX%\bin) do if not exist %%d mkdir %%d
  call :get api/include/ffx_api.h            %FX%\api\include\ffx_api.h            4D9DCF243EF5EFBF03340A02F16B51A22053243A22E8A9A0B3CE91C26982B4BF || goto :fail
  call :get api/include/ffx_api_types.h      %FX%\api\include\ffx_api_types.h      53F07C113A9BA493F297656E4481B6539AED0AFC9F3CC8DA682AA477D270C46C || goto :fail
  call :get api/include/ffx_api_loader.h     %FX%\api\include\ffx_api_loader.h     18D1901DBBC881E8131B91C65AD44BB3B4A1DE6C31435D5B75236F16727BA80E || goto :fail
  call :get api/include/vk/ffx_api_vk.h      %FX%\api\include\vk\ffx_api_vk.h      09AC1855BC4C2F711243D6982909385072CA3F8D30DB44D1A82EE5BD637832AD || goto :fail
  call :get upscalers/include/ffx_upscale.h  %FX%\upscalers\include\ffx_upscale.h  13091E7FE5FD9C82B1FB7C044B9F08F664D9653F599D2EA500738709236FCA4D || goto :fail
  call :get docs/license.md                  %FX%\license.md                       32357259B965D6A8001E9E508BA60EDA6034027C5923A7D226FFBAFF813C2BFB || goto :fail
  call :get signedbin/amd_fidelityfx_loader_vk.dll   %FX%\bin\amd_fidelityfx_loader_vk.dll   38A56A01FB3565C0757D9F5C1E4E69DE4B65E5F2E5769B323B200DC1F142BAB9 || goto :fail
  call :get signedbin/amd_fidelityfx_upscaler_vk.dll %FX%\bin\amd_fidelityfx_upscaler_vk.dll 4A7E895AF6E0B615D40A84D59EB8C2B1054AD91CDD0C739567940270D51BDAB7 || goto :fail
  echo fidelityfx exit 0
)
echo FIDELITYFX_DONE>> fidelityfx.log
exit /b 0

:fail
echo fidelityfx exit 1>> fidelityfx.log
echo FIDELITYFX_DONE>> fidelityfx.log
exit /b 1

rem :get <path under Kits/FidelityFX> <local file> <sha256>: download unless the local file already verifies.
:get
if exist "%~2" (
  call :verify "%~2" %~3 && exit /b 0
)
curl.exe -fsSL -o "%~2.partial" "%FX_URL%/%~1" 2>&1 || exit /b 1
call :verify "%~2.partial" %~3 || exit /b 1
move /y "%~2.partial" "%~2" >nul
exit /b %ERRORLEVEL%

:verify
venv\Scripts\python.exe -I verify_sha256.py "%~1" "%~2"
exit /b %ERRORLEVEL%
