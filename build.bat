@echo off
setlocal

echo Building shivishell...
if not exist build mkdir build

gcc -O2 -Iinclude src\*.c -o build\shivishell.exe
if %ERRORLEVEL% neq 0 goto :err

echo Built: build\shivishell.exe
goto :eof

:err
echo Build failed
pause

endlocal
exit /b 0