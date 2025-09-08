# ESP32 PC Serial Bridge

A comprehensive PC-to-wireless bridge system for testing ESP32 remote control functionality without physical remote controllers.

## 📋 Overview

The PC Serial Bridge enables you to:
- **Control remote devices** from your PC via USB/Serial
- **Test wireless protocols** (ESP-NOW, NRF24L01+) without hardware remotes
- **Switch protocols** runtime without reflashing firmware
- **Monitor real-time status** and performance metrics
- **Automate testing** with Python scripts

## 🏗️ System Architecture

```
PC (Python Client) ←→ ESP32 (Bridge) ←→ [ESP-NOW/NRF24] ←→ Remote Device(s)
```

## 📁 Files in this Directory

| File | Description |
|------|-------------|
| `pc_serial_bridge.cpp` | ESP32 bridge firmware (Arduino sketch) |
| `mock_receiver.cpp` | ESP32 mock receiver with LED control |
| `esp32_bridge_client.py` | Full-featured Python client with interactive mode |
| `simple_test.py` | Minimal example for quick testing |
| `led_control_demo.py` | Complete LED control demonstration |
| `requirements.txt` | Python dependencies |
| `run_client.bat/.sh` | Platform-specific client launchers |
| `run_led_demo.bat` | LED demo launcher for Windows |
| `PC_Bridge_Usage_Guide.md` | Detailed technical documentation |
| `README.md` | This file - quick start guide |

## 🚀 Quick Start

### 1. Hardware Setup

#### ESP32 Bridge Device
- Connect ESP32 to PC via USB
- For **NRF24L01+ support**, connect radio module:

```
NRF24L01+    ESP32
VCC      →   3.3V
GND      →   GND  
CE       →   GPIO (see esp32_rc_user_config.h)
CSN      →   GPIO (see esp32_rc_user_config.h)  
SCK      →   GPIO (see esp32_rc_user_config.h)
MOSI     →   GPIO (see esp32_rc_user_config.h)
MISO     →   GPIO (see esp32_rc_user_config.h)
```

### 2. Flash ESP32 Bridge Firmware

1. Open `pc_serial_bridge.cpp` in Arduino IDE or PlatformIO
2. Install required libraries:
   - ESP32 Remote Control Library (this library)
   - ArduinoJson (v6.19+)
3. Flash to your ESP32
4. Open Serial Monitor (115200 baud) to verify operation

Expected startup output:
```json
{"bridge":"ESP32_RC_Bridge", "version":"1.0.0", "status":"starting"}
{"status":"protocol_switched", "protocol":"espnow"}  
{"status":"bridge_ready", "default_protocol":"espnow"}
```

### 3. Install Python Dependencies

```bash
cd examples/pc_serial_bridge
pip install -r requirements.txt
```

### 4. Run Quick Test

```bash
# Replace COM3 with your ESP32's port
python simple_test.py COM3
```

### 5. Run Interactive Client

```bash
# Auto-detect port and run interactive mode
python esp32_bridge_client.py

# Specify port and initial protocol  
python esp32_bridge_client.py --port COM3 --protocol espnow

# Run automated demo
python esp32_bridge_client.py --demo --duration 30
```

## 🎮 Usage Examples

### Interactive Mode Commands

```bash
> data 45.0 30.0 1 0 3           # Send joystick data (X=45, Y=30, ID1=1, flags=3)
> switch nrf24                   # Switch to NRF24 protocol
> status                         # Get bridge status  
> help                           # Show available commands
> demo                          # Run automated demo sequence
> quit                          # Exit
```

### Python Script Integration

```python
import serial
import json
import time

# Connect to bridge
bridge = serial.Serial('COM3', 115200)
time.sleep(2)

# Send remote control data
command = {
    "cmd": "data",
    "v1": 45.0,    # Joystick X
    "v2": 30.0,    # Joystick Y  
    "v3": 75.0,    # Throttle
    "id1": 1,      # Controller ID
    "flags": 3     # Button states
}

bridge.write(json.dumps(command).encode() + b'\\n')

# Read response
response = bridge.readline().decode()
print(f"Bridge: {response}")
```

## 📡 Supported Commands

### Data Command
Send remote control data to wireless devices:
```json
{
  "cmd": "data",
  "v1": 45.0, "v2": 30.0, "v3": 0.0, "v4": 0.0, "v5": 0.0,
  "id1": 1, "id2": 0, "id3": 0, "id4": 0,
  "flags": 3
}
```

### Protocol Switch
Switch between wireless protocols:
```json
{"cmd": "switch", "protocol": "espnow"}
{"cmd": "switch", "protocol": "nrf24"}
```

### Status Query
Get comprehensive bridge status:
```json
{"cmd": "status"}
```

### Help Command
Get available commands and usage:
```json
{"cmd": "help"}
```

## 📊 Response Format

### Successful Data Transmission
```json
{"status":"data_sent", "protocol":"espnow", "timestamp":12345}
```

### Received Data from Remote Device
```json
{
  "event": "data_received",
  "protocol": "espnow", 
  "v1": 25.6, "v2": 30.2, "v3": 0.0, "v4": 0.0, "v5": 0.0,
  "id1": 2, "id2": 0, "id3": 0, "id4": 0,
  "flags": 7,
  "timestamp": 15678
}
```

### Bridge Status
```json
{
  "status": {
    "protocol": "espnow",
    "connection": "connected", 
    "send_metrics": {"success": 45, "failed": 2, "total": 47, "rate": 95.7},
    "recv_metrics": {"success": 38, "failed": 1, "total": 39, "rate": 97.4},
    "uptime_ms": 123456
  }
}
```

### Error Response
```json
{"error": "protocol_not_initialized"}
{"error": "send_failed"}
{"error": "invalid_protocol", "supported": ["espnow", "nrf24"]}
```

## 🔧 Troubleshooting

### Bridge Not Found
1. **Check USB connection** - Ensure ESP32 is properly connected
2. **Verify port** - Use Device Manager (Windows) or `ls /dev/tty*` (Linux/Mac)
3. **Check drivers** - Install CH340, CP210x, or FTDI drivers as needed
4. **Test with Serial Monitor** - Verify bridge firmware is running

### Communication Issues
1. **Baud rate** - Ensure 115200 baud rate in both bridge and client
2. **Port permissions** - On Linux/Mac: `sudo chmod 666 /dev/ttyUSB0`
3. **JSON format** - Ensure commands are valid JSON with `\\n` terminator
4. **Buffer overflow** - Commands must be < 512 bytes

### Wireless Connection Problems
1. **Protocol mismatch** - Ensure remote device uses same protocol
2. **Range issues** - Test devices within 10m range initially
3. **Channel conflicts** - Check WiFi channels for ESP-NOW
4. **Hardware connections** - Verify NRF24 wiring if using nRF24L01+

### Performance Issues
1. **High CPU usage** - Reduce command frequency (< 100Hz recommended)
2. **Missed packets** - Check wireless signal strength and interference
3. **Slow responses** - Ensure ESP32 has adequate power supply (500mA+)

## 🎯 Testing Scenarios

### 1. Basic Remote Control
```python
# Simulate joystick input
bridge.write(b'{"cmd":"data", "v1":45.0, "v2":30.0, "id1":1, "flags":1}\\n')

# Simulate button press
bridge.write(b'{"cmd":"data", "v1":0.0, "v2":0.0, "id1":1, "flags":15}\\n')
```

### 2. Protocol Comparison
```python
# Test ESP-NOW performance
bridge.write(b'{"cmd":"switch", "protocol":"espnow"}\\n')
# ... send test data and measure response times

# Test NRF24 performance  
bridge.write(b'{"cmd":"switch", "protocol":"nrf24"}\\n')
# ... send same test data and compare
```

### 3. Stress Testing
```python
# High-frequency data transmission
for i in range(1000):
    data = f'{{"cmd":"data", "v1":{i%100}, "id1":1}}'
    bridge.write((data + '\\n').encode())
    time.sleep(0.01)  # 100Hz
```

## 🧪 Complete Testing Setup (Mock Receiver)

For a complete end-to-end demonstration, use the included mock receiver firmware that responds to bridge commands by controlling an LED.

### Hardware Setup for Testing
```
PC (Python) ←→ ESP32 #1 (Bridge) ←→ [Wireless] ←→ ESP32 #2 (Mock Receiver + LED)
```

### Mock Receiver Setup

1. **Flash Mock Receiver Firmware**
   - Open `mock_receiver.cpp` in Arduino IDE
   - Select your protocol: `#define MOCK_PROTOCOL ESP32_RC_ESPNOW` (or `ESP32_RC_NRF24`)
   - Flash to a second ESP32 device
   - Open Serial Monitor (115200 baud) to see received data

2. **LED Control Behaviors**
   - **v1 > 50**: Fast blink (100ms)
   - **v1 20-50**: Medium blink (250ms) 
   - **v1 < 20**: Slow blink (500ms)
   - **flags & 1**: LED forced ON
   - **flags & 2**: LED forced OFF
   - **id1 > 0**: Burst blink N times

### Run Complete Demo

```bash
# Automated LED pattern demo
python led_control_demo.py COM3 --mode demo --duration 60

# Interactive LED control
python led_control_demo.py COM3 --mode interactive

# Sine wave brightness control
python led_control_demo.py COM3 --mode sine --duration 30

# Windows: Double-click launcher
run_led_demo.bat
```

### Interactive Commands
```bash
> fast          # Fast LED blink
> medium        # Medium LED blink
> slow          # Slow LED blink
> on            # LED always ON
> off           # LED always OFF  
> burst 5       # Blink 5 times rapidly
> switch nrf24  # Switch to NRF24 protocol
> status        # Get bridge status
```

### Expected Output
**Bridge Device:**
```json
{"status":"data_sent", "protocol":"espnow", "timestamp":12345}
```

**Mock Receiver Serial Output:**
```
[15678] RECEIVED DATA:
  IDs: 1, 0, 0, 0
  Values: 80.00, 0.00, 0.00, 0.00, 0.00
  Flags: 0x00 (0)
  Flags: NORMAL_BLINK
  LED: FAST BLINK (80%)
  Total packets: 42, Protocol: ESPNOW
```

## 🛠️ Integration Examples

### Unity Game Engine
```csharp
// C# script for Unity integration
using System.IO.Ports;
using UnityEngine;

public class ESP32Bridge : MonoBehaviour {
    SerialPort bridge = new SerialPort("COM3", 115200);
    
    void Start() {
        bridge.Open();
    }
    
    void Update() {
        float x = Input.GetAxis("Horizontal");
        float y = Input.GetAxis("Vertical");
        
        string cmd = $"{{\\\"cmd\\\":\\\"data\\\", \\\"v1\\\":{x}, \\\"v2\\\":{y}}}\\n";
        bridge.Write(cmd);
    }
}
```

### MATLAB/Simulink
```matlab
% MATLAB serial communication
bridge = serial('COM3', 'BaudRate', 115200);
fopen(bridge);

% Send data
data = struct('cmd', 'data', 'v1', 45.0, 'v2', 30.0, 'id1', 1);
fprintf(bridge, '%s\\n', jsonencode(data));

% Read response
response = fscanf(bridge);
disp(response);
```

## 📈 Advanced Features

- **Real-time monitoring** with timestamp correlation
- **Automatic protocol fallback** on connection loss
- **Command queuing** for batch operations
- **Metrics logging** for performance analysis
- **Multi-device support** via device IDs

For detailed technical information, see `PC_Bridge_Usage_Guide.md`.

---

## 📞 Support

If you encounter issues:
1. Check this README and troubleshooting section
2. Review `PC_Bridge_Usage_Guide.md` for detailed documentation  
3. Test with `simple_test.py` to isolate issues
4. Open an issue on the ESP32 Remote Control Library repository