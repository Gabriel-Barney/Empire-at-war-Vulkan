@echo off
rem Install the pinned x64 Windows MSVC Rust toolchain under toolchain\rust.
setlocal EnableDelayedExpansion
cd /d "%~dp0"
if not exist downloads mkdir downloads
set RUSTUP_VER=1.29.3
set RUST_VER=1.100.0
set RUSTUP_URL=https://static.rust-lang.org/rustup/archive/%RUSTUP_VER%/x86_64-pc-windows-msvc/rustup-init.exe
set RUSTUP_EXE=downloads\rustup-init-%RUSTUP_VER%.exe
set RUSTUP_SHA=38027D51D69A5D05493627A0634DBEA52D6CD793C8F072D2373D3F4163EEC7DB
set "RUSTUP_HOME=%~dp0rust\rustup"
set "CARGO_HOME=%~dp0rust\cargo"
> rust.log (
  if exist "rust\rustup\toolchains\%RUST_VER%-x86_64-pc-windows-msvc\bin\rustc.exe" (
    echo rust %RUST_VER% present
  ) else (
    if not exist %RUSTUP_EXE% curl.exe -fsSL -o %RUSTUP_EXE% %RUSTUP_URL% 2>&1
    call :verify %RUSTUP_EXE% %RUSTUP_SHA% || goto :fail
    %RUSTUP_EXE% -y --no-modify-path --profile minimal --default-toolchain %RUST_VER% --default-host x86_64-pc-windows-msvc 2>&1
  )
  if not exist "rust\rustup\toolchains\%RUST_VER%-x86_64-pc-windows-msvc\bin\rustc.exe" (echo rustc %RUST_VER% missing after install& goto :fail)
  "rust\rustup\toolchains\%RUST_VER%-x86_64-pc-windows-msvc\bin\rustc.exe" --version 2>&1
  echo rust exit 0
)
echo RUST_DONE>> rust.log
exit /b 0

:fail
echo rust exit 1>> rust.log
echo RUST_DONE>> rust.log
exit /b 1

:verify
venv\Scripts\python.exe -I verify_sha256.py "%~1" "%~2"
exit /b %ERRORLEVEL%
