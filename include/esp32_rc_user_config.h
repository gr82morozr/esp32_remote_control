#pragma once
/*
  This file is for user-specific configurations for the ESP32 Remote Control project.
  It allows users to override default hardware pins and settings for various protocols.
*/


#define CURRENT_LOG_LEVEL 3

// =======================================================
// Common Hardware Pins (used on both sides)
// =======================================================
/*
--- NRF24L01 (SPI) Pins (user-overridable) ---
Sample: 
-----------
#define NRF_SPI_BUS           HSPI   // HSPI or VSPI (default)
#define PIN_NRF_CE            17
#define PIN_NRF_CSN           5
#define PIN_NRF_SCK           18
#define PIN_NRF_MISO          19
#define PIN_NRF_MOSI          23
*/
#define NRF_SPI_BUS           HSPI

#define PIN_NRF_CE            17
#define PIN_NRF_CSN           5
#define PIN_NRF_SCK           18
#define PIN_NRF_MISO          19
#define PIN_NRF_MOSI          23

#define NRF24_CHANNEL         76



// --- ESP-NOW specific definitions ---
#define ESPNOW_CHANNEL        2
#define ESPNOW_OUTPUT_POWER   82


// --- WiFi pecific definitions ---
#define RC_WIFI_PASSWORD      "rcpassword"
#define RC_WIFI_PORT          12345
