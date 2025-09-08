#pragma once
/*
 * ESP32 Remote Control - User Configuration
 * 
 * This file contains all user-configurable settings for the ESP32 Remote Control library.
 * Users should modify these settings to match their hardware setup and preferences.
 * 
 * For internal framework settings (timeouts, buffer sizes, etc.), see esp32_rc_common.h
 */

// =======================================================
// UNIFIED PROTOCOL SELECTION
// =======================================================

// Protocol constants (must be defined before use)
#define RC_PROTO_ESPNOW   0
#define RC_PROTO_WIFI     1
#define RC_PROTO_BLE      2
#define RC_PROTO_NRF24    3

/**
 * Single macro to control which protocol to use
 * Only the selected protocol will be compiled, reducing code size and memory usage
 * 
 * Available protocols:
 * - RC_PROTO_ESPNOW  : ESP-NOW protocol
 * - RC_PROTO_NRF24   : NRF24L01+ protocol  
 * - RC_PROTO_WIFI    : WiFi Raw 802.11 protocol
 * - RC_PROTO_BLE     : BLE protocol (future)
 * 
 * To use specific protocol in your code:
 * 1. Define BEFORE including any headers:
 *    #define ESP32_RC_PROTOCOL RC_PROTO_WIFI
 *    #include "esp32_rc_factory.h"
 * 
 * 2. Or change the default below:
 *    #define ESP32_RC_PROTOCOL RC_PROTO_NRF24
 */
#ifndef ESP32_RC_PROTOCOL
#define ESP32_RC_PROTOCOL RC_PROTO_ESPNOW    // Default: ESP-NOW protocol
#endif

// Automatic enable/disable based on selected protocol
#if ESP32_RC_PROTOCOL == RC_PROTO_ESPNOW
    #define ENABLE_ESP32_RC_ESPNOW
#elif ESP32_RC_PROTOCOL == RC_PROTO_NRF24
    #define ENABLE_ESP32_RC_NRF24
#elif ESP32_RC_PROTOCOL == RC_PROTO_WIFI
    #define ENABLE_ESP32_RC_WIFI
#elif ESP32_RC_PROTOCOL == RC_PROTO_BLE
    #define ENABLE_ESP32_RC_BLE
#endif

// =======================================================
// LOGGING CONFIGURATION
// =======================================================
/**
 * Log Level: 0=OFF, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG, 5=VERBOSE
 * Can be overridden by defining CURRENT_LOG_LEVEL before including headers
 */
#ifndef CURRENT_LOG_LEVEL
#define CURRENT_LOG_LEVEL 3
#endif

// =======================================================
// PHYSICAL PIN ASSIGNMENTS
// =======================================================

// --- NRF24L01+ SPI Pins ---
/**
 * SPI Bus Selection: HSPI or VSPI
 * HSPI uses different internal routing but same GPIO pins on ESP32
 */
#ifndef NRF_SPI_BUS
#define NRF_SPI_BUS           VSPI
#endif

/**
 * NRF24L01+ Pin Assignments
 * These are the most common pin assignments for ESP32 development boards
 */
#ifndef PIN_NRF_CE
#define PIN_NRF_CE            17    // Chip Enable
#endif
#ifndef PIN_NRF_CSN  
#define PIN_NRF_CSN           5     // Chip Select (SPI CS)
#endif
#ifndef PIN_NRF_SCK
#define PIN_NRF_SCK           18    // SPI Clock
#endif
#ifndef PIN_NRF_MISO
#define PIN_NRF_MISO          19    // SPI Master In Slave Out
#endif
#ifndef PIN_NRF_MOSI
#define PIN_NRF_MOSI          23    // SPI Master Out Slave In
#endif

// --- Future Protocol Pins (for expansion) ---
// Uncomment and define as needed for additional protocols

// I2C Pins (for future I2C-based protocols)
// #define PIN_I2C_SDA           21
// #define PIN_I2C_SCL           22

// UART Pins (for future serial protocols)
// #define PIN_UART_TX           1
// #define PIN_UART_RX           3

// =======================================================
// COMMUNICATION CHANNELS & FREQUENCIES  
// =======================================================

// --- NRF24L01+ Radio Settings ---
/**
 * NRF24 RF Channel (0-125)
 * Each channel is 1MHz wide. Avoid WiFi channels:
 * WiFi Ch 1: ~2412MHz = NRF24 Ch 12
 * WiFi Ch 6: ~2437MHz = NRF24 Ch 37  
 * WiFi Ch 11: ~2462MHz = NRF24 Ch 62
 * Channel 76 = 2476MHz (clear of most WiFi)
 */
#ifndef NRF24_CHANNEL
#define NRF24_CHANNEL         76
#endif

/**
 * NRF24 Data Rate Options:
 * RF24_250KBPS - Longest range, most reliable
 * RF24_1MBPS   - Good balance of speed and range  
 * RF24_2MBPS   - Fastest, shortest range
 */
#ifndef NRF24_DATA_RATE
#define NRF24_DATA_RATE       RF24_1MBPS
#endif

/**
 * NRF24 Power Amplifier Level:
 * RF24_PA_MIN  - Minimum power, longest battery life
 * RF24_PA_LOW  - Low power, good for close range
 * RF24_PA_HIGH - High power, better range
 * RF24_PA_MAX  - Maximum power, best range
 */
#ifndef NRF24_PA_LEVEL
#define NRF24_PA_LEVEL        RF24_PA_HIGH
#endif

/**
 * NRF24 Retry Settings
 * RETRY_COUNT: Number of automatic retries (0-15)
 * RETRY_DELAY: Delay between retries in 250μs steps (0-15, so 0-3750μs)
 */
#ifndef NRF24_RETRY_COUNT
#define NRF24_RETRY_COUNT     5
#endif
#ifndef NRF24_RETRY_DELAY  
#define NRF24_RETRY_DELAY     5
#endif

// --- ESP-NOW Settings ---
/**
 * ESP-NOW WiFi Channel (1-13)
 * Must match the WiFi channel if WiFi is also used
 */
#ifndef ESPNOW_CHANNEL
#define ESPNOW_CHANNEL        2
#endif

/**
 * ESP-NOW TX Power (8-84 in 0.25dBm steps)
 * 84 = 21dBm = ~125mW (maximum)
 * 60 = 15dBm = ~32mW (good balance) 
 * 32 = 8dBm = ~6mW (low power)
 */
#ifndef ESPNOW_OUTPUT_POWER
#define ESPNOW_OUTPUT_POWER   82    // ~20.5dBm
#endif

// =======================================================
// WIFI NETWORK SETTINGS
// =======================================================

/**
 * WiFi Operation Mode:
 * 0 = Auto-negotiate (scan for peers, become AP if none found)
 * 1 = Force Station Mode (connect to existing WLAN)  
 * 2 = Force Access Point Mode (create own network)
 */
#ifndef RC_WIFI_MODE
    #ifdef DEVICE_MODE_AP
        #define RC_WIFI_MODE          2    // Access Point mode (create own network)
    #elif defined(DEVICE_MODE_STATION)
        #define RC_WIFI_MODE          1    // Station mode (connect to AP)
    #else
        #define RC_WIFI_MODE          0    // Auto-negotiate mode (RECOMMENDED)
    #endif
#endif

/**
 * WiFi Network Credentials
 * For Station mode: credentials to connect to existing AP
 * For AP mode: credentials for the network this device creates
 * Note: Both devices must use the same SSID/password for peer-to-peer communication
 */
#ifndef RC_WIFI_SSID
#define RC_WIFI_SSID          "ESP32_RC_Network"
#endif

#ifndef RC_WIFI_PASSWORD  
#define RC_WIFI_PASSWORD      "esp32remote"
#endif

/**
 * WiFi Communication Protocol:
 * 0 = TCP (reliable, connection-oriented, requires client/server roles)
 * 1 = UDP (fast, connectionless, symmetric peer-to-peer)
 * 
 * UDP is recommended for peer-to-peer communication because:
 * - No client/server role assignment needed
 * - Both devices can send/receive equally 
 * - Simpler discovery and connection process
 * - Lower latency for real-time applications
 */
#ifndef RC_WIFI_PROTOCOL
#define RC_WIFI_PROTOCOL      1    // UDP mode (better for peer-to-peer)
#endif

/**
 * WiFi Communication Port
 * TCP/UDP port for communication
 */
#ifndef RC_WIFI_PORT
#define RC_WIFI_PORT          12345
#endif

/**
 * WiFi Connection Timeout (ms)
 * How long to wait for WiFi connection before giving up
 */
#ifndef RC_WIFI_TIMEOUT_MS
#define RC_WIFI_TIMEOUT_MS    10000
#endif

/**
 * WiFi Discovery Settings
 * IP_DISCOVERY_INTERVAL_MS: How often to broadcast IP (ms)
 * IP_DISCOVERY_PORT: UDP port for discovery broadcasts
 */
#ifndef RC_WIFI_IP_DISCOVERY_INTERVAL_MS
#define RC_WIFI_IP_DISCOVERY_INTERVAL_MS    2000
#endif

#ifndef RC_WIFI_IP_DISCOVERY_PORT
#define RC_WIFI_IP_DISCOVERY_PORT           12346
#endif

// =======================================================
// BLUETOOTH LOW ENERGY SETTINGS (Future Expansion)
// =======================================================

// BLE Device Name
// #ifndef RC_BLE_DEVICE_NAME
// #define RC_BLE_DEVICE_NAME    "ESP32_RC_Device"
// #endif

// BLE Service UUID (128-bit)
// #ifndef RC_BLE_SERVICE_UUID
// #define RC_BLE_SERVICE_UUID   "12345678-1234-1234-1234-123456789ABC"
// #endif