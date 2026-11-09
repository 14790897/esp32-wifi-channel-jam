@echo off
setlocal
set "MSYSTEM="
set "SHELL="
if not defined IDF_PATH (
    echo Please set IDF_PATH to your ESP-IDF directory.
    exit /b 1
)
call "%IDF_PATH%\export.bat"
if errorlevel 1 goto :fail
cd /d "%~dp0"
set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM4"
idf.py -p %PORT% flash
exit /b %errorlevel%
:fail
exit /b 1
