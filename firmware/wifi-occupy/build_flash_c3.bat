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
if not exist build_c3 (
    idf.py -B build_c3 set-target esp32c3
    if errorlevel 1 goto :fail
)
idf.py -B build_c3 reconfigure
if errorlevel 1 goto :fail
idf.py -B build_c3 build
if errorlevel 1 goto :fail
echo === flash COM4 ===
idf.py -B build_c3 -p COM4 flash
if errorlevel 1 goto :fail
echo === flash COM5 ===
idf.py -B build_c3 -p COM5 flash
if errorlevel 1 goto :fail
echo === OCC_OK ===
exit /b 0
:fail
echo === FAILED ===
exit /b 1
