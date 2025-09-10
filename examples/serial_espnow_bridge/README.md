# ESP32 Serial-to-ESPNOW Transparent Bridge

A simple transparent bridge that passes raw serial data through ESPNOW protocol without any command parsing or protocol-specific logic.

## 📋 Overview

This bridge provides:
- **Transparent data passthrough** between Serial and ESPNOW
- **Raw byte mapping** to ESPNOW packet format (RCPayload_t)
- **Bidirectional communication** (Serial ↔ ESPNOW)
- **No protocol logic** - just passes data through
- **Simple operation** - no JSON commands or complex interfaces

## 📁 Files in this Directory

| File | Description |
|------|-------------|
| `serial_espnow_bridge.cpp` | ESP32 transparent bridge firmware (Arduino sketch) |
| `keyboard_serial.py` | Python keyboard input client for testing the bridge |
| `README.md` | This file - usage guide |

## 🏗️ System Architecture

```
PC/Device (Raw Bytes) ←→ ESP32 (Transparent Bridge) ←→ [ESP-NOW] ←→ Remote Device(s)
```

## 🚀 Quick Start

### 1. Hardware Setup

#### ESP32 Bridge Device
- Connect ESP32 to PC via USB
- No additional hardware required for ESP-NOW

### 2. Flash Bridge Firmware

1. Open `serial_espnow_bridge.cpp` in Arduino IDE or PlatformIO
2. Install required libraries:
   - ESP32 Remote Control Library (this library)
3. Flash to your ESP32
4. Open Serial Monitor (115200 baud) to verify operation

Expected startup output:
```
ESP32 Serial-to-ESPNOW Bridge Starting...
ESPNOW controller initialized
```

### 3. Usage

#### Send Data (Serial → ESPNOW)
Send raw bytes via serial terminal or application:
- Up to 25 bytes per packet (RCPayload_t size)
- Bytes are mapped directly to ESPNOW payload structure
- Debug output shows hex representation of sent data

#### Receive Data (ESPNOW → Serial)  
- Incoming ESPNOW packets are forwarded as raw bytes to serial
- Debug output shows hex representation of received data

## 📡 Data Format

### RCPayload_t Structure (25 bytes)
```cpp
struct RCPayload_t {
  uint8_t id1;        // Byte 0
  uint8_t id2;        // Byte 1  
  uint8_t id3;        // Byte 2
  uint8_t id4;        // Byte 3
  float value1;       // Bytes 4-7
  float value2;       // Bytes 8-11
  float value3;       // Bytes 12-15
  float value4;       // Bytes 16-19
  float value5;       // Bytes 20-23
  uint8_t flags;      // Byte 24
};
```

### Example Data Mapping
Sending 25 bytes: `01 02 03 04 42 28 00 00 42 F0 00 00 43 70 00 00 44 16 00 00 44 7A 00 00 FF`

Maps to:
- id1=1, id2=2, id3=3, id4=4
- value1=42.0, value2=120.0, value3=240.0, value4=600.0, value5=1000.0  
- flags=255

## 🔧 Integration Examples

### Python Serial Communication
```python
import serial
import struct

# Connect to bridge
bridge = serial.Serial('COM3', 115200)

# Send structured data
id1, id2, id3, id4 = 1, 2, 3, 4
v1, v2, v3, v4, v5 = 42.0, 120.0, 240.0, 600.0, 1000.0
flags = 255

# Pack data according to RCPayload_t structure
data = struct.pack('<BBBB5fB', id1, id2, id3, id4, v1, v2, v3, v4, v5, flags)
bridge.write(data)

# Read response (if any)
if bridge.in_waiting:
    received = bridge.read(25)  # Read 25 bytes
    unpacked = struct.unpack('<BBBB5fB', received)
    print(f"Received: IDs={unpacked[:4]}, Values={unpacked[4:9]}, Flags={unpacked[9]}")
```

### Arduino (Another ESP32)
```cpp
// Send data to bridge
RCPayload_t payload = {1, 2, 3, 4, 42.0, 120.0, 240.0, 600.0, 1000.0, 255};
Serial.write(reinterpret_cast<uint8_t*>(&payload), sizeof(payload));

// Receive data from bridge
if (Serial.available() >= sizeof(RCPayload_t)) {
    RCPayload_t received;
    Serial.readBytes(reinterpret_cast<uint8_t*>(&received), sizeof(received));
    // Process received data...
}
```

## 🎮 Python Keyboard Client

The included `keyboard_serial.py` provides an interactive keyboard interface for testing the bridge.

### Features
- **Interactive COM port selection** - Lists and lets you choose available ports
- **Command-based input** - Type commands instead of real-time keystrokes  
- **No external dependencies** - Only requires `pyserial`
- **Built-in help system** - Shows available commands and mappings

### Installation
```bash
pip install pyserial
```

### Usage
```bash
python keyboard_serial.py
```

### Example Session
```
ESP32 Serial-ESPNOW Keyboard Bridge

=== Available COM Ports ===
1. COM3 - USB-SERIAL CH340 (COM3)
2. COM12 - USB Serial Device (COM12)

Enter port number (1-2) or 'q' to quit:
> 1
✅ Connected to COM3 at 115200 baud

=== Available Commands ===
📝 Letters: a-z (ID1: 1-26)
🔢 Numbers: 0-9 (ID1: 48-57)  
⬆️  Arrows: up, down, left, right (Value1/Value2: ±100.0)
❓ Commands: help, status, quit/exit

Type a command and press Enter...

> a
📤 Sent 'a' (Letter A) -> ID1:1 V1:0.0 V2:0.0

> up
📤 Sent 'up' (Arrow Up) -> ID1:200 V1:100.0 V2:0.0

> 5
📤 Sent '5' (Number 5) -> ID1:53 V1:0.0 V2:0.0

> help
=== Available Commands ===
📝 Letters: a-z (ID1: 1-26)
🔢 Numbers: 0-9 (ID1: 48-57)  
⬆️  Arrows: up, down, left, right (Value1/Value2: ±100.0)
❓ Commands: help, status, quit/exit

> quit
👋 Goodbye!
```

### Key Mappings
| Input | RCPayload_t Field | Value | Description |
|-------|------------------|-------|-------------|
| `a`-`z` | `id1` | 1-26 | Letter mapping (a=1, b=2, etc.) |
| `0`-`9` | `id1` | 48-57 | ASCII values for numbers |
| `up` | `id1=200`, `value1` | 100.0 | Up arrow |
| `down` | `id1=201`, `value1` | -100.0 | Down arrow |
| `left` | `id1=202`, `value2` | -100.0 | Left arrow |
| `right` | `id1=203`, `value2` | 100.0 | Right arrow |

### Built-in Commands
- `help`, `?`, `h` - Show available commands
- `status`, `s` - Show connection status
- `clear` - Clear screen
- `quit`, `exit`, `q` - Exit program

### Testing Setup
For complete end-to-end testing:

1. **Flash bridge firmware** to ESP32 #1
2. **Flash receiving firmware** to ESP32 #2 (any ESPNOW-compatible device)
3. **Run Python client**: `python keyboard_serial.py`
4. **Send commands** and observe results on both devices

This setup allows you to test ESPNOW communication without physical input devices.

## 🔍 Debug Output

The bridge provides hex debug output for monitoring data flow:

**Sending data:**
```
Sent: 01 02 03 04 42 28 00 00 42 F0 00 00 43 70 00 00 44 16 00 00 44 7A 00 00 FF 
```

**Receiving data:**
```
Received: 05 06 07 08 41 20 00 00 42 48 00 00 42 C8 00 00 43 48 00 00 43 96 00 00 AA 
```

## 🛠️ Troubleshooting

### No Data Transmission
1. **Check ESPNOW peers** - Ensure receiving device is configured for ESPNOW
2. **Verify serial connection** - Test with simple bytes first
3. **Check packet size** - Maximum 25 bytes per transmission
4. **Range issues** - Keep devices within 10-50m range initially

### Data Corruption
1. **Serial buffer** - Ensure complete 25-byte packets are sent
2. **Byte ordering** - Verify little-endian format for multi-byte values
3. **Timing** - Add small delays between transmissions if needed

### Connection Issues
1. **Baud rate** - Ensure 115200 baud on both sides
2. **Port permissions** - On Linux/Mac: `sudo chmod 666 /dev/ttyUSB0`
3. **ESP32 power** - Ensure adequate power supply (500mA+)

## 🎯 Use Cases

### 1. Custom Protocol Bridge
Use this bridge to implement your own communication protocol on top of ESPNOW without modifying the ESP32 firmware.

### 2. Legacy System Integration  
Connect older systems or microcontrollers that output raw data streams to ESPNOW networks.

### 3. High-Performance Applications
Bypass JSON parsing overhead for applications requiring maximum throughput and minimum latency.

### 4. Development and Testing
Quickly prototype ESPNOW communication without writing complex firmware.

## ⚠️ Limitations

- **Fixed packet size**: 25 bytes per transmission (RCPayload_t structure)
- **No error checking**: Raw data passthrough without validation
- **ESPNOW only**: Bridge is hardcoded to use ESPNOW protocol  
- **No buffering**: Data is transmitted immediately when received
- **Single peer**: Broadcasts to all ESPNOW peers, no specific addressing

## 📈 Performance Characteristics

- **Latency**: ~1-5ms serial processing + ESPNOW transmission time
- **Throughput**: Limited by ESPNOW (~250kbps) and serial bandwidth
- **CPU usage**: Minimal - simple byte copying with no processing
- **Memory usage**: ~50 bytes for buffers and controller instance

---

This transparent bridge provides the simplest possible interface between serial communication and ESPNOW, making it ideal for applications that need raw data passthrough without protocol complexity.