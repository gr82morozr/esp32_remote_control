#pragma once
/*
 * ESP32 Remote Control - Protocol Factory
 * 
 * This file conditionally includes protocol implementations and provides
 * factory functions based on user configuration. Only includes the protocols
 * that are enabled via macros in esp32_rc_user_config.h
 * This reduces compilation time, code size, and memory usage.
 */

#include "esp32_rc.h"

// =======================================================
// CONDITIONAL PROTOCOL INCLUDES
// =======================================================

#ifdef ENABLE_ESP32_RC_ESPNOW
#include "esp32_rc_espnow.h"
#endif

#ifdef ENABLE_ESP32_RC_NRF24
#include "esp32_rc_nrf24.h"
#endif

#ifdef ENABLE_ESP32_RC_WIFI
#include "esp32_rc_wifi.h"
#endif

#ifdef ENABLE_ESP32_RC_BLE
// #include "esp32_rc_ble.h"  // Future implementation
#endif

// =======================================================
// PROTOCOL FACTORY FUNCTION
// =======================================================

/**
 * @brief Create a protocol instance based on enum
 * @param protocol The protocol type to create
 * @param fast_mode Enable fast mode (low latency, may drop messages)
 * @return Pointer to ESP32RemoteControl instance or nullptr if protocol not enabled
 * 
 * Example usage:
 *   auto controller = createProtocolInstance(RC_PROTO_ESPNOW, false);
 *   if (controller) {
 *     controller->connect();
 *   }
 */
inline ESP32RemoteControl* createProtocolInstance(RCProtocol_t protocol, bool fast_mode = false) {
    switch (protocol) {
#ifdef ENABLE_ESP32_RC_ESPNOW
        case RC_PROTO_ESPNOW:
            return new ESP32_RC_ESPNOW(fast_mode);
#endif

#ifdef ENABLE_ESP32_RC_NRF24
        case RC_PROTO_NRF24:
            return new ESP32_RC_NRF24(fast_mode);
#endif

#ifdef ENABLE_ESP32_RC_WIFI
        case RC_PROTO_WIFI:
            return new ESP32_RC_WIFI(fast_mode);
#endif

#ifdef ENABLE_ESP32_RC_BLE
        case RC_PROTO_BLE:
            // return new ESP32_RC_BLE(fast_mode);  // Future implementation
            return nullptr;
#endif

        default:
            return nullptr;
    }
}

/**
 * @brief Check if a protocol is available at compile time
 * @param protocol The protocol to check
 * @return true if protocol is enabled, false otherwise
 */
inline bool isProtocolAvailable(RCProtocol_t protocol) {
    switch (protocol) {
#ifdef ENABLE_ESP32_RC_ESPNOW
        case RC_PROTO_ESPNOW: return true;
#endif

#ifdef ENABLE_ESP32_RC_NRF24
        case RC_PROTO_NRF24: return true;
#endif

#ifdef ENABLE_ESP32_RC_WIFI
        case RC_PROTO_WIFI: return true;
#endif

#ifdef ENABLE_ESP32_RC_BLE
        case RC_PROTO_BLE: return true;
#endif

        default: return false;
    }
}