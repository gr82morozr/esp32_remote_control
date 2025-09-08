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



// ========== Protocol Selection Type ==========
// Protocol constants are defined in esp32_rc_user_config.h as #define values
// Use int instead of enum for compatibility with #define constants
typedef int RCProtocol_t;

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
  RCMSG_TYPE_HEARTBEAT = 3,
  RCMSG_TYPE_IP_DISCOVERY = 4  // WiFi IP discovery broadcast
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

// ========== Simplified Address Handling ==========
// Use simple 6-byte MAC addresses for all protocols (standard for ESP32)
typedef uint8_t RCAddress_t[RC_ADDR_SIZE];

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
extern bool rc_metrics_enabled;  // Global flag to enable/disable metrics calculation

// ========== Lightweight Metrics with Sliding Window TPS ==========
struct Metrics_t {
  uint16_t successful = 0;  // Successfully sent/received messages  
  uint16_t failed = 0;      // Failed send/receive attempts
  
  // Circular buffer for last 5 seconds of activity (1 entry per 100ms = 50 entries)
  static const uint8_t WINDOW_SLOTS = 50;  // 5 seconds ÷ 100ms
  mutable uint8_t activity_buffer[WINDOW_SLOTS] = {0};  // Count per 100ms slot
  mutable uint8_t current_slot = 0;
  mutable uint32_t last_slot_update_ms = 0;
  
  inline void addSuccess() { 
    if (rc_metrics_enabled) {
      successful++; 
      recordActivity();
    }
  }
  inline void addFailure() { 
    if (rc_metrics_enabled) {
      failed++; 
      recordActivity();
    }
  }
  inline void reset() { 
    successful = failed = 0;
    memset(activity_buffer, 0, sizeof(activity_buffer));
    current_slot = 0;
    last_slot_update_ms = millis();
  }
  inline uint16_t getTotal() const { return successful + failed; }
  inline float getSuccessRate() const { 
    uint16_t total = getTotal();
    return total ? (successful * 100.0f / total) : 0.0f; 
  }
  inline float getTransactionRate() const {
    updateSlots();  // Ensure buffer is current
    
    // Sum all activity in the last 5 seconds
    uint16_t total_in_window = 0;
    for (uint8_t i = 0; i < WINDOW_SLOTS; i++) {
      total_in_window += activity_buffer[i];
    }
    
    // Convert to transactions per second (5 second window)
    return total_in_window / 5.0f;
  }
  
private:
  inline void recordActivity() {
    updateSlots();
    
    // Increment current slot (max 255 per 100ms slot)
    if (activity_buffer[current_slot] < 255) {
      activity_buffer[current_slot]++;
    }
  }
  
  inline void updateSlots() const {
    uint32_t now = millis();
    uint32_t elapsed = now - last_slot_update_ms;
    
    // Move to next slot every 100ms
    if (elapsed >= 100) {
      uint8_t slots_to_advance = elapsed / 100;
      
      // Clear old slots and advance
      for (uint8_t i = 0; i < slots_to_advance && i < WINDOW_SLOTS; i++) {
        current_slot = (current_slot + 1) % WINDOW_SLOTS;
        activity_buffer[current_slot] = 0;  // Clear new slot
      }
      
      last_slot_update_ms = now;
    }
  }
};

// ========== Connection State Enum ==========
enum class RCConnectionState_t : uint8_t {
  DISCONNECTED = 0,
  CONNECTING = 1,
  CONNECTED = 2,
  ERROR = 3
};

// ========== Simple Discovery ==========
struct RCDiscoveryResult_t {
  bool discovered = false;       // True if peer was discovered  
  uint8_t peer_addr[RC_ADDR_SIZE] = {0}; // Discovered peer MAC address
};






// ==============================================
