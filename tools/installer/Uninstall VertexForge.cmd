@echo off
rem Double-click to uninstall this VertexForge (VK-1696). It runs bin\Uninstall.ps1, which asks Windows Installer to
rem remove the per-user MSI installed in this folder (msiexec /x), exactly like Settings > Apps > Installed apps.
rem It leaves this folder first, so neither this window nor PowerShell keeps it open while it is deleted; "& exit /b"
rem stays on the same line because cmd re-reads a batch file line by line, and the uninstall deletes this file.
setlocal
set "VF_UNINSTALL=%~dp0bin\Uninstall.ps1"
cd /d "%SystemRoot%"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%VF_UNINSTALL%" & exit /b
