# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

This is a PlatformIO ESP32 project with multiple communication protocols.

### Basic Build Commands
```bash
# Build project (default environment)
"C:\Users\gr82m\.platformio\penv\Scripts\pio.exe" run

# Build and upload to COM3 (comA environment)
"C:\Users\gr82m\.platformio\penv\Scripts\pio.exe" run -e comA

# Build and upload to COM12 (comB environment) 
"C:\Users\gr82m\.platformio\penv\Scripts\pio.exe" run -e comB

# Clean build
"C:\Users\gr82m\.platformio\penv\Scripts\pio.exe" run --target clean

# Monitor serial output (COM3)
"C:\Users\gr82m\.platformio\penv\Scripts\pio.exe" device monitor -p COM3 -b 115200

# Monitor serial output (COM12)
"C:\Users\gr82m\.platformio\penv\Scripts\pio.exe" device monitor -p COM12 -b 115200
```

### Platform Configuration
- Platform: ESP32 (espressif32)
- Framework: Arduino
- Board: esp32dev (generic development board)
- Flash mode: DIO (safer for clone boards)
- Upload speed: 921600 baud (robust setting)
- Monitor speed: 115200 baud

## Code Architecture

### Core Design Philosophy
This is a **symmetric communication library** where all devices run identical code. There are no traditional master/slave or sender/receiver roles - devices automatically discover each other and negotiate roles.

### Protocol Abstraction Layer
The library supports multiple wireless protocols through a common interface:

- **WiFi Raw 802.11** (`ESP32_RC_WIFI`) - Raw 802.11 frame injection with peer discovery
- **ESP-NOW** (`ESP32_RC_ESPNOW`) - Direct peer-to-peer communication  
- **nRF24L01+** (`ESP32_RC_NRF24`) - 2.4GHz radio communication

Switch protocols by changing one line in main.cpp:
```cpp
#define ESP32_RC_PROTOCOL ESP32_RC_WIFI      // WiFi mode
#define ESP32_RC_PROTOCOL ESP32_RC_ESPNOW    // ESP-NOW mode
#define ESP32_RC_PROTOCOL ESP32_RC_NRF24     // nRF24 mode
```

### Key Classes and Components

#### ESP32RemoteControl Base Class (`include/esp32_rc.h`)
- Pure virtual base class defining common interface
- Handles message queuing, heartbeat system, metrics tracking
- Manages connection states and peer discovery
- Two operating modes:
  - `fast_mode=false`: Queued/buffered sending (reliable)
  - `fast_mode=true`: Immediate sending (low latency)

#### Protocol Implementations
- **ESP32_RC_WIFI** (`src/esp32_rc_wifi.cpp`, `include/esp32_rc_wifi.h`)
- **ESP32_RC_ESPNOW** (`src/esp32_rc_espnow.cpp`, `include/esp32_rc_espnow.h`)
- **ESP32_RC_NRF24** (`src/esp32_rc_nrf24.cpp`, `include/esp32_rc_nrf24.h`)

#### Data Structures
- **RCPayload_t**: 25-byte payload (4 IDs, 5 float values, 1 flags byte)
- **RCMessage_t**: Full message with addressing and metadata
- **RCConnectionState_t**: Connection status tracking
- **Metrics_t**: Performance and reliability metrics

### Configuration System

#### User Configuration (`include/esp32_rc_user_config.h`)
All user-configurable settings:
- Pin assignments for protocols
- WiFi credentials and network settings
- Radio parameters (channels, power levels)
- Protocol-specific parameters

#### Internal Configuration (`include/esp32_rc_common.h`)
Framework internals and optimized defaults:
- Queue depths and timeouts
- Heartbeat intervals
- Memory management settings

### Key Features

#### Automatic Peer Discovery
- **WiFi**: Raw 802.11 probe request frame injection on fixed channel 6
- **ESP-NOW**: Direct device-to-device MAC-based discovery  
- **nRF24**: Radio channel scanning and addressing
- Built-in conflict resolution and deterministic role negotiation via MAC comparison

#### Connection Management
- Heartbeat system for connection monitoring
- Automatic reconnection on failure
- Connection state tracking (DISCONNECTED/CONNECTING/CONNECTED/ERROR)

#### Performance Monitoring
- Real-time metrics tracking (success rate, throughput, errors)
- Optional metrics display with timestamps
- Global metrics enable/disable control

#### Memory Management
- FreeRTOS queues for message buffering
- Semaphores for thread-safe operations
- Configurable queue depths

### Dependencies
- **esp32-common**: Custom utility library (https://github.com/gr82morozr/esp32-common.git)
- **RF24**: nRF24L01+ radio library (version ^1.4.11)

### Development Notes

#### Protocol Implementation Pattern
When adding new protocols:
1. Inherit from `ESP32RemoteControl`
2. Implement pure virtual methods: `getProtocol()`, `lowLevelSend()`, `parseRawData()`
3. Override `connect()` for protocol-specific initialization
4. Call `onDataReceived()` when messages arrive
5. Use `onPeerDiscovered()` during discovery phase

#### Testing and Examples
- `examples/` directory contains working examples for each protocol
- Each example demonstrates symmetric operation
- Use `src/README.txt` for detailed protocol documentation

#### Metrics and Debugging
- Enable metrics display: `controller->enableMetricsDisplay()`
- Monitor with timestamps: `monitor_filters = time` in platformio.ini
- Disable metrics for clean output: `ESP32RemoteControl::disableGlobalMetrics()`

## WiFi Protocol Implementation (Latest Update)

### Raw 802.11 Frame Discovery System
The WiFi protocol has been completely rebuilt according to `src/WiFi Build Details.txt` specifications:

#### Step 1: Raw Frame Injection Discovery
- **Channel**: Fixed channel 6 for all discovery operations
- **Frame Type**: 802.11 Probe Request frames with vendor-specific Information Elements
- **Frame Structure**: 
  ```c
  frame_control = 0x0040;        // Probe Request
  element_id = 221;              // Vendor specific IE  
  oui = [0x12, 0x34, 0x56];     // Custom vendor OUI
  ```
- **Discovery Process**: Periodic frame injection (500ms intervals) with sniff listening
- **Role Negotiation**: MAC address comparison (higher MAC becomes AP, lower becomes Station)

#### Step 3: Continuous Handshake Listening
- **Reboot Detection**: 10-second peer silence timeout triggers re-handshake
- **Persistent Monitoring**: Sniff mode remains active after discovery
- **Automatic Recovery**: Peer reboot detection restarts full discovery process
- **Background Frames**: Continued discovery frame injection at reduced rate when connected

#### Key Implementation Details
- **Frame Injection**: `esp_wifi_80211_tx()` for raw frame transmission
- **Sniff Mode**: `esp_wifi_set_promiscuous()` for frame reception
- **Vendor OUI**: `12:34:56` for discovery frame identification
- **Discovery Timeout**: 3 seconds for initial peer discovery
- **Sequence Numbers**: Incremental sequence control for frame tracking

#### Current Development Stage
- **Completed**: Steps 1 and 3 (Discovery + Continuous Listening)
- **Disabled**: WiFi connection, UDP handshake, and data transmission (for clean testing)
- **Next Steps**: WiFi AP/Station connection establishment (Step 2)

#### Build and Test Commands
```bash
# Build both environments for testing
"C:\Users\gr82m\.platformio\penv\Scripts\pio.exe" run

# Upload to device A (COM3)
"C:\Users\gr82m\.platformio\penv\Scripts\pio.exe" run -e comA -t upload

# Upload to device B (COM12)  
"C:\Users\gr82m\.platformio\penv\Scripts\pio.exe" run -e comB -t upload

# Monitor both devices simultaneously (separate terminals)
"C:\Users\gr82m\.platformio\penv\Scripts\pio.exe" device monitor -p COM3 -b 115200
"C:\Users\gr82m\.platformio\penv\Scripts\pio.exe" device monitor -p COM12 -b 115200
```

#### Expected Behavior
1. **Discovery Logs**: Frame injection success messages every 500ms
2. **Peer Detection**: MAC address logging when peer discovered  
3. **Role Assignment**: Clear AP/Station decision based on MAC comparison
4. **Continuous Monitoring**: Ongoing discovery frames and reboot detection
5. **Clean Output**: Metrics disabled, only essential discovery information logged

#### Troubleshooting
- **Frame Injection Errors**: Check for "ESP_ERR_INVALID_ARG" - indicates frame format issues
- **No Peer Discovery**: Verify both devices on same channel 6, check sniff mode setup
- **Role Conflicts**: MAC comparison should be deterministic - check MAC address logging
- **Build Errors**: Ensure ESP32 framework compatibility with `esp_wifi_80211_tx()` function