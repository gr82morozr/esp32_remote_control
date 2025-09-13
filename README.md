# ESP32 Remote Control Library

A versatile ESP32 wireless communication library that enables symmetric peer-to-peer communication using multiple wireless protocols. This library allows ESP32 devices to automatically discover each other and establish bi-directional communication without predefined master/slave roles.

## Protocol Design Overview

### Communication Architecture
All protocols implement a unified 3-layer communication stack:

**Layer 1: Discovery & Handshake**
- **ESP-NOW**: Direct MAC-based peer discovery using broadcast messages
- **NRF24L01+**: Radio channel scanning with address exchange handshake protocol

**Layer 2: Connection Management**
- **Heartbeat System**: 100ms interval heartbeat messages for connection monitoring
- **Timeout Handling**: 300ms connection timeout triggers automatic reconnection
- **State Tracking**: Real-time connection states (DISCONNECTED/CONNECTING/CONNECTED/ERROR)

**Layer 3: Data Transport**
- **Standardized Packet**: 32-byte fixed-size messages across all protocols
- **Address Routing**: 6-byte MAC address sender identification
- **Payload Structure**: 25-byte structured data payload with type safety

### Message Format
```cpp
// 32-byte message structure (consistent across all protocols)
struct RCMessage_t {
    uint8_t type;                   // Message type (DATA=0, HEARTBEAT=3)
    uint8_t from_addr[6];          // Sender MAC address
    RCPayload_t payload[25];       // Structured data payload
};

// 25-byte data payload
struct RCPayload_t {
    uint8_t id1, id2, id3, id4;    // Command/identifier bytes
    float value1, value2, value3, value4, value5;  // Sensor/control values
    uint8_t flags;                 // Status/control flags
};
```

### Protocol-Specific Implementation

**ESP-NOW Protocol:**
- **Discovery**: Broadcast ESP-NOW messages to `FF:FF:FF:FF:FF:FF`
- **Pairing**: Automatic peer addition using `esp_now_add_peer()`
- **Transport**: Direct device-to-device communication via ESP32 hardware
- **Reliability**: Built-in ESP-NOW acknowledgments and retries

**NRF24L01+ Protocol:**
- **Discovery**: Broadcasts on shared channel using 5-byte broadcast address `{0xF0,0xF0,0xF0,0xF0,0xAA}`
- **Handshake**: Address exchange protocol converts 6-byte MAC to 5-byte NRF addresses
- **Pipe Switching**: Dynamic switching between broadcast (discovery) and peer (data) pipes
- **Transport**: Hardware automatic retries (5 attempts) with acknowledgment packets


## ⚠️ Protocol Status

**Ready for Production:**
- ✅ **ESP-NOW** - Direct peer-to-peer communication using ESP32's built-in ESP-NOW protocol
- ✅ **NRF24L01+** - 2.4GHz radio communication using nRF24L01+ modules

**Under Development:**
- 🔄 **WiFi TCP/UDP** - WiFi network-based communication protocol

**Future Development:**
- ❌ **Bluetooth LE** - Planned for future implementation

## Key Features

### Symmetric Communication
- **No Master/Slave Roles**: All devices run identical code and automatically negotiate roles
- **Automatic Peer Discovery**: Devices discover each other without manual configuration
- **Bi-directional Communication**: Any device can send or receive data seamlessly

### Protocol Abstraction
- **Unified API**: Switch between protocols with a single line change
- **Consistent Interface**: Same API regardless of underlying wireless technology
- **Protocol-Specific Optimization**: Each protocol optimized for best performance

### Advanced Features
- **Real-time Metrics**: Success rates, throughput monitoring, and error tracking with TPS calculation
- **Connection Management**: Automatic heartbeat system (100ms) and reconnection on failure (300ms timeout)
- **Memory Efficient**: FreeRTOS queues and optimized message structures
- **Fast/Reliable Modes**: Choose between low-latency (1 message queue) or guaranteed delivery (10 message queue)
- **Global Metrics Control**: Enable/disable metrics calculation globally for performance optimization
- **Heartbeat Exclusion**: Heartbeat messages excluded from send metrics to avoid skewing statistics
- **Custom Payload Override**: Override RCPayload_t structure for protocol-specific optimizations

## Quick Start

### 1. Hardware Setup

**For ESP-NOW (Recommended for beginners):**
- 2x ESP32 development boards
- No additional hardware required

**For NRF24L01+:**
- 2x ESP32 development boards  
- 2x NRF24L01+ modules
- Proper wiring (see Pin Configuration section)

### 2. Protocol Selection

Change the protocol in your main.cpp:

```cpp
// ESP-NOW Protocol (easiest setup)
#define ESP32_RC_PROTOCOL RC_PROTO_ESPNOW

// NRF24L01+ Protocol  
#define ESP32_RC_PROTOCOL RC_PROTO_NRF24

#include "esp32_rc_factory.h"
```

### 3. Basic Usage

```cpp
#include <Arduino.h>
#include "esp32_rc_factory.h"

// Create controller instance
ESP32RemoteControl* controller = nullptr;

void onDataReceived(const RCMessage_t& msg) {
    // Handle received data
    const RCPayload_t* payload = msg.getPayload();
    Serial.printf("Received: id1=%d, value1=%.2f\n", 
                  payload->id1, payload->value1);
}

void setup() {
    Serial.begin(115200);
    
    // Create controller based on selected protocol  
    controller = createProtocolInstance(ESP32_RC_PROTOCOL, false);
    if (controller) {
        controller->setOnRecieveMsgHandler(onDataReceived);
        
        // Start connection
        controller->connect();
        Serial.println("Connecting...");
    } else {
        Serial.println("Failed to create controller!");
    }
}

void loop() {
    // Send data every second
    static uint32_t lastSend = 0;
    if (millis() - lastSend > 1000) {
        RCPayload_t payload = {0};
        payload.id1 = 42;
        payload.value1 = random(0, 100) / 100.0f;
        
        if (controller) {
            controller->sendData(payload);
        }
        lastSend = millis();
    }
    
    delay(10);
}
```

## Pin Configuration

### NRF24L01+ Wiring

```
ESP32 Pin    →    NRF24L01+ Pin
GPIO 17      →    CE
GPIO 5       →    CSN (CS)
GPIO 18      →    SCK
GPIO 19      →    MISO
GPIO 23      →    MOSI
3.3V         →    VCC
GND          →    GND
```

**Note**: NRF24L01+ modules require stable 3.3V power. Use decoupling capacitors if experiencing connection issues.

### ESP-NOW Configuration

ESP-NOW uses built-in ESP32 hardware - no external components required. Default channel is 2, configurable in `include/esp32_rc_user_config.h`.

## Message Structure

The library uses a standardized 32-byte message format:

```cpp
struct RCPayload_t {
    uint8_t id1, id2, id3, id4;    // 4 ID bytes
    float value1, value2, value3, value4, value5;  // 5 float values
    uint8_t flags;                 // Status flags
}; // Total: 25 bytes

struct RCMessage_t {
    uint8_t type;                  // Message type
    uint8_t from_addr[6];          // Sender MAC address
    uint8_t payload[25];           // Data payload
}; // Total: 32 bytes
```

## Configuration

### User Configuration (`include/esp32_rc_user_config.h`)

Common settings you may want to modify:

```cpp
// Protocol Selection
#define ESP32_RC_PROTOCOL RC_PROTO_ESPNOW  // or RC_PROTO_NRF24, RC_PROTO_WIFI

// ESP-NOW Settings
#define ESPNOW_CHANNEL        2            // Radio Channel (1-13)
#define ESPNOW_OUTPUT_POWER   82           // TX Power (~20.5dBm)

// NRF24L01+ Settings  
#define NRF24_CHANNEL         76           // RF Channel (clear frequency)
#define NRF24_DATA_RATE       RF24_1MBPS   // Speed vs Range balance
#define NRF24_PA_LEVEL        RF24_PA_HIGH // TX Power level

// WiFi Settings
#define RC_WIFI_SSID          "ESP32_RC_Network"
#define RC_WIFI_PASSWORD      "esp32remote"
#define RC_WIFI_MODE          0            // 0=Auto, 1=Station, 2=AP
#define RC_WIFI_PROTOCOL      1            // 0=TCP, 1=UDP
#define RC_WIFI_PORT          12345

// Pin Assignments (NRF24L01+)
#define PIN_NRF_CE            17
#define PIN_NRF_CSN           5
#define PIN_NRF_SCK           18
#define PIN_NRF_MISO          19
#define PIN_NRF_MOSI          23
```

### Custom Payload Override

For protocols that support larger packets (like ESP-NOW up to ~250 bytes), you can override the default 25-byte payload:

```cpp
// In esp32_rc_user_config.h (before any includes)
#ifndef RC_PAYLOAD_T_DEFINED
#define RC_PAYLOAD_T_DEFINED

struct RCPayload_t {
  uint8_t command_type;
  float sensor_data[50];    // 200 bytes for ESP-NOW
  uint8_t status_flags[4];
  char debug_string[40];
  // Total: ~245 bytes (within ESP-NOW limit)
};

#undef RC_PAYLOAD_MAX_SIZE
#define RC_PAYLOAD_MAX_SIZE sizeof(RCPayload_t)
#endif
```

## Examples

The library includes several complete examples demonstrating different use cases:

### 1. Basic Protocol Examples
- **`basic_espnow.cpp`** - Simple ESP-NOW communication
- **`basic_nrf24.cpp`** - Simple NRF24L01+ communication  
- **`basic_wifi.cpp`** - Simple WiFi communication
- **`remote_blinkled.cpp`** - LED control demonstration

### 2. Keyboard Remote Control System
Complete keyboard-controlled robot system with bidirectional communication:

```bash
# Run keyboard controller (connects to ESP32 bridge)
python examples/keyboard_remote_control/keyboard_controller.py --port COM3

# Test automated command sequences
python examples/keyboard_remote_control/test_demo.py --port COM3 --protocol espnow
```

**Controls:**
- **Arrow Keys**: Movement control (Forward/Backward/Left/Right)
- **Space Bar**: Stop/Stand still
- **ESC**: Exit program

### 3. Serial-ESPNOW Bridge
Transparent bridge for raw data passthrough:

```bash
# Interactive bridge with dual windows
python examples/serial_espnow_bridge/keyboard_serial.py
```

**Features:**
- Text-based communication with unique flags (`RC_DATA:`, `RC_SENT:`)
- Filters debug output from mixed serial streams
- Dual-window interface (input + output)
- Real-time bidirectional communication


## Performance & Monitoring

### Real-time Metrics
Enable metrics to monitor performance:

```cpp
controller->enableMetricsDisplay(true, 1000);  // Display every 1000ms
ESP32RemoteControl::enableGlobalMetrics(true); // Enable metrics calculation globally
```

Metrics include:
- **Success Rate**: Percentage of successful transmissions
- **Throughput**: Transactions per second (TPS) with sliding window calculation
- **Connection Status**: Real-time connection state (CONN/DISC/CONN?/ERR)
- **Error Tracking**: Failed transmission counts
- **Protocol Info**: Current protocol and time since startup

**Sample Output:**
```
Time(s) | Protocol | Conn | Send(OK/Fail/Rate/TPS) | Recv(OK/Fail/Rate/TPS) | Total(Sent/Recv)
     45 |  ESPNOW  | CONN | 42/ 3/ 93%/12.3 | 38/ 0/100%/11.2 |   45/  38
```

**Metrics Control:**
- Heartbeat messages are automatically excluded from send metrics
- Global metrics can be disabled for performance: `ESP32RemoteControl::enableGlobalMetrics(false)`
- Metrics display warning appears when disabled

### Operating Modes

**Reliable Mode (default)**:
```cpp
ESP32RemoteControl* controller = createProtocolInstance(ESP32_RC_PROTOCOL, false);  // fast_mode = false
```
- Queued message transmission (up to 10 messages)
- Guaranteed delivery attempts
- Higher latency (~10-50ms)

**Fast Mode**:
```cpp
ESP32RemoteControl* controller = createProtocolInstance(ESP32_RC_PROTOCOL, true);   // fast_mode = true
```
- Single message queue (overwrites previous)
- Minimal latency (~1-5ms)
- No delivery guarantees


## Dependencies

- **PlatformIO ESP32 Framework** (Arduino)
- **esp32-common**: Custom utility library (https://github.com/gr82morozr/esp32-common.git)
- **RF24**: nRF24L01+ library (version ^1.4.11) - only for NRF24 protocol

## Contributing

This library follows a modular architecture. To add new protocols:

1. Inherit from `ESP32RemoteControl` base class
2. Implement pure virtual methods: `getProtocol()`, `lowLevelSend()`, `parseRawData()`  
3. Override `connect()` for protocol-specific initialization
4. Use `onDataReceived()` for incoming messages and `onPeerDiscovered()` for peer detection

## License

This project is open source. See license file for details.

---

**Ready Protocols**: ESP-NOW ✅ | NRF24L01+ ✅ | WiFi Raw 🔄 | Bluetooth LE ❌