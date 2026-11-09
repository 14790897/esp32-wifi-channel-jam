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
if not exist sdkconfig (
    idf.py -B build_c5 set-target esp32c5
    if errorlevel 1 goto :fail
)
idf.py -B build_c5 reconfigure
if errorlevel 1 goto :fail
idf.py -B build_c5 build
if errorlevel 1 goto :fail
idf.py -B build_c5 -p COM12 flash
if errorlevel 1 goto :fail
echo === C5_OK ===
exit /b 0
:fail
echo === FAILED ===
exit /b 1
