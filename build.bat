@echo off
echo Compiling...

del shivishell.exe 2>nul

gcc main.c -o shivishell.exe -static -static-libgcc -static-libstdc++

if %errorlevel% neq 0 (
    echo Compilation failed!
    pause
    exit /b 1
)

echo Compilation successful!
copy /Y .\shivishell.exe C:\Users\shiva\bins\shivishell.exe

echo Executable copied to C:\Users\shiva\bins\

pause
exit /b 0
