@echo off
rem Double-click launcher for build-local-win11.ps1
rem Default builds the STATIC variant; pass -Link nonstatic (or both) for the
rem dynamically linked build, e.g.:
rem   build-local-win11.cmd -Link nonstatic
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-local-win11.ps1" %*
pause
