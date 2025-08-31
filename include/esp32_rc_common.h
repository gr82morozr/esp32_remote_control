#pragma once
/*
 * ESP32 Remote Control - Common Definitions
 * 
 * This file contains internal framework settings, data structures, and constants.
 * These are optimized defaults that should work well for most applications.
 * 
 * For user-configurable settings (pins, channels, etc.), see esp32_rc_user_config.h
 */

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>  // For memcpy, strncpy
#include "esp32_rc_user_config.h"  // Include user settings first



// ========== Protocol Selection Enum ==========
enum RCProtocol_t {
  RC_PROTO_ESPNOW   = 0,
  RC_PROTO_WIFI     = 1,
  RC_PROTO_BLE      = 2,
  RC_PROTO_NRF24    = 3
};

// Helper function to convert protocol enum to string
inline const char* protocolToString(RCProtocol_t protocol) {
  switch (protocol) {
    case RC_PROTO_ESPNOW: return "ESPNOW";
    case RC_PROTO_WIFI:   return "WIFI";
    case RC_PROTO_BLE:    return "BLE";
    case RC_PROTO_NRF24:  return "NRF24";
    default:              return "UNKNOWN";
  }
}

// =======================================================
// INTERNAL FRAMEWORK CONFIGURATION
// =======================================================
/**
 * These settings control the internal operation of the RC framework.
 * They are optimized defaults that work well for most applications.
 * Advanced users can override these by defining them before including headers.
 */

// --- Message Queue Settings ---
#ifndef QUEUE_DEPTH_SEND
#define QUEUE_DEPTH_SEND        10    // Send queue depth (number of messages)
#endif
#ifndef QUEUE_DEPTH_RECV  
#define QUEUE_DEPTH_RECV        10    // Receive queue depth (number of messages)
#endif
#ifndef RECV_MSG_TIMEOUT_MS
#define RECV_MSG_TIMEOUT_MS     5     // Timeout for queue operations (ms)
#endif

// --- Retry Logic Settings ---
#ifndef MAX_SEND_RETRIES
#define MAX_SEND_RETRIES        3     // Framework-level send retries (not radio retries)
#endif
#ifndef RETRY_DELAY_MS
#define RETRY_DELAY_MS          10    // Delay between framework retries (ms)
#endif

// --- Connection Management ---
#ifndef HEARTBEAT_INTERVAL_MS
#define HEARTBEAT_INTERVAL_MS   100   // How often to send heartbeats (ms)
#endif
#ifndef HEARTBEAT_TIMEOUT_MS  
#define HEARTBEAT_TIMEOUT_MS    300   // Connection timeout without heartbeat (ms)
#endif

// ========== Message Types ==========
enum : uint8_t {
  RCMSG_TYPE_DATA = 0,
  RCMSG_TYPE_HEARTBEAT = 3
};

// ========== Struct Sizes ==========
#define RC_MESSAGE_MAX_SIZE     32
#define RC_PAYLOAD_MAX_SIZE     25
#define RC_ADDR_SIZE 6          // MAC address (6 bytes) - kept for compatibility
#define RC_MAX_ADDR_SIZE 16     // Maximum address size for any protocol (BLE UUID, etc.)

// ========== Message Structure ==========

// ==== Begin packed layout ====
#pragma pack(push, 1)


#ifndef RC_PAYLOAD_T_DEFINED
#define RC_PAYLOAD_T_DEFINED
/**
 * @brief Struct for 25-byte packed payload
 */
struct RCPayload_t {
  uint8_t id1;
  uint8_t id2;
  uint8_t id3;
  uint8_t id4;
  float value1;
  float value2;
  float value3;
  float value4;
  float value5;
  uint8_t flags;
};
#endif



/**
 * @brief Struct for full 32-byte RC message
 */
struct RCMessage_t {
  uint8_t type;                          // 1 byte
  uint8_t from_addr[RC_ADDR_SIZE];       // 6 bytes
  uint8_t payload[RC_PAYLOAD_MAX_SIZE];  // 25 bytes

  // Payload accessors
  RCPayload_t *getPayload() { return reinterpret_cast<RCPayload_t *>(payload); }

  const RCPayload_t *getPayload() const {
    return reinterpret_cast<const RCPayload_t *>(payload);
  }

  void setPayload(const RCPayload_t &data) {
    static_assert(sizeof(RCPayload_t) == RC_PAYLOAD_MAX_SIZE,
                  "Payload size mismatch");
    memcpy(payload, &data, sizeof(RCPayload_t));
  }
};

#pragma pack(pop)
// ==== End packed layout ====

// Compile-time size check
static_assert(sizeof(RCPayload_t) == RC_PAYLOAD_MAX_SIZE,   "RCPayload_t must be 21 bytes");
static_assert(sizeof(RCMessage_t) == RC_MESSAGE_MAX_SIZE,   "RCMessage_t must be 32 bytes");

// ========== Address Abstraction ==========
/**
 * @brief Generic address structure for all protocols
 */
struct RCAddress_t {
  uint8_t data[RC_MAX_ADDR_SIZE];  // Address data (protocol-specific format)
  uint8_t size;                    // Actual size of address (1-16 bytes)
  
  // Constructor for empty address
  RCAddress_t() : size(0) { 
    memset(data, 0, RC_MAX_ADDR_SIZE); 
  }
  
  // Constructor from byte array
  RCAddress_t(const uint8_t* addr, uint8_t addr_size) {
    setAddress(addr, addr_size);
  }
  
  // Set address data
  void setAddress(const uint8_t* addr, uint8_t addr_size) {
    if (addr && addr_size > 0 && addr_size <= RC_MAX_ADDR_SIZE) {
      size = addr_size;
      memcpy(data, addr, addr_size);
      // Zero remaining bytes for consistency
      if (addr_size < RC_MAX_ADDR_SIZE) {
        memset(data + addr_size, 0, RC_MAX_ADDR_SIZE - addr_size);
      }
    } else {
      clear();
    }
  }
  
  // Clear address
  void clear() {
    size = 0;
    memset(data, 0, RC_MAX_ADDR_SIZE);
  }
  
  // Check if address is valid/set
  bool isValid() const {
    return (size > 0 && size <= RC_MAX_ADDR_SIZE);
  }
  
  // Check if address is broadcast/empty
  bool isBroadcast() const {
    if (!isValid()) return false;
    for (uint8_t i = 0; i < size; i++) {
      if (data[i] != 0xFF) return false;
    }
    return true;
  }
  
  // Comparison operators
  bool operator==(const RCAddress_t& other) const {
    return (size == other.size && memcmp(data, other.data, size) == 0);
  }
  
  bool operator!=(const RCAddress_t& other) const {
    return !(*this == other);
  }
};

// ========== Broadcast/Null MAC (Legacy) ==========
#define RC_BROADCAST_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define RC_NULL_MAC {0, 0, 0, 0, 0, 0}

// ========== Safe String Copy Macro ==========
#ifndef RC_SAFE_STRCPY
#define RC_SAFE_STRCPY(dest, src, len) \
  do {                                 \
    strncpy((dest), (src), (len) - 1); \
    (dest)[(len) - 1] = 0;             \
  } while (0)
#endif

// ========== Global Metrics Configuration ==========
extern bool RC_METRICS_ENABLED;  // Global flag to enable/disable metrics calculation

// ========== Metrics Struct (for statistics/debug) ==========
struct Metrics_t {
  unsigned long successful = 0;    // Successfully sent/received messages
  unsigned long failed = 0;        // Failed send/receive attempts
  unsigned long total = 0;         // Total attempts (successful + failed)
  
  // Rate calculation support
  unsigned long start_time_ms = 0; // When metrics started tracking
  unsigned long last_reset_ms = 0; // When metrics were last reset
  
  // Legacy compatibility
  unsigned long in = 0;            // Deprecated - use successful for receive metrics
  unsigned long out = 0;           // Deprecated - use successful for send metrics
  unsigned long err = 0;           // Deprecated - use failed
  
  // Constructor
  Metrics_t() {
    unsigned long now = millis();
    start_time_ms = now;
    last_reset_ms = now;
  }
  
  // Reset all counters
  void reset() {
    successful = failed = total = 0;
    in = out = err = 0;
    last_reset_ms = millis();
  }
  
  // Calculate success rate (0-100%)
  float getSuccessRate() const {
    return (total > 0) ? (successful * 100.0f / total) : 0.0f;
  }
  
  // Calculate transmission rate (transactions per second)
  float getTransactionRate() const {
    unsigned long elapsed_ms = millis() - start_time_ms;
    if (elapsed_ms < 1000) return 0.0f;  // Need at least 1 second of data
    return (total * 1000.0f) / elapsed_ms;
  }
  
  // Calculate success rate (successful transactions per second)
  float getSuccessRate_TPS() const {
    unsigned long elapsed_ms = millis() - start_time_ms;
    if (elapsed_ms < 1000) return 0.0f;  // Need at least 1 second of data
    return (successful * 1000.0f) / elapsed_ms;
  }
  
  // Add successful operation
  void addSuccess() {
    if (!RC_METRICS_ENABLED) return;  // Skip if metrics disabled
    successful++;
    total++;
    out++; // Legacy compatibility
  }
  
  // Add failed operation
  void addFailure() {
    if (!RC_METRICS_ENABLED) return;  // Skip if metrics disabled
    failed++;
    total++;
    err++; // Legacy compatibility
  }
};

// ========== Connection State Enum ==========
enum class RCConnectionState_t : uint8_t {
  DISCONNECTED = 0,
  CONNECTING = 1,
  CONNECTED = 2,
  ERROR = 3
};






// ==============================================


