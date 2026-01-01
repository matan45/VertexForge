@echo off
echo Generating Visual Studio 2022 solution with VS2026 toolset (v145)...
echo.

premake5 vs2022

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: premake5 failed to generate solution
    pause
    exit /b 1
)

echo.
echo Solution generated successfully in VFEngine/
echo Using: VS2026 toolset (v145) with latest Windows SDK
echo.
pause
