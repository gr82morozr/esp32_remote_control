# ESP32 Remote Control Library

Symmetric peer-to-peer communication for ESP32 devices with automatic peer
discovery, heartbeat-based connection management, and a unified API across
multiple radios. Every node runs the same firmware and negotiates roles at
runtime, so there is no master/slave split to maintain.

## Highlights

- Single 32-byte message with a shared 25-byte payload structure for every
  protocol
- Automatic discovery, handshake, and connection monitoring with 100 ms
  heartbeats and 300 ms timeouts
- Switch transports at compile time through one macro while keeping the same API
- Fast mode (single-slot queue) or reliable mode (10-slot queue with retries)
- Lightweight performance metrics with sliding-window TPS calculation
- Python utilities for rapid testing (keyboard controller, serial bridge UI)

## Supported Protocols

| Protocol        | Status       | Notes                                                                           |
|-----------------|--------------|---------------------------------------------------------------------------------|
| ESP-NOW         | Stable       | Direct ESP32-to-ESP32 link with automatic peer addition and ack/retry handling |
| nRF24L01+       | Stable       | 5-byte address handshake, pipe switching, compile-time pin/channel selection    |
| WiFi Raw 802.11 | Experimental | Raw frame discovery plus UDP data channel, AP/STA negotiation at runtime        |
| Bluetooth LE    | Planned      | Interface reserved for a future implementation                                  |

## Architecture at a Glance

- **Discovery layer**: broadcast ESP-NOW beacons, NRF24 handshake frames, or raw
  802.11 vendor frames to locate peers.
- **Connection layer**: role negotiation, heartbeat timers, and reconnection on
  silence; exposes state via `getConnectionState()`.
- **Transport layer**: fixed `RCMessage_t` container with helper accessors and
  protocol-specific `lowLevelSend()` implementations.

```cpp
struct RCPayload_t {
  uint8_t id1, id2, id3, id4;
  float value1, value2, value3, value4, value5;
  uint8_t flags;
};

struct RCMessage_t {
  uint8_t type;                  // RCMSG_TYPE_DATA or RCMSG_TYPE_HEARTBEAT
  uint8_t from_addr[RC_ADDR_SIZE];
  uint8_t payload[RC_PAYLOAD_MAX_SIZE];  // reinterpret_cast to RCPayload_t
};
```

## Getting Started

### Requirements

- PlatformIO 6.x or Arduino IDE with the ESP32 core
- `esp32-common` helper library (installed automatically when listed in
  `lib_deps`)
- `nrf24/RF24` when compiling the nRF24L01+ protocol

Add the library to your `platformio.ini`:

```ini
[env:esp32]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps =
  https://github.com/gr82morozr/esp32-common.git
  nrf24/RF24 @ ^1.4.11    ; required only for NRF24 builds
```

### Select a protocol

Choose the protocol **before** including `esp32_rc_factory.h`. Only the selected
transport is compiled, keeping the firmware focused.

```cpp
#define ESP32_RC_PROTOCOL RC_PROTO_ESPNOW   // or RC_PROTO_NRF24 / RC_PROTO_WIFI
#include "esp32_rc_factory.h"
```

### Minimal sketch

```cpp
#include <Arduino.h>

#define ESP32_RC_PROTOCOL RC_PROTO_ESPNOW
#include "esp32_rc_factory.h"

ESP32RemoteControl* controller = nullptr;

void onMessage(const RCMessage_t& msg) {
  const auto* payload = msg.getPayload();
  Serial.printf("Peer flags: %u value1: %.2f\n", payload->flags, payload->value1);
}

void setup() {
  Serial.begin(115200);
  controller = createProtocolInstance(ESP32_RC_PROTOCOL, false); // false = reliable mode
  if (!controller) {
    Serial.println("Protocol not enabled in esp32_rc_user_config.h");
    return;
  }
  controller->setOnRecieveMsgHandler(onMessage);
  controller->connect();
}

void loop() {
  if (!controller) {
    delay(1000);
    return;
  }

  RCPayload_t payload = {};
  payload.id1 = 0xAA;
  payload.value1 = millis() / 1000.0f;
  controller->sendData(payload);
  delay(500);
}
```

## Configuration

All user-tunable settings live in `include/esp32_rc_user_config.h`:

- Select the active protocol with `ESP32_RC_PROTOCOL`
- Configure nRF24L01+ pins, SPI bus, RF channel, data rate, and power level
- Adjust WiFi discovery timeouts and UDP port
- Set global logging level (`CURRENT_LOG_LEVEL`)
- Override queue depths, retry counts, or heartbeat timings if needed

### Custom payloads

The default payload is 25 bytes. If you need a different structure, redefine it
*before* including any library headers:

```cpp
#ifndef RC_PAYLOAD_T_DEFINED
#define RC_PAYLOAD_T_DEFINED
struct RCPayload_t {
  uint8_t command;
  float values[4];
  uint16_t crc;
};
#undef  RC_PAYLOAD_MAX_SIZE
#define RC_PAYLOAD_MAX_SIZE sizeof(RCPayload_t)
#endif
```

Make sure the new size fits within the limits of your chosen transport
(ESP-NOW supports up to 250 bytes, NRF24 up to 32 bytes).

### Fast versus reliable mode

- `fast_mode = false` (default): background queue of 10 messages with retries.
- `fast_mode = true`: single-slot queue for minimum latency; new data replaces
  pending packets.

## Metrics and Diagnostics

- `ESP32RemoteControl::enableGlobalMetrics(bool)` toggles statistical tracking.
- `controller->enableMetricsDisplay(true, 1000)` prints a summary every second.
- Heartbeat packets are excluded from send metrics automatically.
- `setOnDiscoveryHandler()` delivers discovery events for dashboards or logs.

Typical metrics output:

```
Time(s) | Protocol | Conn | Send(OK/Fail/Rate/TPS) | Recv(OK/Fail/Rate/TPS) | Total(Sent/Recv)
     45 |  ESPNOW  | CONN | 42/ 3/ 93%/12.3        | 38/ 0/100%/11.2        |   45/  38
```

## Examples

- `examples/basic_espnow.cpp` – introductory ESP-NOW sender/receiver pair.
- `examples/basic_nrf24.cpp` – nRF24L01+ handshake, queueing, and data flow.
- `examples/basic_wifi.cpp` – raw 802.11 discovery with UDP transport.
- `examples/callback/basic_espnow_callback.cpp` – receive callbacks instead of polling.
- `examples/remote_blinkled.cpp` – payload-driven LED control sketch.
- `examples/serial_espnow_bridge/serial_espnow_bridge.cpp` – USB-to-ESP-NOW bridge firmware.
- `examples/keyboard_remote_control/keyboard_receiver.cpp` – robot-style command consumer.

Each sketch is single-source and can be uploaded directly with PlatformIO
(`pio run -e <env> -t upload`).

## PC Utilities

- `examples/serial_espnow_bridge/keyboard_serial.py` – terminal client for the
  serial bridge that tags outgoing and incoming packets.
- `examples/serial_espnow_bridge/ui_serial.py` – PyQt5/PySide6 desktop UI with a
  light theme, serial port picker, logging panes, and packet editor.
- `examples/keyboard_remote_control/keyboard_controller.py` – keyboard-driven
  command generator (ESP-NOW or nRF24) with serial bridge integration.

Install prerequisites with `pip install pyserial` and, for the UI, `pip install pyqt5`
or `pip install pyside6`.

## Troubleshooting Tips

- Increase logging by setting `CURRENT_LOG_LEVEL` to `4` (DEBUG) in
  `esp32_rc_user_config.h`.
- Verify heartbeats by watching the metrics banner; a missing heartbeat forces
  reconnection.
- For nRF24 links, double-check CE/CSN wiring and confirm both nodes share the
  same RF channel.
- WiFi mode is experimental; keep both devices near each other on channel 6 and
  allow up to 15 seconds for the full raw-frame discovery and UDP handshake.

## Contributing

Protocol implementations inherit from `ESP32RemoteControl` and implement:

- `getProtocol()`
- `lowLevelSend(const RCMessage_t&)`
- `parseRawData(const uint8_t*, size_t)`

Optional overrides handle discovery broadcasts, address formats, and custom
connection logic. Pull requests with additional protocols, tooling, or examples
are welcome.

## License

The project is released under the MIT License as declared in `library.json`.
