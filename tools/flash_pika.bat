@echo off
setlocal
title Pika Note - Firmware updater
where py >nul 2>nul
if not errorlevel 1 (
    py -3 "%~dp0flash_pika.py" %*
    goto finished
)
where python >nul 2>nul
if not errorlevel 1 (
    python "%~dp0flash_pika.py" %*
    goto finished
)
echo Python was not found. Install Python 3.10 or newer from https://www.python.org/downloads/windows/
echo Enable the Python launcher or Add Python to PATH during installation.
pause
exit /b 1
:finished
set "pika_flash_result=%errorlevel%"
echo.
if not "%pika_flash_result%"=="0" echo Update did not finish. Read the error above before retrying.
pause
exit /b %pika_flash_result%
