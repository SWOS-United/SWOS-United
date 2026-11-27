@echo off
rem Build OpenSWOS Setup (and the swosdata CLI) with MSVC.
rem
rem   build.bat          configure and build  -> build_win\bin\Release\swosunited-setup.exe (PROJECT_SETUP_PROGRAM)
rem   build.bat static   link Slint and the MSVC runtime statically: ONE .exe,
rem                      no slint_cpp.dll, no VC++ redistributable
rem                      -> build_win_static\bin\Release\swosunited-setup.exe
rem   build.bat clean    remove the build directories
rem
rem Without SLINT_ROOT, CMake builds Slint from source on the first configure
rem (cmake/FetchSlint.cmake), which needs Rust: https://rustup.rs
rem
rem   set SLINT_ROOT=C:\path\to\sdk            use an SDK installed elsewhere
rem   set SLINT_STATIC_ROOT=C:\path\to\sdk     where the static Slint lives
rem                                            (default D:\Projects\GitHub\slint\install\windows-x64-static)
setlocal enabledelayedexpansion
set STATIC_ARGS=
cd /d "%~dp0"

rem Not build-app, which build.sh uses: one checkout is seen by both Windows
rem and WSL, and CMakeCache.txt records the source path it was made for.
set BUILD_DIR=build_win

if /i "%~1"=="clean" (
  if exist build_win rmdir /s /q build_win
  if exist build_win_static rmdir /s /q build_win_static
  echo removed build_win\ and build_win_static\
  goto :eof
)

rem Static gets its own tree: Slint_DIR is cached, so a new SLINT_ROOT in a
rem configured tree would be ignored in silence.
if /i "%~1"=="static" (
  set BUILD_DIR=build_win_static
  if "%SLINT_STATIC_ROOT%"=="" set SLINT_STATIC_ROOT=D:\Projects\GitHub\slint\install\windows-x64-static
  if not exist "!SLINT_STATIC_ROOT!\lib\slint_cpp.lib" (
    echo No static Slint at !SLINT_STATIC_ROOT!.
    echo Build one from a Slint 1.17.1 checkout with BUILD_SHARED_LIBS=OFF ^(see
    echo StorageAgent's docs\SLINT_STATIC.md^), or set SLINT_STATIC_ROOT.
    exit /b 1
  )
  set SLINT_ROOT=!SLINT_STATIC_ROOT!
  rem The C/C++ runtime linked in as well: no VC++ redistributable needed.
  set STATIC_ARGS=-DSD_STATIC_CRT=ON
)

set CMAKE_ARCH=x64
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set CMAKE_ARCH=ARM64
if /i "%PROCESSOR_ARCHITEW6432%"=="ARM64" set CMAKE_ARCH=ARM64

if "%SLINT_ROOT%"=="" (
  where cargo >nul 2>&1
  if errorlevel 1 (
    echo cargo not found. Slint is built from source on Windows, which needs Rust:
    echo install it from https://rustup.rs and run this again, or set SLINT_ROOT.
    exit /b 1
  )
)

set ARGS=%STATIC_ARGS%
if not "%SLINT_ROOT%"=="" set ARGS=%ARGS% -DSLINT_ROOT=%SLINT_ROOT%

rem A cache generated for another source path cannot be reused; start over.
if exist %BUILD_DIR%\CMakeCache.txt (
  set CDFWD=%CD:\=/%
  findstr /i /c:"CMAKE_HOME_DIRECTORY:INTERNAL=!CDFWD!" %BUILD_DIR%\CMakeCache.txt >nul 2>&1
  if errorlevel 1 (
    echo Cache in %BUILD_DIR%\ was generated for a different source path; removing it.
    rmdir /s /q %BUILD_DIR%
  )
)

cmake -S . -B %BUILD_DIR% -A %CMAKE_ARCH% %ARGS%
if errorlevel 1 exit /b 1
cmake --build %BUILD_DIR% --config Release --parallel
if errorlevel 1 exit /b 1

echo.
echo Built:
dir /b %BUILD_DIR%\bin\Release 2>nul || dir /b %BUILD_DIR%\bin
