@echo off
setlocal

set "SRC=build"
set "DEST=C:\Users\shiva\bins"

if not exist "%DEST%" mkdir "%DEST%"

rem Mirror the entire build folder into bins
robocopy "%SRC%" "%DEST%" /MIR >nul
set "RC=%ERRORLEVEL%"

rem Robocopy returns 0-7 for success/minor differences
if %RC% GEQ 8 exit /b %RC%
exit /b 0
