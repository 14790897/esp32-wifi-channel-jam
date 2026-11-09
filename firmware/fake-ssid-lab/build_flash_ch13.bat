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
idf.py -B build13 -DBOARD_INDEX=0 -DBOARD_CHANNEL=13 build
if errorlevel 1 goto :fail
idf.py -B build13 -p COM5 flash
if errorlevel 1 goto :fail
echo === FAKE13_OK ===
exit /b 0
:fail
echo === FAILED ===
exit /b 1
