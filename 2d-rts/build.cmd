@echo off
setlocal

set RAYLIB_INCLUDE=%USERPROFILE%\scoop\apps\vcpkg\current\installed\x64-windows\include
set RAYLIB_LIB=%USERPROFILE%\scoop\apps\vcpkg\current\installed\x64-windows\lib

REM Compile and link using Zig
zig cc main.c -o rts.exe ^
  -I%RAYLIB_INCLUDE% ^
  -L%RAYLIB_LIB% ^
  -lraylib ^
  -lgdi32 -lwinmm -luser32 -lshell32 ^
  -target x86_64-windows ^
  -O2

REM Check if compilation succeeded
if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    exit /b %ERRORLEVEL%
)

echo Build succeeded! Run rts.exe to play.
endlocal
