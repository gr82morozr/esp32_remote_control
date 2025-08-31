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
#define NRF_SPI_BUS           HSPI
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
 * WiFi Network Credentials
 * Used for WiFi-based communication protocols
 */
#ifndef RC_WIFI_SSID
#define RC_WIFI_SSID          "ESP32_RC_Network"
#endif

#ifndef RC_WIFI_PASSWORD  
#define RC_WIFI_PASSWORD      "rcpassword"
#endif

/**
 * WiFi Communication Port
 * TCP/UDP port for WiFi-based protocols
 */
#ifndef RC_WIFI_PORT
#define RC_WIFI_PORT          12345
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