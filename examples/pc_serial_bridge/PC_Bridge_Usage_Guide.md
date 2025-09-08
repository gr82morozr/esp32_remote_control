# ESP32 PC Bridge Usage Guide

## Overview
The PC Bridge example (`pc_bridge.cpp`) creates a bidirectional bridge between PC commands (via USB/Serial) and wireless protocols (ESP-NOW/NRF24). This enables testing remote control functionality using PC-based commands without requiring physical remote controllers.

## Features
- ✅ **JSON Command Interface** - Clean, structured commands via Serial
- ✅ **Runtime Protocol Switching** - Switch between ESP-NOW and NRF24 on-the-fly  
- ✅ **Bidirectional Translation** - PC ↔ ESP32 ↔ Remote Device
- ✅ **Real-time Status** - Connection status, metrics, discovery results
- ✅ **Automatic Discovery** - Peer discovery and connection management

## Hardware Setup

### ESP32 Bridge Device
```
PC (USB) ←→ ESP32 (Bridge) ←→ [ESP-NOW/NRF24] ←→ Remote Device
```

### For NRF24L01+ Support
Connect NRF24L01+ module to ESP32 according to your pin configuration in `esp32_rc_user_config.h`:
```
NRF24L01+    ESP32
VCC      →   3.3V
GND      →   GND  
CE       →   PIN_NRF_CE
CSN      →   PIN_NRF_CSN
SCK      →   PIN_NRF_SCK
MOSI     →   PIN_NRF_MOSI
MISO     →   PIN_NRF_MISO
```

## Usage Instructions

### 1. Upload Firmware
1. Upload `pc_bridge.cpp` to your ESP32
2. Connect ESP32 to PC via USB
3. Open serial monitor at **115200 baud**

### 2. Bridge Startup
Upon startup, you'll see:
```json
{"bridge":"ESP32_RC_Bridge", "version":"1.0.0", "status":"starting"}
{"status":"protocol_switched", "protocol":"espnow"}
{"status":"bridge_ready", "default_protocol":"espnow"}
{"help":"Available commands: data, switch, status, discover, help"}
```

### 3. Available Commands

#### **Send Data Command**
Send remote control data to wireless devices:
```json
{"cmd":"data", "v1":45.0, "v2":30.0, "id1":1, "id2":2, "flags":3}
```

**Parameters:**
- `id1-id4`: Integer IDs (0-255) 
- `v1-v5`: Float values for analog channels
- `flags`: 8-bit flags field

**Response:**
```json
{"status":"data_sent", "protocol":"espnow", "timestamp":12345}
```

#### **Switch Protocol**
Switch between wireless protocols:
```json
{"cmd":"switch", "protocol":"espnow"}
{"cmd":"switch", "protocol":"nrf24"}
```

**Response:**
```json
{"status":"protocol_switched", "protocol":"nrf24"}
```

#### **Get Status**
Get comprehensive bridge status:
```json
{"cmd":"status"}
```

**Response:**
```json
{
  "status": {
    "protocol": "espnow",
    "connection": "connected",
    "send_metrics": {"success":45, "failed":2, "total":47, "rate":95.7},
    "recv_metrics": {"success":38, "failed":1, "total":39, "rate":97.4},
    "uptime_ms": 123456
  }
}
```

#### **Check Discovery**
Check peer discovery results:
```json
{"cmd":"discover"}
```

**Response:**
```json
{"discovery":{"status":"peer_found", "timestamp":12345, "info":"192.168.1.100"}}
```

#### **Help Command**
Get detailed command help:
```json
{"cmd":"help"}
```

## Received Data Events

When remote devices send data back, the bridge forwards it to PC:
```json
{
  "event": "data_received", 
  "protocol": "espnow",
  "id1": 1, "id2": 2, "id3": 0, "id4": 0,
  "v1": 25.6, "v2": 30.2, "v3": 0.0, "v4": 0.0, "v5": 0.0,
  "flags": 7,
  "timestamp": 15678
}
```

## Example Testing Scenarios

### 1. Basic Remote Control Test
```json
// Send joystick data (X=45.0, Y=30.0, Button1=pressed)
{"cmd":"data", "v1":45.0, "v2":30.0, "id1":1, "flags":1}

// Send trigger/throttle data  
{"cmd":"data", "v3":75.5, "v4":20.0, "id2":2, "flags":0}
```

### 2. Protocol Performance Comparison
```json
// Test ESP-NOW
{"cmd":"switch", "protocol":"espnow"}
{"cmd":"data", "v1":50.0, "id1":1}
{"cmd":"status"}

// Switch to NRF24
{"cmd":"switch", "protocol":"nrf24"} 
{"cmd":"data", "v1":50.0, "id1":1}
{"cmd":"status"}
```

### 3. Automated Testing Script
```python
import serial
import json
import time

# Connect to ESP32 bridge
bridge = serial.Serial('COM3', 115200)  # Adjust COM port

# Send test data
test_data = {
    "cmd": "data",
    "v1": 45.0,
    "v2": 30.0, 
    "id1": 1,
    "flags": 3
}

bridge.write(json.dumps(test_data).encode() + b'\n')

# Read response
response = bridge.readline().decode()
print(f"Bridge response: {response}")
```

## Error Handling

Common error responses:
```json
{"error":"json_parse_error", "message":"Invalid JSON syntax"}
{"error":"protocol_not_initialized"}
{"error":"send_failed"}
{"error":"unknown_command", "received":"invalid_cmd"}
{"error":"invalid_protocol", "supported":["espnow", "nrf24"]}
```

## Performance Notes

- **Reliable Mode**: Bridge uses reliable mode (queue depth 10) for stability
- **Low Latency**: ~1ms processing delay for command translation
- **Throughput**: Supports high-frequency data streams (limited by wireless protocol)
- **Memory**: Uses ~512 bytes for JSON parsing buffer

## Integration with Applications

The bridge can integrate with:
- **Python scripts** for automated testing
- **Unity/Unreal** game engines for game controller simulation  
- **MATLAB/Simulink** for control system testing
- **Web applications** via WebSerial API
- **Custom GUI applications** for manual testing

This bridge enables comprehensive testing of your ESP32 remote control library without requiring physical remote controllers, making development and debugging much more efficient.