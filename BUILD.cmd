@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
set "RC=%errorlevel%"
pause
exit /b %RC%
