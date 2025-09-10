#include "esp32_rc_nrf24.h"

ESP32_RC_NRF24* ESP32_RC_NRF24::instance_ = nullptr;

// ========== Constructor / Destructor ==========

/**
 * @brief Constructor for NRF24 protocol implementation
 * 
 * Initializes NRF24L01+ radio, SPI bus, generates unique address from MAC,
 * and starts background receive task for continuous packet monitoring.
 * 
 * @param fast_mode If true, uses queue depth of 1 for low-latency
 *                  If false, uses queue depth of 10 for reliability
 */
ESP32_RC_NRF24::ESP32_RC_NRF24(bool fast_mode)
    : ESP32RemoteControl(fast_mode) {
    instance_ = this;
    LOG_INFO("[ESP32_RC_NRF24] Initializing NRF24L01+...");
    
    // Initialize SPI bus
    spiBus_ = new SPIClass(NRF_SPI_BUS);
    spiBus_->begin(PIN_NRF_SCK, PIN_NRF_MISO, PIN_NRF_MOSI, PIN_NRF_CSN);
    
    // Give SPI bus time to stabilize
    DELAY(10);
    
    if (!init()) {
        LOG_ERROR("NRF24 initialization failed!");
        SYS_HALT;
    }

    LOG_INFO("NRF24L01+ initialized successfully");

    // Clean up existing task if any
    if (receiveTaskHandle_) {
      vTaskDelete(receiveTaskHandle_);
    }

    // Create background receive task
    xTaskCreatePinnedToCore(
      receiveLoopWrapper,
      "NRF24Receive", 
      4096,  // Increased stack size
      this, 
      2, 
      &receiveTaskHandle_, 
      0
    );
    
    if (receiveTaskHandle_ == nullptr) {
        LOG_ERROR("Failed to create NRF24 receive task");
        SYS_HALT;
    }
    
    LOG("NRF24 receiver task created successfully");
}

/**
 * @brief Destructor for NRF24 protocol implementation
 * 
 * Cleanly shuts down NRF24 radio, deletes receive task, and releases SPI resources.
 */
ESP32_RC_NRF24::~ESP32_RC_NRF24() {
  if (receiveTaskHandle_) {
    vTaskDelete(receiveTaskHandle_);
    receiveTaskHandle_ = nullptr;
  }
  radio_.powerDown();
  if (spiBus_) {
    delete spiBus_;
    spiBus_ = nullptr;
  }
}

// ========== Core Protocol Methods ==========

/**
 * @brief Initialize NRF24L01+ radio and configure pipes
 * 
 * Sets up radio parameters, generates addresses, configures reading/writing pipes,
 * and establishes initial broadcast listening mode.
 * 
 * @return true if initialization successful, false otherwise
 */
bool ESP32_RC_NRF24::init() {
    // Generate unique NRF24 address from ESP32 MAC
    generateMyNrfAddress();
    
    // Initialize radio hardware with SPI bus
    if (!radio_.begin(spiBus_)) {
        LOG_ERROR("NRF24L01+ hardware initialization failed!");
        return false;
    }
    
    // Give radio time to initialize
    DELAY(10);
    
    // Check if chip is connected before configuration
    if (!radio_.isChipConnected()) {
        LOG_ERROR("NRF24 chip not detected - check wiring");
        return false;
    }
    
    // Configure radio parameters from user config
    radio_.setChannel(NRF24_CHANNEL);
    radio_.setDataRate(NRF24_DATA_RATE);
    radio_.setPALevel(NRF24_PA_LEVEL);
    radio_.setRetries(NRF24_RETRY_DELAY, NRF24_RETRY_COUNT);
    radio_.enableDynamicPayloads();
    radio_.setCRCLength(RF24_CRC_16);
    radio_.setAutoAck(true);
    
    // Flush any existing data
    radio_.flush_rx();
    radio_.flush_tx();
    
    // Set up reading pipes
    radio_.stopListening();
    
    // Pipe 0: Broadcast address (no auto-ack for broadcast) 
    radio_.openReadingPipe(0, NRF_BROADCAST_ADDR);
    radio_.setAutoAck(0, false);
    
    // Pipe 1: My unique address (with auto-ack for reliability)
    radio_.openReadingPipe(1, nrf_my_addr_);
    radio_.setAutoAck(1, true);
    
    radio_.startListening();
    
    // Set my address in generic format
    nrfToMacAddress(nrf_my_addr_, my_addr_);
    memcpy(my_address_, my_addr_, RC_ADDR_SIZE);
    
    LOG("NRF24 My Address: %s (NRF: %s)", formatAddr(my_addr_).c_str(), formatNrfAddr(nrf_my_addr_).c_str());
    LOG("NRF24 Config: Channel=%d, DataRate=%s, Power=%s, Retries=%d/%d", 
        NRF24_CHANNEL,
        (NRF24_DATA_RATE == RF24_250KBPS ? "250K" : 
         NRF24_DATA_RATE == RF24_1MBPS ? "1M" : 
         NRF24_DATA_RATE == RF24_2MBPS ? "2M" : "Unknown"),
        (NRF24_PA_LEVEL == RF24_PA_MIN ? "MIN" :
         NRF24_PA_LEVEL == RF24_PA_LOW ? "LOW" :
         NRF24_PA_LEVEL == RF24_PA_HIGH ? "HIGH" :
         NRF24_PA_LEVEL == RF24_PA_MAX ? "MAX" : "Unknown"),
        NRF24_RETRY_DELAY, NRF24_RETRY_COUNT);
    
    // Start in broadcast mode for discovery
    switchToBroadcastPipe();
    return true;
}

/**
 * @brief Low-level NRF24 message transmission with retry logic
 * 
 * Sends message via NRF24 radio with automatic retry on failure.
 * Uses broadcast address when not connected, peer address when connected.
 * 
 * @param msg The message structure to send (32 bytes)
 */
void ESP32_RC_NRF24::lowLevelSend(const RCMessage_t& msg) {
    bool sendSuccess = false;
    
    // Stop listening to send
    radio_.stopListening();
    
    // Try sending with retries
    for (int retry = 0; retry <= MAX_SEND_RETRIES; retry++) {
        // Use multicast flag for broadcast pipe (no ACK expected)
        bool multicast = (pipeType_ == 0);  // pipe 0 is broadcast
        sendSuccess = radio_.write(&msg, sizeof(msg), multicast);
        
        if (sendSuccess) {
            if (retry > 0) {
                LOG_DEBUG("NRF24 send succeeded on retry %d", retry);
            }
            break;  // Success, exit retry loop
        }
        
        // Log error and retry if not last attempt
        if (retry < MAX_SEND_RETRIES) {
            LOG_DEBUG("NRF24 send failed (attempt %d/%d), retrying...", 
                      retry + 1, MAX_SEND_RETRIES + 1);
            DELAY(RETRY_DELAY_MS);
        }
    }
    
    // Resume listening
    radio_.startListening();
    
    // Update metrics and log results (exclude heartbeat from metrics)
    if (msg.type != RCMSG_TYPE_HEARTBEAT) {
        if (sendSuccess) {
            send_metrics_.addSuccess();
            LOG_DEBUG("NRF24 sent message type %d successfully", msg.type);
        } else {
            send_metrics_.addFailure();
            LOG_ERROR("NRF24 send failed after %d retries (type %d, pipe %d)", 
                      MAX_SEND_RETRIES + 1, msg.type, pipeType_);
        }
    } else {
        // Still log heartbeat results but don't include in metrics
        if (sendSuccess) {
            LOG_DEBUG("NRF24 sent heartbeat successfully");
        } else {
            LOG_ERROR("NRF24 heartbeat send failed after %d retries (pipe %d)", 
                      MAX_SEND_RETRIES + 1, pipeType_);
        }
    }
}

/**
 * @brief Parse and validate raw NRF24 data into message structure
 * 
 * @param data Raw data from NRF24 radio
 * @param len Length of data
 * @return Parsed and validated RCMessage_t
 */
RCMessage_t ESP32_RC_NRF24::parseRawData(const uint8_t* data, size_t len) {
    RCMessage_t msg = {};
    
    // Validate input
    if (!data) {
        LOG_ERROR("Invalid data: null pointer");
        return msg;
    }
    
    if (len != sizeof(RCMessage_t)) {
        LOG_ERROR("Invalid message size: expected %d, got %d", sizeof(RCMessage_t), len);
        return msg;
    }
    
    // Copy and validate message
    memcpy(&msg, data, sizeof(RCMessage_t));
    
    // Validate message type
    if (msg.type != RCMSG_TYPE_DATA && msg.type != RCMSG_TYPE_HEARTBEAT) {
        LOG_ERROR("Invalid message type: %d", msg.type);
        memset(&msg, 0, sizeof(RCMessage_t));
    }
    
    return msg;
}

/**
 * @brief Check heartbeat timeout and handle disconnection
 * 
 * Extends base class heartbeat checking with NRF24-specific handling.
 * Switches back to broadcast pipe when connection is lost.
 */
void ESP32_RC_NRF24::checkHeartbeat() {
    ESP32RemoteControl::checkHeartbeat();  // Call base class method

    if (conn_state_ == RCConnectionState_t::DISCONNECTED) {
        // Reset handshake state and switch to broadcast for rediscovery
        handshake_completed_ = false;
        switchToBroadcastPipe();
        LOG_DEBUG("Connection lost, switched to broadcast mode");
    }
}

/**
 * @brief Create NRF24 broadcast address
 * 
 * @return Generic address structure with NRF24 broadcast
 */
void ESP32_RC_NRF24::createBroadcastAddress(RCAddress_t& broadcast_addr) const {
    uint8_t broadcast_mac[RC_ADDR_SIZE] = {0xF0, 0xF0, 0xF0, 0xF0, 0xAA, 0x00};
    memcpy(broadcast_addr, broadcast_mac, RC_ADDR_SIZE);
}

// ========== Receive Task and Message Handling ==========

void ESP32_RC_NRF24::receiveLoopWrapper(void* arg) {
  auto* self = static_cast<ESP32_RC_NRF24*>(arg);
  self->receiveLoop(arg);  // Call instance method
}

/**
 * @brief Background receive loop for continuous packet monitoring
 * 
 * Runs in separate FreeRTOS task to continuously monitor for incoming packets.
 * Handles address handshake, heartbeats, and data messages.
 * 
 * @param arg Pointer to ESP32_RC_NRF24 instance (unused, uses instance_)
 */
void ESP32_RC_NRF24::receiveLoop(void* arg) {
    while (true) {
        if (radio_.available()) {
            // Get dynamic payload size
            uint8_t len = radio_.getDynamicPayloadSize();
            
            // Skip invalid payloads
            if (len == 0 || len > 32) {
                uint8_t dump[32];
                if (len == 0 || len > 32) len = 32;
                radio_.read(dump, len);
                continue;
            }
            
            // Read the actual message
            uint8_t buf[32] = {0};
            radio_.read(buf, len);
            
            // Only process if it's our expected message size
            if (len != sizeof(RCMessage_t)) {
                LOG_DEBUG("Received payload size %d, expected %d", len, sizeof(RCMessage_t));
                continue;
            }
            
            // Parse and validate the message
            RCMessage_t parsedMsg = parseRawData(buf, len);
            
            // Ignore messages from self
            if (memcmp(parsedMsg.from_addr, my_addr_, RC_ADDR_SIZE) == 0) {
                continue;
            }
            
            LOG_DEBUG("NRF24 received type %d from %s", parsedMsg.type, formatAddr(parsedMsg.from_addr).c_str());
            
            // Handle different message types
            switch (parsedMsg.type) {
                case RCMSG_TYPE_HEARTBEAT:
                    if (!handshake_completed_) {
                        handleHandshakeMessage(parsedMsg);
                    }
                    // Process through base class for connection management
                    onDataReceived(parsedMsg);
                    break;
                    
                case RCMSG_TYPE_DATA:
                    if (handshake_completed_) {
                        onDataReceived(parsedMsg);
                    } else {
                        LOG_DEBUG("Data received before handshake complete, ignoring");
                    }
                    break;
                    
                default:
                    LOG_DEBUG("Unknown message type: %d", parsedMsg.type);
                    break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ========== Pipe Management ==========

/**
 * @brief Switch to broadcast pipe for discovery mode
 */
void ESP32_RC_NRF24::switchToBroadcastPipe() {
    if (pipeType_ == 0) {
        return;
    }
    
    radio_.stopListening();
    radio_.openWritingPipe(NRF_BROADCAST_ADDR);
    radio_.startListening();
    pipeType_ = 0;  // Set to broadcast pipe type
    LOG_DEBUG("Switched to BROADCAST pipe");
}

/**
 * @brief Switch to peer-specific pipe for direct communication
 */
void ESP32_RC_NRF24::switchToPeerPipe() {
  if (pipeType_ == 1) {
    return;
  };
  radio_.stopListening();
  radio_.openWritingPipe(nrf_peer_addr_);
  radio_.startListening();
  pipeType_ = 1;  // Set to peer pipe type
  LOG_DEBUG("Switched to PEER pipe, PeerAddress = %s", formatNrfAddr(nrf_peer_addr_).c_str());
}

// ========== Address Management ==========

/**
 * @brief Set peer address (legacy 6-byte MAC interface)
 * 
 * @param peer_addr 6-byte MAC address of peer
 */
void ESP32_RC_NRF24::setPeerAddr(const uint8_t* peer_addr) {
    if (!peer_addr) {
        LOG_ERROR("Invalid peer address: null pointer");
        return;
    }
    
    // Validate MAC address (not null MAC)
    uint8_t null_mac[RC_ADDR_SIZE] = {0};
    if (memcmp(peer_addr, null_mac, RC_ADDR_SIZE) == 0) {
        LOG_ERROR("Invalid peer address: null MAC");
        return;
    }
    
    // Store MAC address
    memcpy(peer_addr_, peer_addr, RC_ADDR_SIZE);
    
    // Convert to NRF24 address and store
    macToNrfAddress(peer_addr, nrf_peer_addr_);
    
    // Update generic address
    memcpy(peer_address_, peer_addr, RC_ADDR_SIZE);
    
    LOG_DEBUG("Peer address set: %s (NRF: %s)", 
              formatAddr(peer_addr_).c_str(), 
              formatNrfAddr(nrf_peer_addr_).c_str());
}

/**
 * @brief Set peer address (generic interface)
 * 
 * @param peer_addr Generic address structure
 */
// Removed - using base class implementation now

/**
 * @brief Clear peer address and return to broadcast mode
 */
void ESP32_RC_NRF24::unsetPeerAddr() {
    memset(peer_addr_, 0, RC_ADDR_SIZE);
    memset(nrf_peer_addr_, 0, 5);
    memset(peer_address_, 0, RC_ADDR_SIZE);
    handshake_completed_ = false;
    
    LOG_DEBUG("Peer address cleared, returning to broadcast mode");
}

/**
 * @brief Generate unique NRF24 address from ESP32 MAC address
 * 
 * Creates a 5-byte NRF24 address from the ESP32's unique MAC address.
 * Uses a fixed prefix (0xD2) plus 4 bytes from MAC for uniqueness.
 */
void ESP32_RC_NRF24::generateMyNrfAddress() {
    uint64_t chipId = ESP.getEfuseMac();
    
    // Generate 6-byte MAC address
    my_addr_[0] = 0xD2;  // Fixed prefix for NRF24 devices
    my_addr_[1] = (chipId >> 0) & 0xFF;
    my_addr_[2] = (chipId >> 8) & 0xFF;
    my_addr_[3] = (chipId >> 16) & 0xFF;
    my_addr_[4] = (chipId >> 24) & 0xFF;
    my_addr_[5] = (chipId >> 32) & 0xFF;
    
    // Convert to 5-byte NRF24 address
    macToNrfAddress(my_addr_, nrf_my_addr_);
}

// ========== Address Conversion Utilities ==========

/**
 * @brief Convert 6-byte MAC address to 5-byte NRF24 address
 * 
 * @param mac 6-byte MAC address input
 * @param nrf_addr 5-byte NRF24 address output
 */
void ESP32_RC_NRF24::macToNrfAddress(const uint8_t mac[6], uint8_t nrf_addr[5]) {
    // Use first 5 bytes of MAC for NRF24 address
    // XOR last byte into first byte for better distribution
    nrf_addr[0] = mac[0] ^ mac[5];
    nrf_addr[1] = mac[1];
    nrf_addr[2] = mac[2];
    nrf_addr[3] = mac[3];
    nrf_addr[4] = mac[4];
}

/**
 * @brief Convert 5-byte NRF24 address back to 6-byte MAC format
 * 
 * @param nrf_addr 5-byte NRF24 address input
 * @param mac 6-byte MAC address output
 */
void ESP32_RC_NRF24::nrfToMacAddress(const uint8_t nrf_addr[5], uint8_t mac[6]) {
    // Reconstruct MAC address (approximation)
    mac[0] = 0xD2;  // Fixed prefix
    mac[1] = nrf_addr[1];
    mac[2] = nrf_addr[2];
    mac[3] = nrf_addr[3];
    mac[4] = nrf_addr[4];
    mac[5] = nrf_addr[0] ^ mac[0];  // Reverse the XOR
}

/**
 * @brief Compare two NRF24 addresses (5 bytes)
 * 
 * @param a First NRF24 address
 * @param b Second NRF24 address
 * @return true if addresses are identical
 */
bool ESP32_RC_NRF24::isSameNrfAddr(const uint8_t *a, const uint8_t *b) {
    return memcmp(a, b, 5) == 0;
}

// ========== Address Formatting ==========

/**
 * @brief Format MAC address (6 bytes) for display (legacy compatibility)
 */
String ESP32_RC_NRF24::formatAddr(const uint8_t addr[RC_ADDR_SIZE]) {
    String out;
    for (int i = 0; i < RC_ADDR_SIZE; ++i) {
        char buf[3];
        sprintf(buf, "%02X", addr[i]);
        out += buf;
    }
    return out;
}

/**
 * @brief Format NRF24 address (5 bytes) for display
 * 
 * @param addr 5-byte NRF24 address
 * @return Formatted hex string (e.g., "D2AB1234CD")
 */
String ESP32_RC_NRF24::formatNrfAddr(const uint8_t addr[5]) {
    String out;
    for (int i = 0; i < 5; ++i) {
        char buf[3];
        sprintf(buf, "%02X", addr[i]);
        out += buf;
    }
    return out;
}

// ========== Handshake Protocol ==========

/**
 * @brief Send address handshake message to establish peer communication
 */
void ESP32_RC_NRF24::sendAddressHandshake() {
    // Handshake is implicit in NRF24 - heartbeat contains address
    // The address exchange happens when heartbeat is received
    LOG_DEBUG("NRF24 handshake via heartbeat message");
}

/**
 * @brief Handle handshake message from potential peer
 * 
 * @param msg Received heartbeat message containing peer address
 */
void ESP32_RC_NRF24::handleHandshakeMessage(const RCMessage_t& msg) {
    if (msg.type == RCMSG_TYPE_HEARTBEAT) {
        // Extract peer address and complete handshake
        setPeerAddr((const uint8_t*)msg.from_addr);
        handshake_completed_ = true;
        switchToPeerPipe();
        
        LOG("NRF24 handshake completed with peer: %s", 
            formatAddr(msg.from_addr).c_str());
    }
}

// ========== Configuration Interface ==========

/**
 * @brief Configure NRF24 protocol-specific parameters
 * 
 * @param key Configuration parameter name
 * @param value Configuration parameter value
 * @return true if successfully configured
 */
bool ESP32_RC_NRF24::setProtocolConfig(const char* key, const char* value) {
    if (!key || !value) return false;
    
    if (strcmp(key, "channel") == 0) {
        int channel = atoi(value);
        if (channel >= 0 && channel <= 125) {
            radio_.setChannel(channel);
            LOG_DEBUG("NRF24 channel set to %d", channel);
            return true;
        }
    }
    else if (strcmp(key, "power") == 0) {
        if (strcmp(value, "MIN") == 0) {
            radio_.setPALevel(RF24_PA_MIN);
            return true;
        } else if (strcmp(value, "LOW") == 0) {
            radio_.setPALevel(RF24_PA_LOW);
            return true;
        } else if (strcmp(value, "HIGH") == 0) {
            radio_.setPALevel(RF24_PA_HIGH);
            return true;
        } else if (strcmp(value, "MAX") == 0) {
            radio_.setPALevel(RF24_PA_MAX);
            return true;
        }
    }
    else if (strcmp(key, "datarate") == 0) {
        if (strcmp(value, "250K") == 0) {
            radio_.setDataRate(RF24_250KBPS);
            return true;
        } else if (strcmp(value, "1M") == 0) {
            radio_.setDataRate(RF24_1MBPS);
            return true;
        } else if (strcmp(value, "2M") == 0) {
            radio_.setDataRate(RF24_2MBPS);
            return true;
        }
    }
    
    return false;  // Unsupported configuration
}

/**
 * @brief Get current NRF24 protocol configuration
 * 
 * @param key Configuration parameter to query
 * @param value Buffer to store result
 * @param len Buffer length
 * @return true if parameter found
 */
bool ESP32_RC_NRF24::getProtocolConfig(const char* key, char* value, size_t len) {
    if (!key || !value || len == 0) return false;
    
    if (strcmp(key, "protocol") == 0) {
        strncpy(value, "NRF24", len - 1);
        value[len - 1] = '\0';
        return true;
    }
    else if (strcmp(key, "channel") == 0) {
        snprintf(value, len, "%d", radio_.getChannel());
        return true;
    }
    else if (strcmp(key, "datarate") == 0) {
        rf24_datarate_e rate = radio_.getDataRate();
        const char* rateStr = "UNKNOWN";
        switch (rate) {
            case RF24_250KBPS: rateStr = "250K"; break;
            case RF24_1MBPS: rateStr = "1M"; break;
            case RF24_2MBPS: rateStr = "2M"; break;
        }
        strncpy(value, rateStr, len - 1);
        value[len - 1] = '\0';
        return true;
    }
    
    return false;  // Unsupported query
}