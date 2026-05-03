# ESP32 Serial-to-ESPNOW CSV Bridge

A simple bridge that maps newline-terminated CSV command packets to the legacy
`RCPayload_t` ESPNOW payload format, and prints fixed-point telemetry packets
from the dummy sensor as `RCPayload_I16x8_Time_t` fields.

## 📋 Overview

This bridge provides:
- **CSV text input** from Serial to ESPNOW
- **Structured text output** from ESPNOW to Serial
- **Bidirectional communication** (Serial ↔ ESPNOW)
- **Strict packet validation** before sending
- **Simple operation** - no JSON commands or complex interfaces

## 📁 Files in this Directory

| File | Description |
|------|-------------|
| `serial_espnow_bridge.cpp` | ESP32 CSV bridge firmware (Arduino sketch) |
| `keyboard_serial.py` | Python keyboard input client for testing the bridge |
| `README.md` | This file - usage guide |

## 🏗️ System Architecture

```
PC/Device (CSV Text) <-> ESP32 (CSV Bridge) <-> [ESP-NOW] <-> Remote Device(s)
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
4. Open Serial Monitor (230400 baud) to verify operation

Expected startup output:
```
ESP32 Serial-to-ESPNOW Bridge Starting...
ESPNOW controller initialized
```

### 3. Usage

#### Send Data (Serial → ESPNOW)
Send one newline-terminated CSV packet per message:
- Format: `id1,id2,id3,id4,value1,value2,value3,value4,value5,flags`
- Exactly 10 fields are required
- ID and flags fields must be integer values from 0 to 255
- Float fields must be valid numeric values
- Invalid input returns `RC_ERROR:bad_csv`

#### Receive Data (ESPNOW → Serial)  
- Incoming fixed-point telemetry packets are printed as `seq=<v>,sample_us=<v>,value0=<v>,value1=<v>,value2=<v>,value3=<v>,value4=<v>,value5=<v>,value6=<v>,value7=<v>,flags=<v>,reserved1=<v>,reserved2=<v>`
- Text output modes forward sensor-owned `RC_SCHEMA:...` metadata after link connection and every 20 seconds, with field names, types, scales, and units for compact telemetry parsing.
- Successfully submitted serial packets are echoed as `RC_SENT:id1=<v>,id2=<v>,id3=<v>,id4=<v>,value1=<v>,value2=<v>,value3=<v>,value4=<v>,value5=<v>,flags=<v>`

## 📡 Data Format

### Serial Command Input: `RCPayload_t` (25 bytes)
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

### ESP-NOW Telemetry Output: `RCPayload_I16x8_Time_t` (25 bytes)
```cpp
struct RCPayload_I16x8_Time_t {
  uint16_t seq;          // Bytes 0-1
  uint32_t sample_us;    // Bytes 2-5
  int16_t value[8];      // Bytes 6-21
  uint8_t flags;         // Byte 22
  uint8_t reserved1;     // Byte 23: schema ID
  uint8_t reserved2;     // Byte 24: schema version
};
```

The included dummy sensor maps telemetry as:

| Field | Scale | Description |
|-------|-------|-------------|
| `seq` | raw | 16-bit telemetry sequence counter |
| `sample_us` | raw | remote ESP32 `micros()` timestamp |
| `value[0]` | 0.01 C | temperature |
| `value[1]` | 0.001 V | voltage |
| `value[2]` | 0.01 | echoed command `value1` |
| `value[3]` | 0.01 | echoed command `value2` |
| `value[4]` | raw | echoed command `id1` |
| `value[5]` | raw | echoed command `id2` |
| `value[6]` | raw | echoed command `flags` |
| `value[7]` | ms | telemetry interval |
| `flags.0` | bit | set after any command is received |
| `reserved1` | raw | schema ID |
| `reserved2` | raw | schema version |

The dummy sensor sends schema metadata after the ESP-NOW link is connected and
then every 20 seconds. Text output modes forward the reassembled schema as:

```text
RC_SCHEMA:n=i16x8t;f=seq:u16:1,s_us:u32:us,v0:i16:.01:temp,...
```

Schema metadata is owned by the sensor firmware and sent over ESP-NOW as
`RCMSG_TYPE_SCHEMA` chunks. The bridge only reassembles and forwards it, so PC
software can learn the compact telemetry field mapping without the bridge
hardcoding those labels. Binary output mode suppresses `RC_SCHEMA:` text lines.

Schema text format:

```text
n=<schema_name>;f=<field>,<field>,...
```

Each field is colon-separated:

```text
<wire_name>:<type>:<scale_or_unit>[:<label>]
```

Full dummy-sensor schema:

```text
n=i16x8t;f=seq:u16:1,s_us:u32:us,v0:i16:.01:temp,v1:i16:.001:volt,v2:i16:.01:cmd1,v3:i16:.01:cmd2,v4:i16:1:id1,v5:i16:1:id2,v6:i16:1:cflg,v7:i16:ms:dt,fl:u8:seen,r1:u8:sid,r2:u8:sver
```

Token meanings:

- `n`: short schema name
- `f`: comma-separated field list
- `seq`, `s_us`, `v0`...`v7`, `fl`, `r1`, `r2`: compact wire field names
- `u8`, `u16`, `u32`, `i16`: integer storage type
- Numeric scale such as `.01`: multiply raw integer by that scale
- Unit-only token such as `us` or `ms`: timestamp/interval unit
- Final token such as `temp` or `sid`: display label or semantic name

For binary parsing on a PC, decode telemetry with little-endian format
`<HI8hBBB`.

### Example Telemetry Line

```text
seq=42,sample_us=12345678,value0=2501,value1=3300,value2=1000,value3=-500,value4=1,value5=2,value6=255,value7=10,flags=1,reserved1=1,reserved2=1
```

## 🔧 Integration Examples

### Python Serial Communication
```python
import serial

bridge = serial.Serial('COM3', 230400)

id1, id2, id3, id4 = 1, 2, 3, 4
v1, v2, v3, v4, v5 = 42.0, 120.0, 240.0, 600.0, 1000.0
flags = 255

# Send one legacy command payload as CSV text.
line = f"{id1},{id2},{id3},{id4},{v1},{v2},{v3},{v4},{v5},{flags}\n"
bridge.write(line.encode("ascii"))

# Read bridge echoes or fixed-point telemetry text.
print(bridge.readline().decode("ascii", errors="replace").strip())
```

### Arduino (Another ESP32)
```cpp
Serial.println("1,2,3,4,42.0,120.0,240.0,600.0,1000.0,255");

String line = Serial.readStringUntil('\n');
// Example telemetry line:
// seq=42,sample_us=12345678,value0=2501,value1=3300,...
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
✅ Connected to COM3 at 230400 baud

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
1. **Baud rate** - Ensure 230400 baud on both sides
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

This CSV bridge provides a simple structured text interface between serial communication and ESPNOW.
