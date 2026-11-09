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
    idf.py set-target esp32c6
    if errorlevel 1 goto :fail
)
idf.py reconfigure
if errorlevel 1 goto :fail
idf.py build
if errorlevel 1 goto :fail
idf.py -p COM20 flash
if errorlevel 1 goto :fail
echo === PHYJAM_C6_OK ===
exit /b 0
:fail
echo === FAILED ===
exit /b 1
