@echo off
REM LED Control Demo Launcher - Windows

echo ESP32 LED Control Demo
echo ======================
echo.
echo This demo requires:
echo 1. ESP32 Bridge device (pc_serial_bridge.cpp) connected via USB
echo 2. ESP32 Mock Receiver device (mock_receiver.cpp) powered and in range
echo 3. Watch the LED on the receiver device!
echo.

REM Check Python
python --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python not found. Please install Python 3.6+
    pause
    exit /b 1
)

REM Activate virtual environment if it exists
if exist venv\ (
    call venv\Scripts\activate.bat
)

REM Auto-detect COM port or ask user
set BRIDGE_PORT=
for /f "tokens=1" %%i in ('python -c "import serial.tools.list_ports; ports=serial.tools.list_ports.comports(); [print(p.device) for p in ports if any(k in p.description.lower() for k in ['ch340','cp210x','ftdi','esp32'])]"') do (
    set BRIDGE_PORT=%%i
    goto :found
)

:found
if "%BRIDGE_PORT%"=="" (
    echo Could not auto-detect ESP32 bridge port.
    set /p BRIDGE_PORT=Enter COM port (e.g., COM3): 
)

echo Using bridge port: %BRIDGE_PORT%
echo.

REM Choose demo mode
echo Select demo mode:
echo 1. Automated LED patterns (default)
echo 2. Interactive control
echo 3. Sine wave brightness
echo.
set /p DEMO_CHOICE=Enter choice (1-3, default=1): 
if "%DEMO_CHOICE%"=="" set DEMO_CHOICE=1

REM Run appropriate demo
if "%DEMO_CHOICE%"=="1" (
    echo Running automated LED demo...
    python led_control_demo.py %BRIDGE_PORT% --mode demo --duration 60
) else if "%DEMO_CHOICE%"=="2" (
    echo Starting interactive mode...
    python led_control_demo.py %BRIDGE_PORT% --mode interactive
) else if "%DEMO_CHOICE%"=="3" (
    echo Running sine wave demo...
    python led_control_demo.py %BRIDGE_PORT% --mode sine --duration 30
) else (
    echo Invalid choice, running default demo...
    python led_control_demo.py %BRIDGE_PORT% --mode demo --duration 60
)

echo.
echo Demo completed!
pause