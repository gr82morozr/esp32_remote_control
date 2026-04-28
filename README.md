# ESP32 Remote Control Library

Symmetric peer-to-peer communication for ESP32 devices with automatic peer
discovery, heartbeat-based connection management, and a unified API across
multiple radios. Every node uses the same transport lifecycle and negotiates
peers at runtime, so there is no master/slave split to maintain.

## Highlights

- Single 32-byte message with a shared 25-byte payload structure for every
  protocol
- Automatic discovery, handshake, and connection monitoring with 100 ms
  heartbeats and 1000 ms timeouts
- Switch transports at compile time through one macro while keeping the same API
- Fast mode, using a single-slot queue, or reliable mode, using 10-slot queues
  with retries
- Lightweight performance metrics with sliding-window TPS calculation
- Python utilities for rapid testing, including keyboard control and serial UI
- ESP32-only target; ESP8266 is not supported by this library

## Supported Protocols

| Protocol | Status | Notes |
|----------|--------|-------|
| ESP-NOW | Stable | Direct ESP32-to-ESP32 link with automatic peer addition and ack/retry handling |
| nRF24L01+ | Stable | 5-byte address handshake, pipe switching, compile-time pin/channel selection |
| WiFi Raw 802.11 | Experimental | Raw frame discovery and AP/STA negotiation work in progress |
| Bluetooth LE | Planned | Interface reserved for a future implementation |

## Architecture

- **Discovery layer**: broadcast ESP-NOW beacons, NRF24 handshake frames, or raw
  802.11 vendor frames to locate peers.
- **Connection layer**: role negotiation, heartbeat timers, and reconnection on
  silence; exposes state through `getConnectionState()`.
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
- `esp32-common` helper library, installed automatically when listed in
  `lib_deps`
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

### Select A Protocol

Choose the protocol before including `esp32_rc_factory.h`. Only the selected
transport is compiled, keeping the firmware focused.

```cpp
#define ESP32_RC_PROTOCOL RC_PROTO_ESPNOW   // or RC_PROTO_NRF24 / RC_PROTO_WIFI
#include "esp32_rc_factory.h"
```

### Minimal Sketch

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
  controller->setOnReceiveMsgHandler(onMessage);
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

### Symmetric ESP-NOW Usage

ESP-NOW is invoked the same way on both peers:

1. Create the controller with `createProtocolInstance(RC_PROTO_ESPNOW, ...)`.
2. Register either a receive callback with `setOnReceiveMsgHandler()` or poll
   with `recvData()`.
3. Call `connect()` on every node.
4. Call `sendData()` from any node when it has payload to publish.

The library does not require a dedicated initiator or listener. If one sketch
behaves like a bridge and another behaves like a sensor, that difference is only
in the application logic built on top of the same `connect()`, `sendData()`,
and receive path.

### ESP-NOW Channel Negotiation

The current ESP-NOW transport does not use a fixed compile-time channel only.
It performs a small `HELLO`-based negotiation implemented in
`src/esp32_rc_espnow.cpp` and `include/esp32_rc_espnow.h`.

Build-time entry points:

- `ESP32_RC_ESPNOW::connect()` resets negotiation state and calls
  `determineInitialChannelState()`.
- `ESP32_RC_ESPNOW::init()` registers the ESP-NOW callbacks and also calls
  `determineInitialChannelState()` during transport startup.

Key state fields in `ESP32_RC_ESPNOW`:

- `preferred_channel_`: preferred starting channel when Wi-Fi is not locking the radio
- `current_channel_`: channel currently in use
- `negotiated_channel_`: last agreed peer channel
- `channel_locked_`: true when STA Wi-Fi is connected and the AP channel must be used
- `pending_negotiation_channel_` and `pending_peer_mac_`: deferred negotiation target
- `awaiting_link_confirmation_`: true after channel agreement, before the first real unicast packet confirms the link

Negotiation packet:

- `RCMSG_TYPE_HELLO` is built by `ESP32_RC_ESPNOW::makeHelloMessage()`
- The payload type is `HelloPayload`
- It carries:
  - `version`
  - `current_channel`
  - `flags`
  - `priority`
  - `device_id`
- `flags & kHelloFlagChannelLocked` indicates that the sender is locked to an active Wi-Fi AP channel

Runtime flow:

1. `ESP32_RC_ESPNOW::connect()` calls `determineInitialChannelState()`.
If `WiFi.status() == WL_CONNECTED`, the node becomes AP-channel-locked and uses
the live Wi-Fi channel. Otherwise it starts from `preferred_channel_` and can
hop discovery channels.

2. While disconnected, `ESP32_RC_ESPNOW::sendSysMsg()` sends `RCMSG_TYPE_HELLO`
instead of normal heartbeats.
`ESP32_RC_ESPNOW::lowLevelSend()` broadcasts that `HELLO` and
`ESP32_RC_ESPNOW::advanceDiscoveryChannel()` hops to the next discovery channel
after each discovery send when the node is not Wi-Fi locked.

3. Incoming ESP-NOW frames arrive in `ESP32_RC_ESPNOW::onDataRecvStatic()`.
If the frame type is `RCMSG_TYPE_HELLO`, it is routed to
`ESP32_RC_ESPNOW::handleHelloMessage()`.

4. `ESP32_RC_ESPNOW::handleHelloMessage()` parses `HelloPayload` and calls
`ESP32_RC_ESPNOW::chooseNegotiatedChannel()`.
That function applies the current rules:
  - if both nodes are Wi-Fi locked to the same channel, use that channel
  - if one node is Wi-Fi locked, use the locked channel
  - if both nodes are Wi-Fi locked to different channels, pairing is impossible
  - if neither node is locked, choose deterministically from the two candidate channels

5. HELLO reception does not switch channels immediately inside the receive
callback.
Instead, `handleHelloMessage()` stores `pending_peer_mac_` and
`pending_negotiation_channel_`, and
`ESP32_RC_ESPNOW::processPendingNegotiation()` performs the real work later
from `ESP32_RC_ESPNOW::lowLevelSend()`.
This keeps channel switching and peer registration out of the ESP-NOW receive callback.

6. `ESP32_RC_ESPNOW::completeNegotiationWithPeer()` finalizes the agreement:
  - `applyChannel(agreed_channel)`
  - `ensureBroadcastPeerRegistered()`
  - `ensurePeerRegistered(peer_mac)`
  - `setPeerAddr(peer_mac)`
  - `conn_state_ = CONNECTING`
  - `awaiting_link_confirmation_ = true`

7. The first real unicast packet from the negotiated peer clears
`awaiting_link_confirmation_` inside `ESP32_RC_ESPNOW::onDataRecvStatic()`.
At that point the normal base-class receive path marks the peer as connected.

8. Recovery is handled by `ESP32_RC_ESPNOW::checkHeartbeat()`.
If a confirmed peer stops talking for `HEARTBEAT_TIMEOUT_MS`, or if link
confirmation never arrives after negotiation, the transport clears the peer and
returns to discovery automatically.

Important implementation notes:

- Negotiation is transport-internal; application code still uses the same
  `connect()`, `sendData()`, `recvData()`, and `setOnReceiveMsgHandler()` API.
- `esp_now_peer_info_t.channel` is registered as `0`, so unicast peers follow
  the current Wi-Fi channel after negotiation.
- The impossible case where both nodes are actively Wi-Fi locked to different
  AP channels currently transitions the ESP-NOW transport to `ERROR`.

## Configuration

All user-tunable settings live in `include/esp32_rc_user_config.h`:

- Select the active protocol with `ESP32_RC_PROTOCOL`
- Configure nRF24L01+ pins, SPI bus, RF channel, data rate, and power level
- Adjust WiFi discovery timeouts and UDP port
- Set global logging level with `CURRENT_LOG_LEVEL`
- Override queue depths, retry counts, or heartbeat timings if needed

### Custom Payloads

The default payload is 25 bytes. If you need a different structure, redefine it
before including any library headers:

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

Make sure the new size fits within the limits of your chosen transport.
ESP-NOW supports larger packets, but NRF24 payloads are limited to 32 bytes.

### Fast Versus Reliable Mode

- `fast_mode = false`: reliable mode with a background queue of 10 messages and
  retries.
- `fast_mode = true`: low-latency mode with a single-slot queue; new data
  replaces pending packets.

## Metrics And Diagnostics

- `ESP32RemoteControl::enableGlobalMetrics(bool)` toggles statistical tracking.
- `controller->enableMetricsDisplay(true, 1000)` prints a summary every second.
- Heartbeat packets are excluded from send metrics automatically.
- `setOnReceiveMsgHandler()` registers a receive callback. The older
  `setOnRecieveMsgHandler()` spelling is kept as a compatibility wrapper.
- `setOnDiscoveryHandler()` delivers discovery events for dashboards or logs.

Typical metrics output:

```text
Time(s) | Protocol | Conn | Send(OK/Fail/Rate/TPS) | Recv(OK/Fail/Rate/TPS) | Total(Sent/Recv)
     45 |  ESPNOW  | CONN | 42/ 3/ 93%/12.3        | 38/ 0/100%/11.2        |   45/  38
```

## Examples

- `examples/basic/basic_espnow.cpp` - symmetric ESP-NOW example; flash the same sketch to both nodes
- `examples/basic/basic_nrf24.cpp` - nRF24L01+ handshake, queueing, and data flow
- `examples/basic/basic_wifi.cpp` - raw 802.11 discovery experiment
- `examples/callback/basic_espnow_callback.cpp` - receive callbacks instead of polling
- `examples/basic/remote_blinkled.cpp` - payload-driven LED control sketch
- `examples/serial_espnow_bridge/serial_espnow_bridge.cpp` - USB-to-ESP-NOW CSV bridge built on the same symmetric API
- `examples/serial_espnow_bridge/dummy_sensor_collector.cpp` - peer sketch that uses the same ESP-NOW lifecycle with different application behavior
- `examples/keyboard_remote_control/keyboard_receiver.cpp` - robot-style command consumer

Each sketch is single-source and can be uploaded directly with PlatformIO:

```bash
pio run -e <env> -t upload
```

## Serial CSV Bridge

The serial bridge accepts one newline-terminated CSV packet per message:

```text
id1,id2,id3,id4,value1,value2,value3,value4,value5,flags
```

Exactly 10 fields are required. ID and flag fields must be integer values from
0 to 255, and value fields must be valid floats. Invalid input returns:

```text
RC_ERROR:bad_csv
```

Incoming ESP-NOW packets are printed as:

```text
id1=<v>,id2=<v>,id3=<v>,id4=<v>,value1=<v>,value2=<v>,value3=<v>,value4=<v>,value5=<v>,flags=<v>
```

Successfully submitted serial packets are echoed as:

```text
RC_SENT:id1=<v>,id2=<v>,id3=<v>,id4=<v>,value1=<v>,value2=<v>,value3=<v>,value4=<v>,value5=<v>,flags=<v>
```

The bridge example is intentionally application-asymmetric, but its ESP-NOW
usage is still symmetric with the peer sketch: both sides create the same
controller type, call `connect()`, register receive handling, and use
`sendData()` for outbound payloads.

Bridge output mode is compile-time selectable in
[src/mcu_a_main.cpp](/d:/projects.git/esp32_remote_control/src/mcu_a_main.cpp:17):

- `BRIDGE_OUTPUT_MODE_CSV_VERBOSE`: `id1=2,id2=180,...`
- `BRIDGE_OUTPUT_MODE_CSV_COMPACT`: `2,180,0,0,232.24,...`
- `BRIDGE_OUTPUT_MODE_BINARY`: framed binary output

Exactly one of those macros must be enabled.

Binary mode frame layout:

```text
0xAA 0x55 <type> <25-byte RCPayload_t> <checksum>
```

- `type = 0x01`: payload received from ESP-NOW and forwarded to the PC
- `type = 0x02`: payload accepted from the PC side and echoed as sent
- `checksum`: XOR of `type` and all 25 payload bytes

The included Python bridge tools assume text output by default, so binary mode
is intended for custom PC-side parsers.

If the bridge is receiving telemetry packets with a sequence counter in `id2`,
you can enable or disable gap reporting at compile time with
`BRIDGE_ENABLE_DROP_DETECT` in [src/mcu_a_main.cpp](/d:/projects.git/esp32_remote_control/src/mcu_a_main.cpp:12).

## PC Utilities

- `examples/serial_espnow_bridge/keyboard_serial.py` - terminal client for the
  CSV serial bridge that tags outgoing and incoming packets
- `examples/serial_espnow_bridge/ui_serial.py` - PyQt5/PySide6 desktop UI with a
  light theme, serial port picker, logging panes, and packet editor
- `examples/keyboard_remote_control/keyboard_controller.py` - keyboard-driven
  command generator with serial bridge integration

Install prerequisites with `pip install pyserial`. For the UI, install either
`pyqt5` or `pyside6`.

## Troubleshooting

- Increase logging by setting `CURRENT_LOG_LEVEL` to `4` in
  `esp32_rc_user_config.h`.
- Verify heartbeats by watching the metrics banner. A missing heartbeat forces
  reconnection.
- For nRF24 links, double-check CE/CSN wiring and confirm both nodes share the
  same RF channel.
- WiFi mode is experimental. Keep both devices near each other on the rendezvous
  channel and expect the implementation to change.

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
