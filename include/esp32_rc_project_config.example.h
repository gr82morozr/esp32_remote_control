#pragma once

/*
 * Copy the settings you need into your app's own
 * `include/esp32_rc_project_config.h`.
 *
 * Example consumer `platformio.ini`:
 *   lib_deps =
 *     https://github.com/gr82morozr/esp32_remote_control.git
 */

// Optional protocol default for the consuming app.
// #define ESP32_RC_PROTOCOL RC_PROTO_NRF24

// NRF24 pin overrides.
// #define NRF_SPI_BUS  HSPI
// #define PIN_NRF_CE   4
// #define PIN_NRF_CSN  5
// #define PIN_NRF_SCK  18
// #define PIN_NRF_MISO 19
// #define PIN_NRF_MOSI 23

// Radio defaults.
// #define NRF24_CHANNEL   76
// #define ESPNOW_CHANNEL  6

// WiFi defaults.
// #define RC_WIFI_SSID      "your-ssid"
// #define RC_WIFI_PASSWORD  "your-password"
// #define RC_WIFI_PORT      12345
