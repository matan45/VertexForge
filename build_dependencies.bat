@echo off
setlocal

rem ============================================================
rem  VertexForge - CMake dependency builder
rem
rem  Builds the libs premake5.lua expects to find pre-built:
rem    libogg      -> dependencies/libogg/build/{Debug,Release}/ogg.lib            (static)
rem    libvorbis   -> dependencies/libvorbis/build/lib/{Debug,Release}/vorbis*.lib (static)
rem    assimp      -> dependencies/assimp/build/lib/{Debug,Release}/assimp-vc145-mt(d).lib + DLL in build/bin
rem    openal-soft -> dependencies/openal-soft/build/{Debug,Release}/OpenAL32.{lib,dll}
rem    freetype    -> dependencies/freetype/build/{Debug,Release}/freetype(d).lib  (static)
rem
rem  Usage:
rem    build_dependencies.bat            (Debug + Release)
rem    build_dependencies.bat debug
rem    build_dependencies.bat release
rem ============================================================

set "ROOT=%~dp0"
set "DEPS=%ROOT%dependencies"
rem forward-slash variant for CMake -D path arguments
set "DEPSF=%DEPS:\=/%"
set "GEN=Visual Studio 18 2026"

set "BUILD_DEBUG=1"
set "BUILD_RELEASE=1"
if /i "%~1"=="debug"   set "BUILD_RELEASE="
if /i "%~1"=="release" set "BUILD_DEBUG="

where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] cmake.exe not found on PATH.
    exit /b 1
)

rem ---------- 1/5 libogg (static, must precede libvorbis) ----------
echo.
echo === [1/5] libogg ===
cmake -S "%DEPS%\libogg" -B "%DEPS%\libogg\build" -G "%GEN%" -A x64 ^
    -DBUILD_SHARED_LIBS=OFF
if errorlevel 1 goto :fail
call :build_configs "%DEPS%\libogg\build"
if errorlevel 1 goto :fail

rem ---------- 2/5 libvorbis (static, links against libogg) ----------
echo.
echo === [2/5] libvorbis ===
set "OGG_LIB=%DEPSF%/libogg/build/Release/ogg.lib"
if not defined BUILD_RELEASE set "OGG_LIB=%DEPSF%/libogg/build/Debug/ogg.lib"
cmake -S "%DEPS%\libvorbis" -B "%DEPS%\libvorbis\build" -G "%GEN%" -A x64 ^
    -DBUILD_SHARED_LIBS=OFF ^
    -DOGG_INCLUDE_DIR="%DEPSF%/libogg/include" ^
    -DOGG_LIBRARY="%OGG_LIB%"
if errorlevel 1 goto :fail
call :build_configs "%DEPS%\libvorbis\build"
if errorlevel 1 goto :fail

rem ---------- 3/5 assimp (DLL) ----------
echo.
echo === [3/5] assimp ===
cmake -S "%DEPS%\assimp" -B "%DEPS%\assimp\build" -G "%GEN%" -A x64 ^
    -DBUILD_SHARED_LIBS=ON ^
    -DASSIMP_BUILD_ASSIMP_TOOLS=OFF ^
    -DASSIMP_BUILD_TESTS=OFF ^
    -DASSIMP_BUILD_ZLIB=ON ^
    -DASSIMP_INSTALL=ON
if errorlevel 1 goto :fail
call :build_configs "%DEPS%\assimp\build"
if errorlevel 1 goto :fail

rem ---------- 4/5 openal-soft (DLL) ----------
echo.
echo === [4/5] openal-soft ===
cmake -S "%DEPS%\openal-soft" -B "%DEPS%\openal-soft\build" -G "%GEN%" -A x64
if errorlevel 1 goto :fail
call :build_configs "%DEPS%\openal-soft\build"
if errorlevel 1 goto :fail

rem ---------- 5/5 freetype (static) ----------
echo.
echo === [5/5] freetype ===
cmake -S "%DEPS%\freetype" -B "%DEPS%\freetype\build" -G "%GEN%" -A x64
if errorlevel 1 goto :fail
call :build_configs "%DEPS%\freetype\build"
if errorlevel 1 goto :fail

echo.
echo ============================================================
echo  All dependencies built successfully.
echo  Next: premake5 vs2022, then build VFEngine/VertexForge.sln
echo ============================================================
exit /b 0

rem ---------- helpers ----------
:build_configs
if defined BUILD_DEBUG (
    echo --- %~1 [Debug] ---
    cmake --build "%~1" --config Debug --parallel
    if errorlevel 1 exit /b 1
)
if defined BUILD_RELEASE (
    echo --- %~1 [Release] ---
    cmake --build "%~1" --config Release --parallel
    if errorlevel 1 exit /b 1
)
exit /b 0

:fail
echo.
echo [ERROR] Dependency build failed - see output above.
exit /b 1
