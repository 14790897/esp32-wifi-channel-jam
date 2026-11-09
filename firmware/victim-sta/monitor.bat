@echo off
setlocal
set "MSYSTEM="
set "SHELL="
if not defined IDF_PATH (
    echo Please set IDF_PATH to your ESP-IDF directory.
    exit /b 1
)
call "%IDF_PATH%\export.bat"
cd /d "%~dp0"
idf.py -B build -p COM6 monitor
