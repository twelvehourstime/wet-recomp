@echo off
rem Starts the wet-recomp installer (Install-Wet.ps1 in this folder).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Wet.ps1" %*
echo.
pause
