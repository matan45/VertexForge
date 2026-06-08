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
rem  If a configure fails (e.g. stale compiler path after a VS
rem  update), the build dir cache is wiped and configured fresh.
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
set "EXTRA_ARGS=-DBUILD_SHARED_LIBS=OFF"
call :configure_and_build "%DEPS%\libogg"
if errorlevel 1 goto :fail

rem ---------- 2/5 libvorbis (static, links against libogg) ----------
echo.
echo === [2/5] libvorbis ===
set "OGG_LIB=%DEPSF%/libogg/build/Release/ogg.lib"
if not defined BUILD_RELEASE set "OGG_LIB=%DEPSF%/libogg/build/Debug/ogg.lib"
set "EXTRA_ARGS=-DBUILD_SHARED_LIBS=OFF -DOGG_INCLUDE_DIR=%DEPSF%/libogg/include -DOGG_LIBRARY=%OGG_LIB%"
call :configure_and_build "%DEPS%\libvorbis"
if errorlevel 1 goto :fail

rem ---------- 3/5 assimp (DLL) ----------
echo.
echo === [3/5] assimp ===
set "EXTRA_ARGS=-DBUILD_SHARED_LIBS=ON -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ZLIB=ON -DASSIMP_INSTALL=ON"
call :configure_and_build "%DEPS%\assimp"
if errorlevel 1 goto :fail

rem ---------- 4/5 openal-soft (DLL) ----------
echo.
echo === [4/5] openal-soft ===
set "EXTRA_ARGS="
call :configure_and_build "%DEPS%\openal-soft"
if errorlevel 1 goto :fail

rem ---------- 5/5 freetype (static) ----------
echo.
echo === [5/5] freetype ===
set "EXTRA_ARGS="
call :configure_and_build "%DEPS%\freetype"
if errorlevel 1 goto :fail

echo.
echo ============================================================
echo  All dependencies built successfully.
echo  Next: premake5 vs2022, then build VFEngine/VertexForge.sln
echo ============================================================
exit /b 0

rem ---------- helpers ----------
:configure_and_build
set "SRC=%~1"
set "BLD=%~1\build"
cmake -S "%SRC%" -B "%BLD%" -G "%GEN%" -A x64 %EXTRA_ARGS%
if errorlevel 1 (
    echo [WARN] Configure failed - wiping stale CMake cache and retrying fresh...
    del /q "%BLD%\CMakeCache.txt" 2>nul
    rmdir /s /q "%BLD%\CMakeFiles" 2>nul
    cmake -S "%SRC%" -B "%BLD%" -G "%GEN%" -A x64 %EXTRA_ARGS%
    if errorlevel 1 exit /b 1
)
if defined BUILD_DEBUG (
    echo --- %SRC% [Debug] ---
    cmake --build "%BLD%" --config Debug --parallel
    if errorlevel 1 exit /b 1
)
if defined BUILD_RELEASE (
    echo --- %SRC% [Release] ---
    cmake --build "%BLD%" --config Release --parallel
    if errorlevel 1 exit /b 1
)
exit /b 0

:fail
echo.
echo [ERROR] Dependency build failed - see output above.
exit /b 1
