#include "esp32_rc_espnow.h"


ESP32_RC_ESPNOW* ESP32_RC_ESPNOW::instance_ = nullptr;

/**
 * @brief Constructor for ESP-NOW protocol implementation
 * 
 * Initializes the ESP-NOW protocol stack including WiFi, channels, and peer management.
 * Sets up static callbacks and broadcast peer for immediate communication capability.
 * 
 * @param fast_mode If true, uses queue depth of 1 for low-latency (may drop messages)
 *                  If false, uses queue depth of 10 for reliability
 * 
 * Example usage:
 *   ESP32_RC_ESPNOW* controller = new ESP32_RC_ESPNOW(false);  // Reliable mode
 *   ESP32_RC_ESPNOW* controller = new ESP32_RC_ESPNOW(true);   // Low-latency mode
 */
ESP32_RC_ESPNOW::ESP32_RC_ESPNOW(bool fast_mode)
    : ESP32RemoteControl(fast_mode) {
    // Initialize the ESPNOW instance
    LOG_INFO( "[ESP32_RC_ESPNOW] Initializing ESPNOW...");
    instance_ = this;
    if (!init()) {
        LOG_ERROR("ESPNOW init failed!");
        SYS_HALT;   
    }
}

/**
 * @brief Destructor for ESP-NOW protocol implementation
 * 
 * Cleanly shuts down the ESP-NOW protocol stack and releases resources.
 * Called automatically when the controller object is destroyed.
 */
ESP32_RC_ESPNOW::~ESP32_RC_ESPNOW() {
    esp_now_deinit();
}

/**
 * @brief Initialize ESP-NOW protocol stack
 * 
 * Sets up WiFi in STA mode, configures channel and TX power, initializes ESP-NOW,
 * adds broadcast peer for discovery, and registers callback functions.
 * 
 * @return true if initialization successful, false otherwise
 * 
 * Internal method called by constructor - not for direct use
 */
bool ESP32_RC_ESPNOW::init() {
    WiFi.mode(WIFI_STA);
      
    // Set WiFi channel for ESPNOW
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
   
    esp_wifi_set_max_tx_power(ESPNOW_OUTPUT_POWER);
   
    esp_err_t initResult = esp_now_init();
    LOG_DEBUG("esp_now_init: "); LOG_DEBUG(initResult);
    if (initResult != ESP_OK) {
        return false;
    }

    // Get my MAC address
    WiFi.macAddress(my_addr_);  // Get my MAC address
    memcpy(my_address_, my_addr_, RC_ADDR_SIZE);  // Also set generic address

    // Always add broadcast peer so send always works
    esp_now_peer_info_t peerInfo = {};
    uint8_t bcast[RC_ADDR_SIZE] = RC_BROADCAST_MAC;
    memcpy(peerInfo.peer_addr, bcast, RC_ADDR_SIZE);
    peerInfo.channel = ESPNOW_CHANNEL;
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;
    if (!esp_now_is_peer_exist(bcast)) {
        esp_err_t addPeerRes = esp_now_add_peer(&peerInfo);
        LOG_DEBUG("esp_now_add_peer (broadcast): ");
        LOG_DEBUG(addPeerRes);
    }

    esp_now_register_recv_cb(ESP32_RC_ESPNOW::onDataRecvStatic);
    esp_now_register_send_cb(ESP32_RC_ESPNOW::onDataSentStatic);

    return true;
}



/**
 * @brief Low-level message transmission with retry logic
 * 
 * Sends a message via ESP-NOW protocol with automatic retry on failure.
 * Uses broadcast address when not connected, peer address when connected.
 * Implements exponential backoff retry strategy for improved reliability.
 * 
 * @param msg The message structure to send (32 bytes fixed size)
 * 
 * Message routing:
 * - DISCONNECTED state: Sends to broadcast address (FF:FF:FF:FF:FF:FF)
 * - CONNECTED state: Sends directly to established peer
 * 
 * Retry behavior:
 * - Up to 3 retries with 10ms delays
 * - Logs each retry attempt
 * - Updates error metrics on final failure
 */
void ESP32_RC_ESPNOW::lowLevelSend(const RCMessage_t &msg) {
  esp_err_t sendResult = ESP_FAIL;
  uint8_t* target_addr;
  static uint8_t bcast[RC_ADDR_SIZE] = RC_BROADCAST_MAC;
  
  // Determine target address
  if (conn_state_ == RCConnectionState_t::CONNECTED) {
    target_addr = peer_addr_;
  } else {
    target_addr = bcast;
  }
  
  // Retry logic for failed sends
  for (int retry = 0; retry <= MAX_SEND_RETRIES; retry++) {
    sendResult = esp_now_send(target_addr, reinterpret_cast<const uint8_t *>(&msg), sizeof(RCMessage_t));
    
    if (sendResult == ESP_OK) {
      if (retry > 0) {
        LOG_DEBUG("ESP-NOW send succeeded on retry %d", retry);
      }
      break;  // Success, exit retry loop
    }
    
    // Log error and retry if not last attempt
    if (retry < MAX_SEND_RETRIES) {
      LOG_DEBUG("ESP-NOW send failed (attempt %d/%d): %s, retrying...", 
                retry + 1, MAX_SEND_RETRIES + 1, esp_err_to_name(sendResult));
      DELAY(pdMS_TO_TICKS(RETRY_DELAY_MS));
    }
  }
  
  // Final error handling and metrics update
  if (sendResult != ESP_OK) {
    LOG_ERROR("ESP-NOW send failed after %d retries: %s", MAX_SEND_RETRIES + 1, esp_err_to_name(sendResult));
    send_metrics_.addFailure();  // Track failed transmission
  } else {
    send_metrics_.addSuccess();  // Track successful transmission
  }
}

/**
 * @brief Set and validate peer MAC address for direct communication
 * 
 * Validates the provided MAC address and adds it as an ESP-NOW peer.
 * Enables direct point-to-point communication instead of broadcast.
 * 
 * @param peer_addr 6-byte MAC address of the peer device
 *                  Must not be NULL or all zeros
 * 
 * Example usage:
 *   uint8_t peer_mac[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
 *   controller->setPeerAddr(peer_mac);
 * 
 * Validation checks:
 * - Non-null pointer
 * - Non-zero MAC address
 * - Adds to ESP-NOW peer list if not already present
 */
void ESP32_RC_ESPNOW::setPeerAddr(const uint8_t *peer_addr) {
  // Validate MAC address
  if (!peer_addr) {
    LOG_ERROR("Invalid peer address: null pointer");
    return;
  }
  
  // Check if it's not a null MAC
  uint8_t null_mac[RC_ADDR_SIZE] = {0};
  if (memcmp(peer_addr, null_mac, RC_ADDR_SIZE) == 0) {
    LOG_ERROR("Invalid peer address: null MAC");
    return;
  }

  // Set peer address and update connection state, 
  // ESPNOW specific pairing steps
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peer_addr, RC_ADDR_SIZE);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = 0;
  peerInfo.ifidx = WIFI_IF_STA;
  
  if (!esp_now_is_peer_exist(peer_addr)) {
    esp_err_t res = esp_now_add_peer(&peerInfo);
    if (res != ESP_OK) {
      LOG_ERROR("Failed to add ESP-NOW peer: %s", esp_err_to_name(res));
      return;
    }
    LOG_DEBUG("ESP-NOW peer added successfully");
  }

  // Set the peer address in the base class
  ESP32RemoteControl::setPeerAddr(peer_addr);  // Call base class method
};

/**
 * @brief Set and validate peer MAC address (generic interface)
 * 
 * Generic interface version that accepts RCAddress_t structure.
 * Validates that address size matches ESP-NOW requirements (6 bytes).
 * 
 * @param peer_addr Generic address structure containing MAC address
 * 
 * Validation:
 * - Must be exactly 6 bytes (MAC address size)
 * - Must not be null or broadcast-only
 * - Adds to ESP-NOW peer list if valid
 */
// Removed - using base class implementation now

/**
 * @brief Remove current peer and return to broadcast mode
 * 
 * Removes the current peer from ESP-NOW peer list and clears the peer address.
 * Future transmissions will use broadcast until a new peer is set.
 * 
 * Automatically called when:
 * - Connection timeout occurs
 * - Explicit disconnection requested
 * - Peer becomes unreachable
 */
void ESP32_RC_ESPNOW::unsetPeerAddr() {
  // Remove peer from ESPNOW
  if (esp_now_is_peer_exist(peer_addr_)) {
    esp_err_t res = esp_now_del_peer(peer_addr_);
    if (res != ESP_OK) {
      LOG_ERROR("Failed to remove ESP-NOW peer: %s", esp_err_to_name(res));
    } else {
      LOG_DEBUG("ESP-NOW peer removed successfully");
    }
  }
  ESP32RemoteControl::unsetPeerAddr();  // Call base class method
}

// --- Static Callback Wrappers ---
/**
 * @brief Static callback wrapper for ESP-NOW data reception
 * 
 * Called by ESP-NOW stack when data is received from any peer.
 * Converts static callback to instance method call for proper object context.
 * 
 * @param mac MAC address of the sender (6 bytes)
 * @param data Raw received data buffer
 * @param len Length of received data in bytes
 * 
 * Processing flow:
 * 1. Validates instance exists
 * 2. Parses raw data into RCMessage_t structure
 * 3. Overwrites from_addr with actual sender MAC
 * 4. Forwards to base class for processing
 * 
 * Note: This is an ESP-NOW system callback - not called directly by user code
 */
void ESP32_RC_ESPNOW::onDataRecvStatic(const uint8_t *mac, const uint8_t *data, int len) {
  // Called when data is received
  LOG_DEBUG("ESPNOW: Data received");
  if (instance_) {
    RCMessage_t msg = instance_->parseRawData(data, len);

    // Overwrite from_addr with the actual sender MAC from ESPNOW
    memcpy(msg.from_addr, mac, RC_ADDR_SIZE);
    instance_->onDataReceived(msg);
  }
}

/**
 * @brief Static callback wrapper for ESP-NOW transmission confirmation
 * 
 * Called by ESP-NOW stack after each transmission attempt.
 * Provides delivery confirmation for sent messages.
 * 
 * @param mac MAC address of the intended recipient
 * @param status ESP_NOW_SEND_SUCCESS or ESP_NOW_SEND_FAIL
 * 
 * Note: This is an ESP-NOW system callback - not called directly by user code
 */
void ESP32_RC_ESPNOW::onDataSentStatic(const uint8_t *mac,  esp_now_send_status_t status) {
    // Called when data is sent
   if (instance_) instance_->onDataSentInternal(mac, status);
}

/**
 * @brief Parse and validate raw ESP-NOW data into message structure
 * 
 * Converts raw byte stream from ESP-NOW into typed RCMessage_t structure.
 * Performs comprehensive validation to ensure data integrity.
 * 
 * @param data Raw data buffer from ESP-NOW callback
 * @param len Length of data buffer in bytes
 * @return Parsed and validated RCMessage_t structure (empty if invalid)
 * 
 * Validation performed:
 * - Non-null data pointer
 * - Exact size match (32 bytes)
 * - Size within maximum limits
 * - Valid message type (DATA or HEARTBEAT)
 * 
 * Returns empty message on any validation failure with error logging.
 */
RCMessage_t ESP32_RC_ESPNOW::parseRawData(const uint8_t *data, size_t len) {
  // Parse raw data into RCMessage_t structure
  RCMessage_t msg = {};
  
  // Validate input parameters
  if (!data) {
    LOG_ERROR("Invalid data: null pointer");
    return msg;  // Return empty message
  }
  
  if (len != sizeof(RCMessage_t)) {
    LOG_ERROR("Invalid message size: expected %d, got %d", sizeof(RCMessage_t), len);
    return msg;  // Return empty message
  }
  
  // Validate message size is within bounds
  if (len > RC_MESSAGE_MAX_SIZE) {
    LOG_ERROR("Message too large: %d bytes (max: %d)", len, RC_MESSAGE_MAX_SIZE);
    return msg;
  }
  
  memcpy(&msg, data, sizeof(RCMessage_t));
  
  // Validate message type
  if (msg.type != RCMSG_TYPE_DATA && msg.type != RCMSG_TYPE_HEARTBEAT) {
    LOG_ERROR("Invalid message type: %d", msg.type);
    memset(&msg, 0, sizeof(RCMessage_t));  // Clear invalid message
  }
  
  return msg;
}



/**
 * @brief Handle ESP-NOW transmission status internally
 * 
 * Processes delivery confirmation from ESP-NOW stack.
 * Updates error metrics for failed transmissions.
 * 
 * @param mac MAC address of intended recipient
 * @param status ESP_NOW_SEND_SUCCESS or ESP_NOW_SEND_FAIL
 * 
 * Future enhancements:
 * - Delivery confirmation tracking
 * - Automatic retry triggers
 * - Peer reachability status
 */
void ESP32_RC_ESPNOW::onDataSentInternal(const uint8_t *mac, esp_now_send_status_t status) {
  // Handle send status for delivery confirmation tracking
  if (status != ESP_NOW_SEND_SUCCESS) {
    LOG_DEBUG("ESP-NOW send delivery failed to peer");
    // Note: Actual send metrics are tracked in lowLevelSend() based on esp_now_send() result
    // This callback provides additional delivery confirmation info
  } else {
    LOG_DEBUG("ESP-NOW message delivered successfully");
  }
}

// Configuration interface implementation
/**
 * @brief Configure ESP-NOW protocol-specific parameters
 * 
 * Allows runtime configuration of ESP-NOW specific settings.
 * Changes take effect immediately.
 * 
 * @param key Configuration parameter name
 * @param value Configuration parameter value as string
 * @return true if configuration applied successfully, false otherwise
 * 
 * Supported configurations:
 * - "channel": WiFi channel (1-14)
 *   Example: setProtocolConfig("channel", "6")
 * - "tx_power": Transmission power (8-84, in 0.25dBm units)
 *   Example: setProtocolConfig("tx_power", "52")  // 13dBm
 * 
 * Note: Channel changes affect all WiFi operations on the device
 */
bool ESP32_RC_ESPNOW::setProtocolConfig(const char* key, const char* value) {
  if (!key || !value) return false;
  
  if (strcmp(key, "channel") == 0) {
    int channel = atoi(value);
    if (channel >= 1 && channel <= 14) {
      esp_wifi_set_promiscuous(true);
      esp_err_t result = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
      esp_wifi_set_promiscuous(false);
      return (result == ESP_OK);
    }
  }
  else if (strcmp(key, "tx_power") == 0) {
    int power = atoi(value);
    if (power >= 8 && power <= 84) {  // ESP32 TX power range: 2-20 dBm (8-84 in 0.25dBm units)
      esp_err_t result = esp_wifi_set_max_tx_power(power);
      return (result == ESP_OK);
    }
  }
  
  return false;  // Unsupported configuration
}

/**
 * @brief Retrieve current ESP-NOW protocol configuration
 * 
 * Queries current protocol settings and returns them as strings.
 * Useful for debugging and runtime introspection.
 * 
 * @param key Configuration parameter name to query
 * @param value Buffer to store the result string
 * @param len Maximum length of the value buffer
 * @return true if parameter found and retrieved, false otherwise
 * 
 * Supported queries:
 * - "protocol": Returns "ESPNOW"
 *   Example: getProtocolConfig("protocol", buffer, sizeof(buffer))
 * - "channel": Returns current WiFi channel as string
 *   Example: getProtocolConfig("channel", buffer, sizeof(buffer)) // "6"
 * 
 * Buffer management:
 * - Always null-terminates the result string
 * - Truncates if result exceeds buffer length
 */
bool ESP32_RC_ESPNOW::getProtocolConfig(const char* key, char* value, size_t len) {
  if (!key || !value || len == 0) return false;
  
  if (strcmp(key, "protocol") == 0) {
    strncpy(value, "ESPNOW", len - 1);
    value[len - 1] = '\0';
    return true;
  }
  else if (strcmp(key, "channel") == 0) {
    uint8_t primary;
    wifi_second_chan_t secondary;
    esp_err_t result = esp_wifi_get_channel(&primary, &secondary);
    if (result == ESP_OK) {
      snprintf(value, len, "%d", primary);
      return true;
    }
  }
  
  return false;  // Unsupported configuration
}

/**
 * @brief Create ESP-NOW broadcast address
 * 
 * Creates a 6-byte MAC broadcast address (FF:FF:FF:FF:FF:FF) suitable
 * for ESP-NOW protocol discovery and broadcast communication.
 * 
 * @return Generic address structure containing MAC broadcast address
 */
void ESP32_RC_ESPNOW::createBroadcastAddress(RCAddress_t& broadcast_addr) const {
  uint8_t broadcast_mac[RC_ADDR_SIZE] = RC_BROADCAST_MAC;
  memcpy(broadcast_addr, broadcast_mac, RC_ADDR_SIZE);
}

