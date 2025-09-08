@echo off
REM ESP32 Bridge Client - Windows Batch File
REM Double-click this file to run the interactive client

echo ESP32 Remote Control Bridge Client
echo ====================================
echo.

REM Check if Python is installed
python --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python is not installed or not in PATH
    echo Please install Python 3.6+ from https://python.org
    pause
    exit /b 1
)

REM Install requirements if needed
if not exist venv\ (
    echo Setting up Python environment...
    python -m venv venv
    call venv\Scripts\activate.bat
    pip install -r requirements.txt
) else (
    call venv\Scripts\activate.bat
)

echo.
echo Starting ESP32 Bridge Client...
echo Commands: data, switch, status, help, demo, quit
echo.

REM Run the client
python esp32_bridge_client.py

echo.
echo Bridge client closed.
pause