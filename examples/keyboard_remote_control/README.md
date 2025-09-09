# ESP32 Keyboard Remote Control Example

Complete demonstration of keyboard-controlled robot system using the ESP32 Remote Control library.

## Overview

This example shows how to create a keyboard-controlled remote system using three components:

1. **Python Controller** (`keyboard_controller.py`) - Captures keyboard input and sends commands
2. **ESP32 Bridge** (`../pc_serial_bridge/pc_serial_bridge.cpp`) - Translates PC commands to wireless
3. **ESP32 Receiver** (`keyboard_receiver.cpp`) - Receives commands and provides feedback

```
┌─────────────┐    USB     ┌─────────────┐   Wireless   ┌─────────────┐
│   PC with   │ ─────────► │ ESP32 Bridge│ ───────────► │ESP32 Robot  │
│  Keyboard   │            │ (PC Serial  │  (ESP-NOW/   │ (Receiver)  │
│             │            │   Bridge)   │   NRF24)     │             │
└─────────────┘            └─────────────┘              └─────────────┘
```

## Hardware Requirements

### ESP32 Bridge Device
- ESP32 development board
- USB connection to PC
- Running `pc_serial_bridge.cpp` firmware

### ESP32 Robot/Receiver Device  
- ESP32 development board
- Built-in LED or external LED on GPIO 2
- Optional: NRF24L01+ module (if using NRF24 protocol)

## Software Requirements

- **PC**: Python 3.7+ with `pyserial` library
- **ESP32**: PlatformIO with esp32_remote_control library

## Quick Start

### 1. Setup ESP32 Bridge
```bash
# Flash the PC Serial Bridge to first ESP32
cd examples/pc_serial_bridge
pio run -e comA -t upload    # Upload to COM3 (adjust as needed)
```

### 2. Setup ESP32 Receiver
```bash
# Flash the Keyboard Receiver to second ESP32  
cd examples/keyboard_remote_control
pio run -e comB -t upload    # Upload to COM12 (adjust as needed)
```

### 3. Install Python Dependencies
```bash
pip install pyserial
```

### 4. Run the Keyboard Controller
```bash
# Auto-detect bridge port
python keyboard_controller.py

# Or specify port directly
python keyboard_controller.py --port COM3

# Use NRF24 protocol instead of ESP-NOW
python keyboard_controller.py --port COM3 --protocol nrf24
```

## Usage

### Keyboard Controls
- **UP ARROW** - Move Forward
- **DOWN ARROW** - Move Backward  
- **LEFT ARROW** - Turn Left
- **RIGHT ARROW** - Turn Right
- **SPACE BAR** - Stop/Stand Still
- **ESC** - Exit Program

### Expected Behavior

1. **Python Controller**: Shows real-time command feedback with timestamps
2. **Bridge ESP32**: Confirms command transmission via serial (JSON responses)
3. **Receiver ESP32**: Echoes received commands with detailed interpretation

## Testing the System

### Option 1: Interactive Testing
Use the full keyboard controller for real-time interaction:
```bash
python keyboard_controller.py --port COM3
```

### Option 2: Automated Testing  
Use the test demo for automated command sequences:
```bash
python test_demo.py --port COM3
```

The test demo sends predefined movement commands to verify the complete communication chain.

## Command Protocol

The system uses a standardized command format:

### Command Values (value1)
- `0` = STOP - Robot stops and maintains balance
- `1` = FORWARD - Robot moves forward  
- `2` = BACKWARD - Robot moves backward
- `3` = TURN_LEFT - Robot turns left
- `4` = TURN_RIGHT - Robot turns right

### Parameters
- **value2**: Speed percentage (0-100) for forward/backward movements
- **value3**: Turn rate in degrees/second for turning movements  
- **id1/id2**: Command identifiers (0xAA, 0xBB magic bytes)
- **flags**: Command flags (bit 0 = command valid)

### JSON Message Format
```json
{
    "cmd": "data",
    "id1": 170,
    "id2": 187, 
    "v1": 1.0,
    "v2": 50.0,
    "v3": 30.0,
    "flags": 1
}
```

## Files Description

### `keyboard_controller.py`
- Main keyboard controller application
- Cross-platform keyboard input handling
- Real-time command transmission
- Colorized output with timestamps
- Serial port auto-detection
- Bridge status monitoring

### `keyboard_receiver.cpp`
- ESP32 firmware for receiving keyboard commands
- Command interpretation and echoing
- LED feedback for received commands
- Connection status monitoring
- Human-readable command output
- Simulated robot action feedback

### `test_demo.py`
- Automated testing script
- Predefined command sequences
- System verification tool
- Debugging assistance

## Protocol Support

The system supports multiple wireless protocols:

### ESP-NOW (Default)
- Direct ESP32-to-ESP32 communication
- No WiFi network required
- Low latency
- Automatic peer discovery

### NRF24L01+
- 2.4GHz radio communication
- Longer range option
- Requires NRF24 modules on both ESP32s
- Hardware SPI connection

Switch protocols using the `--protocol` parameter:
```bash
python keyboard_controller.py --port COM3 --protocol nrf24
```

## Troubleshooting

### No Bridge Response
- Verify ESP32 bridge is running `pc_serial_bridge.cpp` firmware
- Check correct serial port specified
- Ensure bridge device is powered and connected

### No Wireless Communication
- Confirm both ESP32s use the same protocol (ESP-NOW/NRF24)
- Check protocol is enabled in `esp32_rc_user_config.h`
- Verify devices are within wireless range
- For NRF24: Check SPI wiring and power supply

### Keyboard Input Issues
- Windows: Run as administrator if needed
- Linux: Add user to `dialout` group or use `sudo`
- Ensure terminal supports ANSI colors for best experience

### Serial Port Issues
```bash
# List available ports
python keyboard_controller.py --list-ports

# Test with different baud rates
python keyboard_controller.py --port COM3 --baud 9600
```

## Extending the Example

### Adding New Commands
1. Define new command constants in both Python and ESP32 code
2. Add command handling in `processKeyboardCommand()`
3. Update keyboard mapping in `control_loop()`

### Custom Robot Actions
Replace the simulated robot feedback in `keyboard_receiver.cpp` with:
- Motor control code
- Servo movements  
- Sensor readings
- Custom hardware interfaces

### Advanced Features
- Add joystick support
- Implement proportional control
- Add telemetry feedback
- Create GUI interface

## License

This example is part of the esp32_remote_control library.
Use freely for educational and personal projects.

## See Also

- [PC Serial Bridge Example](../pc_serial_bridge/) - Generic wireless bridge
- [ESP32 Remote Control Library](../../) - Main library documentation  
- [Protocol Configuration](../../include/esp32_rc_user_config.h) - Hardware setup