@echo off
setlocal

echo Building tests...
if not exist build mkdir build

gcc -O2 -Iinclude tests\test_main.c src\parse.c src\history.c -o build\tests.exe
if %ERRORLEVEL% neq 0 goto :err

build\tests.exe
if %ERRORLEVEL% neq 0 goto :err

echo Tests passed
goto :eof

:err
echo Tests failed
exit /b 1
