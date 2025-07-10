#pragma once
#include <Arduino.h>

#define CURRENT_LOG_LEVEL 4

// =======================================================
// Uncomment this line when building for the CONTROLLER UI
// (comment it out for the robot build)
// =======================================================
// #define RC_CONTROLLER_UI 1

// =======================================================
// Protocol Enable Macros
// =======================================================
#ifdef RC_CONTROLLER_UI
    // Controller: include all protocol code for runtime selection
    #define ENABLE_ESPNOW  1
    #define ENABLE_WIFI    1
    #define ENABLE_BLE     1
    #define ENABLE_NRF24   1
#else
    // Robot: only include the protocol(s) you want to support
    #define ENABLE_ESPNOW  1    // Set to 1 to include, 0 to exclude
    #define ENABLE_WIFI    0
    #define ENABLE_BLE     0
    #define ENABLE_NRF24   0

    // Set which protocol the robot actually uses at build time
    #define SELECTED_PROTOCOL RC_PROTO_ESPNOW
#endif


#define ENABLE_ESPNOW  1
// =======================================================
// Common Hardware Pins (used on both sides)
// =======================================================

// NRF24L01 (SPI)
#define PIN_NRF_CE      17
#define PIN_NRF_CSN     5
#define PIN_NRF_SCK     18
#define PIN_NRF_MISO    19
#define PIN_NRF_MOSI    23



// Example: General RC buttons, available on both sides
#define PIN_RC_BUTTON_A 32
#define PIN_RC_BUTTON_B 33

// =======================================================
// LCD & UI Pins (only for CONTROLLER UI builds)
// =======================================================
#ifdef RC_CONTROLLER_UI
    #define PIN_LCD_SCL      22
    #define PIN_LCD_SDA      21
    #define PIN_ENCODER_A    15
    #define PIN_ENCODER_B    4
    #define PIN_MENU_BUTTON  16
    // ...add any more controller-specific hardware here
#endif

// =======================================================
// Switch Pins or protocol selector switches (optional)
// =======================================================
#define PIN_SWITCH1      13
#define PIN_SWITCH2      14

// ...add any more shared pins as needed

