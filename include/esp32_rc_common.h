#pragma once
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>  // For memcpy, strncpy
#include "config.h"


// ========== Protocol Selection Enum ==========
enum RCProtocol_t {
  RC_PROTO_ESPNOW = 0,
  RC_PROTO_WIFI = 1,
  RC_PROTO_BLE = 2,
  RC_PROTO_NRF24 = 3
};

//
#define QUEUE_DEPTH_SEND 10
#define QUEUE_DEPTH_RECV 10
#define RECV_MSG_TIMEOUT_MS 5

// ========== Common Macros ==========
#define HEARTBEAT_INTERVAL_MS 100  // Default heartbeat interval in ms
#define HEARTBEAT_TIMEOUT_MS  300  // Default heartbeat timeout in ms

// ========== Message Types ==========
enum : uint8_t {
  RCMSG_TYPE_DATA = 0,
  RCMSG_TYPE_HEARTBEAT = 3
};

// ========== Struct Sizes ==========
#define RC_MESSAGE_MAX_SIZE 32
#define RC_PAYLOAD_MAX_SIZE 25
#define RC_ADDR_SIZE 6        // MAC address (6 bytes)

// ========== Message Structure ==========

// ==== Begin packed layout ====
#pragma pack(push, 1)

/**
 * @brief Struct for 21-byte packed payload
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

// ========== Broadcast/Null MAC ==========
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

// ========== Metrics Struct (for statistics/debug) ==========
struct Metrics_t {
  unsigned long in = 0;
  unsigned long out = 0;
  unsigned long err = 0;
};

// ========== Connection State Enum ==========
enum class RCConnectionState_t : uint8_t {
  DISCONNECTED = 0,
  CONNECTING = 1,
  CONNECTED = 2,
  ERROR = 3
};



