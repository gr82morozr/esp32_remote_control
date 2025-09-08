ESP32 Remote Control Library
================================

A generic ESP32 remote control library that provides symmetric communication 
between devices using multiple wireless protocols. The library is designed to 
work identically on both sender and receiver devices, with automatic peer 
discovery and role negotiation.

SUPPORTED PROTOCOLS
===================
- WiFi TCP/UDP (esp32_rc_wifi.h) - Network-based communication
- ESP-NOW (esp32_rc_espnow.h) - Direct peer-to-peer communication  
- nRF24L01+ (esp32_rc_nrf24.h) - 2.4GHz radio communication
- Future: BLE, LoRa, other protocols

KEY FEATURES
============
✓ Symmetric Design - Same code works on all devices (sender/receiver)
✓ Protocol Abstraction - Switch protocols by changing one line of code
✓ Automatic Discovery - Devices find each other automatically
✓ Built-in Metrics - Track success rates, throughput, connection status
✓ Queue Management - Buffered sending (fast_mode=false) or immediate (fast_mode=true)
✓ Heartbeat System - Automatic connection monitoring and recovery
✓ Simple API - Just sendData() and recvData() for most use cases

BASIC USAGE
===========

1. Choose your protocol:
   #include "esp32_rc_wifi.h"    // For WiFi
   #include "esp32_rc_espnow.h"  // For ESP-NOW  
   #include "esp32_rc_nrf24.h"   // For nRF24L01+

2. Create controller instance:
   ESP32_RC_WIFI* controller = new ESP32_RC_WIFI(fast_mode);
   // fast_mode: false = queued (reliable), true = immediate (fast)

3. Initialize and connect:
   controller->connect();

4. Send/receive data:
   RCPayload_t outgoing = {0};
   outgoing.value1 = 123.45f;
   controller->sendData(outgoing);
   
   RCPayload_t incoming;
   if (controller->recvData(incoming)) {
       // Process received data
   }

DATA STRUCTURE  
==============
RCPayload_t (25 bytes total):
- 4 × uint8_t IDs (id1-id4) for message identification
- 5 × float values (value1-value5) for sensor data, control signals, etc.
- 1 × uint8_t flags for status/control bits

CONFIGURATION
=============
All user settings are in: include/esp32_rc_user_config.h
- Pin assignments (NRF24 SPI pins, etc.)
- Network credentials (WiFi SSID/password)
- Radio settings (channels, power levels)
- Protocol-specific parameters

EXAMPLES
========
See examples/ directory:
- basic_wifi.cpp    - WiFi TCP/UDP communication with discovery
- basic_espnow.cpp  - Direct ESP-NOW peer-to-peer
- basic_nrf24.cpp   - nRF24L01+ radio communication

Each example shows the same symmetric code pattern that works on both devices.

PROTOCOL DETAILS
================

WiFi Mode:
- Automatic peer discovery via UDP broadcast
- UDP peer-to-peer communication (symmetric, no server/client roles)
- Station mode (join existing network) or AP mode (create network)
- Both devices can send and receive data equally

ESP-NOW Mode:
- Direct device-to-device communication (no WiFi network required)
- Automatic peer pairing and management
- Low latency, moderate range

nRF24L01+ Mode:
- 2.4GHz radio with excellent range and reliability
- Configurable power levels and data rates
- Multiple devices on same channel with addressing

SYMMETRIC OPERATION
===================
The library eliminates the traditional master/slave or sender/receiver roles:

1. All devices run identical code
2. Automatic peer discovery finds other devices
3. Built-in conflict resolution (e.g., MAC address comparison)
4. Heartbeat system maintains connections
5. Automatic role switching as needed

This means you can deploy the same firmware to all devices and they will
automatically organize themselves into a working communication network.

ADVANCED FEATURES
=================
- Connection state monitoring (DISCONNECTED/CONNECTING/CONNECTED/ERROR)
- Real-time metrics (success rate, throughput, error counts)
- Configurable timeouts and retry logic  
- Protocol-specific configuration via setProtocolConfig()
- Custom discovery and receive callbacks
- Memory-efficient queuing system
