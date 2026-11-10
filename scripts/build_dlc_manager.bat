@echo off
rem Build the SWOS United DLC Manager for Windows (tools\dlc-manager, a submodule) with MSVC:
rem ONE static .exe -- Slint, SDL2, SDL2_mixer, curl, libarchive and the MSVC runtime linked in, no DLLs
rem beside it and no VC++ redistributable, like the data setup's window (tools\swosdata\build.bat static).
rem
rem   scripts\build_dlc_manager.bat         -> tools\dlc-manager\build_win_static\bin\Release\swos-dlc-manager.exe
rem   scripts\build_dlc_manager.bat clean   remove that build tree
rem
rem Run by scripts/build_dlc_manager.sh windows (scripts/build_dist.sh's dlc-manager target) through
rem cmd.exe. Needs Visual Studio 2022, CMake, and:
rem   VCPKG_ROOT          a vcpkg checkout: sdl2, curl and libarchive are installed into it for the
rem                       x64-windows-static triplet on the first run
rem   SLINT_STATIC_ROOT   a static Slint 1.17 with the software renderer (default the setup's,
rem                       D:\Projects\GitHub\slint\install\windows-x64-static)
rem Exit 3 when one of them is missing (a SKIP for build_dist.sh), 1 when the build fails.
setlocal enabledelayedexpansion
cd /d "%~dp0..\tools\dlc-manager" || exit /b 1
set BUILD_DIR=build_win_static

if /i "%~1"=="clean" (
  if exist %BUILD_DIR% rmdir /s /q %BUILD_DIR%
  echo removed tools\dlc-manager\%BUILD_DIR%\
  goto :eof
)

if "%VCPKG_ROOT%"=="" (
  echo build_dlc_manager: SKIP: VCPKG_ROOT is not set ^(a vcpkg checkout: https://github.com/microsoft/vcpkg^)
  exit /b 3
)
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
  echo build_dlc_manager: SKIP: no vcpkg.exe in %VCPKG_ROOT% ^(run its bootstrap-vcpkg.bat^)
  exit /b 3
)
if "%SLINT_STATIC_ROOT%"=="" set SLINT_STATIC_ROOT=D:\Projects\GitHub\slint\install\windows-x64-static
if not exist "%SLINT_STATIC_ROOT%\lib\slint_cpp.lib" (
  echo build_dlc_manager: SKIP: no static Slint at %SLINT_STATIC_ROOT% ^(set SLINT_STATIC_ROOT^)
  exit /b 3
)
where cmake >nul 2>&1 || (
  echo build_dlc_manager: SKIP: no cmake on PATH
  exit /b 3
)

rem sdl2-mixer for the DLC previews (.wav/.ogg/.mp3): its default build has the built-in decoders.
"%VCPKG_ROOT%\vcpkg.exe" install sdl2 sdl2-mixer curl libarchive --triplet x64-windows-static
if errorlevel 1 exit /b 1

rem A cache made for another source path (the checkout seen from elsewhere) cannot be reused.
if exist %BUILD_DIR%\CMakeCache.txt (
  set CDFWD=%CD:\=/%
  findstr /i /c:"CMAKE_HOME_DIRECTORY:INTERNAL=!CDFWD!" %BUILD_DIR%\CMakeCache.txt >nul 2>&1
  if errorlevel 1 rmdir /s /q %BUILD_DIR%
)

rem The player's folder, as the game's (include/project_identity.h): build_dlc_manager.sh sets these.
if "%SWOS_USER_DIR%"=="" (echo build_dlc_manager.bat: SWOS_USER_DIR not set ^(run it from build_dlc_manager.sh^) & exit /b 1)
cmake -S . -B %BUILD_DIR% -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
  -DSWOS_USER_DIR=%SWOS_USER_DIR% -DSWOS_USER_DOT_DIR=%SWOS_USER_DOT_DIR% ^
  -DSlint_DIR=%SLINT_STATIC_ROOT%\lib\cmake\Slint
if errorlevel 1 exit /b 1
cmake --build %BUILD_DIR% --config Release --parallel
if errorlevel 1 exit /b 1
if not exist %BUILD_DIR%\bin\Release\swos-dlc-manager.exe (
  echo build_dlc_manager: no %BUILD_DIR%\bin\Release\swos-dlc-manager.exe
  exit /b 1
)
echo build_dlc_manager: built tools\dlc-manager\%BUILD_DIR%\bin\Release\swos-dlc-manager.exe
