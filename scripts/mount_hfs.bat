@echo off
setlocal

if "%~1"=="" (
    echo Usage:
    echo   mount_hfs.bat ^<path_to_dmg_or_drive^> ^<drive_letter^>
    echo Examples:
    echo   mount_hfs.bat C:\Images\MacDisk.dmg H:
    echo   mount_hfs.bat \\.\PhysicalDrive1 H:
    exit /b 1
)

set DEVICE=%~1
set MOUNT=%~2
if "%MOUNT%"=="" set MOUNT=H:

echo Mounting HFS+ [%DEVICE%] to [%MOUNT%]...

:: Check if hfsplus.exe exists in build directories
set EXE=
if exist "%~dp0..\build\Release\hfsplus.exe" set EXE=%~dp0..\build\Release\hfsplus.exe
if exist "%~dp0..\build\hfsplus.exe" set EXE=%~dp0..\build\hfsplus.exe
if exist "%~dp0..\build-x64\hfsplus.exe" set EXE=%~dp0..\build-x64\hfsplus.exe
if exist "%~dp0..\dist\hfsplus.exe" set EXE=%~dp0..\dist\hfsplus.exe
if exist "%~dp0hfsplus.exe" set EXE=%~dp0hfsplus.exe

if "%EXE%"=="" (
    echo Error: hfsplus.exe binary not found. Please build the project first.
    exit /b 1
)

"%EXE%" -v "%DEVICE%" -m "%MOUNT%"
