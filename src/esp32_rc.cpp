#include "esp32_rc.h"

// Static member initialization
int ESP32RemoteControl::metrics_line_count_ = 0;

// Global metrics configuration (default enabled)
bool rc_metrics_enabled = true;

/**
 * @brief Constructor for ESP32RemoteControl base class
 * 
 * Initializes the core remote control framework including FreeRTOS primitives,
 * message queues, heartbeat timer, and background tasks. This constructor sets up
 * the foundation for all protocol-specific implementations.
 * 
 * @param fast_mode Operating mode selection:
 *   - false (default): Reliable mode with queue depth of 10 messages
 *   - true: Low-latency mode with queue depth of 1 (may drop messages)
 * 
 * Initialization sequence:
 * 1. Creates recursive mutex for thread-safe access
 * 2. Sets up send/receive message queues (size depends on fast_mode)
 * 3. Creates heartbeat timer (100ms interval by default)
 * 4. Starts background task for queue processing
 * 5. Initializes connection state and peer addressing
 * 
 * Example usage:
 *   ESP32RemoteControl* controller = new ESP32_RC_ESPNOW(false);  // Reliable
 *   ESP32RemoteControl* controller = new ESP32_RC_WIFI(true);     // Low-latency
 */
ESP32RemoteControl::ESP32RemoteControl(bool fast_mode) : fast_mode_(fast_mode) {

  LOG("Initializing...");
  conn_state_ = RCConnectionState_t::DISCONNECTED;
  memset(peer_addr_, 0, RC_ADDR_SIZE);
  
  // Initialize generic addresses (simplified)
  memset(peer_address_, 0, RC_ADDR_SIZE);
  memset(my_address_, 0, RC_ADDR_SIZE);



  // fast mode is false, then queue lenth is 10, otherwise 1
  data_lock_ = xSemaphoreCreateRecursiveMutex();
  if (!data_lock_) {
    LOG_ERROR("Failed to create data lock semaphore!");
    SYS_HALT;
  }
  
  // Create receive queue with fixed length of 10 messages
  // This queue is used to receive messages from the protocol layer
  queue_send_ = xQueueCreate(fast_mode_ ? 1 : QUEUE_DEPTH_SEND, sizeof(RCMessage_t));
  queue_recv_ = xQueueCreate(fast_mode_ ? 1 : QUEUE_DEPTH_RECV, sizeof(RCMessage_t));


  // Check if queues were created successfully
  if (!queue_send_ || !queue_recv_) {
    LOG_ERROR("Failed to create message queues!");
    SYS_HALT;
  }

  // set heartbeat timer
  
  if (!timer_heartbeat_) {
    timer_heartbeat_ = xTimerCreate(
        "HeartbeatTimer", pdMS_TO_TICKS(heartbeat_interval_ms_),
        pdTRUE,  // auto-reload
        this,    // timer ID (optional, for static->member mapping)
        &ESP32RemoteControl::heartbeatTimerCallback  // static function
    );
  }
  LOG("Heartbeat Timer created.");
    

  // start the sendFromQueueTask
  
  xTaskCreatePinnedToCore(
      ESP32RemoteControl::sendFromQueueLoop,  // Static task wrapper
      "SendTask", 4096,
      this,  // Pass the instance
      3, 
      &sendFromQueueTaskHandle_, 
      APP_CPU_NUM
  );

  LOG("SendFromQueueTask created.");

  if (sendFromQueueTaskHandle_ == nullptr) {
    LOG_ERROR("Failed to create SendFromQueueTask");
    SYS_HALT;
  } else {
    xTaskNotifyGive(sendFromQueueTaskHandle_); // Notify the task to start immediately
  }
  LOG("Initialization complete.");
}

/**
 * @brief Destructor for ESP32RemoteControl base class
 * 
 * Cleanly shuts down all FreeRTOS resources including timers, semaphores,
 * and message queues. Ensures no resource leaks when controller is destroyed.
 * 
 * Cleanup sequence:
 * 1. Stops and deletes heartbeat timer
 * 2. Deletes recursive mutex
 * 3. Deletes send and receive message queues
 * 4. Background task cleanup handled by FreeRTOS
 */
ESP32RemoteControl::~ESP32RemoteControl() {
  if (timer_heartbeat_) {
    xTimerStop(timer_heartbeat_, 0);
    xTimerDelete(timer_heartbeat_, 0);
    timer_heartbeat_ = nullptr;
  }
  if (data_lock_) {
    vSemaphoreDelete(data_lock_);
    data_lock_ = nullptr;
  }
  if (queue_send_) {
    vQueueDelete(queue_send_);
    queue_send_ = nullptr;
  }
  if (queue_recv_) {
    vQueueDelete(queue_recv_);
    queue_recv_ = nullptr;
  }
}

/**
 * @brief Initiate connection process for the remote control protocol
 * 
 * Starts the heartbeat mechanism and sets connection state to CONNECTING.
 * This method should be called after controller initialization to begin
 * communication attempts.
 * 
 * Connection flow:
 * 1. Starts heartbeat timer (begins sending periodic heartbeat messages)
 * 2. Sets state to CONNECTING
 * 3. Protocol-specific connection logic handled in derived classes
 * 4. State transitions to CONNECTED when peer responds
 * 
 * Example usage:
 *   controller->connect();  // Start attempting to connect
 *   while (controller->getConnectionState() != RCConnectionState_t::CONNECTED) {
 *     delay(100);  // Wait for connection establishment
 *   }
 */
void ESP32RemoteControl::connect() {
  // Start the heartbeat timer
  if (timer_heartbeat_ && !xTimerIsTimerActive(timer_heartbeat_)) {
    xTimerStart(timer_heartbeat_, 0);
    LOG("Heartbeat Timer started");
  }

  // Set the connection state to CONNECTING
  conn_state_ = RCConnectionState_t::CONNECTING;
  LOG("Starting connection process...");
}


/**
 * @brief Set custom callback function for received messages
 * 
 * Registers a user-defined function to be called whenever a valid data message
 * is received and successfully queued. Useful for immediate message processing
 * or custom logging.
 * 
 * @param cb Function pointer to callback with signature: void func(const RCMessage_t& msg)
 *           Pass nullptr to disable callback
 * 
 * Example usage:
 *   void myMessageHandler(const RCMessage_t& msg) {
 *     Serial.println("Received message!");
 *   }
 *   controller->setOnRecieveMsgHandler(myMessageHandler);
 * 
 * Note: Callback executes in protocol context - keep processing minimal
 */
void ESP32RemoteControl::setOnRecieveMsgHandler(recv_cb_t cb) {
  // Set the custom callback for received messages - FINAL implementation
  recv_callback_ = cb;
}

/**
 * @brief Set callback function for peer discovery events
 * 
 * Registers a callback function that will be called whenever a peer device is discovered.
 * This is useful for automatic pairing, connection management, or UI updates.
 * 
 * @param cb Function pointer to discovery callback, or nullptr to disable
 * 
 * Callback signature: void callback(const RCDiscoveryResult_t& result)
 * 
 * Example usage:
 *   void onPeerFound(const RCDiscoveryResult_t& result) {
 *     Serial.printf("Found peer: %s\n", result.info);
 *   }
 *   controller->setOnDiscoveryHandler(onPeerFound);
 * 
 * Note: Callback executes in protocol context - keep processing minimal
 */
void ESP32RemoteControl::setOnDiscoveryHandler(discovery_cb_t cb) {
  // Set discovery callback - FINAL implementation
  discovery_callback_ = cb;
}

/**
 * @brief Get current connection state
 * 
 * @return Current connection state:
 *   - DISCONNECTED: No peer communication
 *   - CONNECTING: Attempting to establish connection
 *   - CONNECTED: Active peer communication
 *   - ERROR: Connection error occurred
 * 
 * Example usage:
 *   if (controller->getConnectionState() == RCConnectionState_t::CONNECTED) {
 *     // Safe to send data
 *   }
 */
RCConnectionState_t ESP32RemoteControl::getConnectionState() const {
  // Get current connection state - FINAL implementation
  return conn_state_;
}

/**
 * @brief Process received messages with heartbeat and data handling
 * 
 * Central message processing hub that handles both heartbeat responses and
 * data messages. Any received message resets the heartbeat timeout, treating
 * data messages as implicit heartbeat responses for efficiency.
 * 
 * @param msg Received and validated message structure
 * 
 * Processing flow:
 * 1. Updates heartbeat timestamp (any message = alive peer)
 * 2. Establishes connection if not already connected
 * 3. Queues data messages for user consumption
 * 4. Calls user callback if registered
 * 5. Updates receive metrics
 * 
 * Message routing:
 * - HEARTBEAT: Only updates connection state (no queuing)
 * - DATA: Queued for recvData() and triggers callback
 * 
 * Thread safety: Uses recursive mutex for connection state updates
 */
void ESP32RemoteControl::onDataReceived(const RCMessage_t& msg) {
  LOG_DEBUG("Received message of type: %d", msg.type);
  
  // Update heartbeat timestamp for any received message (treat data messages as heartbeat responses)
  if (xSemaphoreTakeRecursive(data_lock_, pdMS_TO_TICKS(5)) == pdTRUE) {
    if (conn_state_ != RCConnectionState_t::CONNECTED) {
      // If we didn't have a peer before, set the peer address to the sender's address
      setPeerAddr((const uint8_t*)msg.from_addr);
      conn_state_ = RCConnectionState_t::CONNECTED;
      LOG("Peer set and connected!");
    } 
    // Reset heartbeat timer for any received message
    last_heartbeat_rx_ms_ = millis();
    xSemaphoreGive(data_lock_);
  }
  
  switch (msg.type) {
    case RCMSG_TYPE_HEARTBEAT:
      // Heartbeat messages are now handled above, no additional processing needed
      // Do not count heartbeats in receive metrics
      break;
    default:
      // Process data messages normally
      BaseType_t ok;
      if (fast_mode_) {
        // Assumes queue_recv_ length == 1
        ok = xQueueOverwrite(queue_recv_, &msg);  // Non-blocking overwrite
      } else {
        // Attempt to send non-blocking
        ok = xQueueSend(queue_recv_, &msg, 0);
        if (ok != pdTRUE) {
          RCMessage_t dropMsg;
          // Drop oldest message (non-blocking) and retry
          if (xQueueReceive(queue_recv_, &dropMsg, 0) == pdTRUE) {
            ok = xQueueSend(queue_recv_, &msg, 0);
          }
        }

        if (ok != pdTRUE) {
          LOG_ERROR("Failed to enqueue message");
          recv_metrics_.addFailure();
        }
      }

      // If we successfully added the message, call the custom callback
      // handler and update metrics
      if (ok == pdTRUE) {
        if (recv_callback_) recv_callback_(msg);
        recv_metrics_.addSuccess();  // Track successful message reception
      } else {
        recv_metrics_.addFailure();  // Track failed message reception
      }
      break;
  }
}

/**
 * @brief Handle peer discovery event
 * 
 * Called by protocol implementations when a peer device is discovered.
 * Updates the discovery result state and triggers user callback if registered.
 * 
 * @param addr Discovered peer address
 * @param info Optional protocol-specific information (e.g., IP address, device name)
 * 
 * Thread safety: Uses recursive mutex for state updates
 */
void ESP32RemoteControl::onPeerDiscovered(const RCAddress_t& addr, const char* info) {
  if (xSemaphoreTakeRecursive(data_lock_, pdMS_TO_TICKS(5)) == pdTRUE) {
    // Update discovery result (simplified)
    discovery_result_.discovered = true;
    memcpy(discovery_result_.peer_addr, addr, RC_ADDR_SIZE);
    
    xSemaphoreGiveRecursive(data_lock_);
    
    // Trigger callback if registered (outside lock to avoid deadlock)
    if (discovery_callback_) {
      discovery_callback_(discovery_result_);
    }
    
    LOG_INFO("[Discovery] Peer discovered");
  } else {
    LOG_ERROR("[Discovery] Failed to acquire lock for discovery update");
  }
}

/**
 * @brief Legacy heartbeat handler (deprecated)
 * 
 * This method is kept for backward compatibility but no longer performs
 * any operations. Heartbeat logic has been moved to onDataReceived() to
 * treat any message as a heartbeat response, improving efficiency.
 * 
 * @param msg Heartbeat message (ignored)
 * 
 * @deprecated Heartbeat processing now integrated into onDataReceived()
 */
void ESP32RemoteControl::onHeartbeatReceived(const RCMessage_t& msg) {
  // Heartbeat logic is now handled in onDataReceived() for all message types
  // This method is kept for compatibility but does nothing
}

/**
 * @brief Monitor heartbeat timeout and update connection state
 * 
 * Called periodically by heartbeat timer to check if peer communication
 * has timed out. If no messages received within timeout period, connection
 * state is set to DISCONNECTED.
 * 
 * Timeout behavior:
 * - Default timeout: 300ms (configurable via HEARTBEAT_TIMEOUT_MS)
 * - ANY received message resets timeout counter
 * - Only affects CONNECTED state (ignores CONNECTING/DISCONNECTED)
 * 
 * Called automatically by:
 * - Heartbeat timer callback (every 100ms by default)
 * - Should not be called directly by user code
 */
void ESP32RemoteControl::checkHeartbeat() {
  // Check if we received a heartbeat one time in the last
  // heartbeat_timeout_ms_ If not, we assume the connection is lost and set
  // the state to DISCONNECTED.
  if ((millis() - last_heartbeat_rx_ms_) > heartbeat_timeout_ms_) {
    if (conn_state_ == RCConnectionState_t::CONNECTED) {
      conn_state_ = RCConnectionState_t::DISCONNECTED;
      LOG("Connection lost! No heartbeat received in timeout period.");
    }
  }
}

/**
 * @brief Set peer address for direct communication (legacy 6-byte interface)
 * 
 * Stores the peer's address for targeted communication. This is the legacy
 * interface for 6-byte MAC addresses. Updates both legacy and generic addresses.
 * 
 * @param peer_addr Pointer to 6-byte address (typically MAC address)
 *                  Must be valid pointer to RC_ADDR_SIZE bytes
 * 
 * Usage notes:
 * - Called automatically when peer is discovered via heartbeat
 * - Can be called manually to set known peer address
 * - Protocol implementations may add validation and peer registration
 * - Also updates generic address structure for consistency
 */
void ESP32RemoteControl::setPeerAddr(const uint8_t* peer_addr) {
  // Check if peer_addr is valid
  if (peer_addr) {
    memcpy(peer_addr_, peer_addr, RC_ADDR_SIZE);
    // Also update generic address
    memcpy(peer_address_, peer_addr, RC_ADDR_SIZE);
  }
}

/**
 * @brief Set peer address for direct communication (generic interface)
 * 
 * Modern interface supporting variable-length addresses for any protocol.
 * Updates both generic and legacy address storage for compatibility.
 * 
 * @param peer_addr Generic address structure with protocol-specific data
 * 
 * Supported address types:
 * - MAC addresses (6 bytes): ESPNOW, WiFi
 * - BLE UUIDs (16 bytes): BLE GATT characteristics
 * - NRF24 addresses (1-5 bytes): NRF24L01 protocol
 * 
 * Example usage:
 *   // MAC address
 *   RCAddress_t addr = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
 *   controller->setPeerAddr(addr);
 */
void ESP32RemoteControl::setPeerAddr(const RCAddress_t& peer_addr) {
  // Copy address data (simplified)
  memcpy(peer_address_, peer_addr, RC_ADDR_SIZE);
  memcpy(peer_addr_, peer_addr, RC_ADDR_SIZE);
}

/**
 * @brief Clear peer address and return to discovery mode
 * 
 * Clears stored peer address, typically causing protocol to return to
 * broadcast/discovery mode for communication. Clears both legacy and generic addresses.
 * 
 * Called automatically when:
 * - Connection timeout occurs
 * - Protocol-specific disconnection
 * - Manual disconnection requested
 */
void ESP32RemoteControl::unsetPeerAddr() {
  memset(peer_addr_, 0, RC_ADDR_SIZE);
  memset(peer_address_, 0, RC_ADDR_SIZE);
}

/**
 * @brief Create protocol-specific broadcast address
 * 
 * Default implementation creates a 6-byte broadcast address (all 0xFF).
 * Protocol implementations should override for protocol-specific broadcast.
 * 
 * @return Generic address structure with broadcast address
 * 
 * Protocol-specific examples:
 * - ESPNOW/WiFi: FF:FF:FF:FF:FF:FF (6 bytes)
 * - BLE: Service-specific UUID (16 bytes)
 * - NRF24: 0xFF (1-5 bytes depending on configuration)
 */
void ESP32RemoteControl::createBroadcastAddress(RCAddress_t& broadcast_addr) const {
  static const uint8_t broadcast[RC_ADDR_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  memcpy(broadcast_addr, broadcast, RC_ADDR_SIZE);
}

// --- Heartbeat timer ISR ---
/**
 * @brief Static timer callback for heartbeat management
 * 
 * FreeRTOS timer callback that executes periodically to maintain connection.
 * Sends heartbeat messages and monitors for peer timeouts.
 * 
 * @param xTimer FreeRTOS timer handle (contains instance pointer)
 * 
 * Operations performed:
 * 1. Sends heartbeat message to peer
 * 2. Checks for peer communication timeout
 * 3. Updates connection state if timeout detected
 * 
 * Timing:
 * - Called every 100ms by default (HEARTBEAT_INTERVAL_MS)
 * - Timeout threshold: 300ms by default (HEARTBEAT_TIMEOUT_MS)
 * 
 * Note: This is a FreeRTOS callback - not called directly by user code
 */
void ESP32RemoteControl::heartbeatTimerCallback(TimerHandle_t xTimer) {
  ESP32RemoteControl* self = static_cast<ESP32RemoteControl*>(pvTimerGetTimerID(xTimer));
  if (self) {
    self->sendSysMsg(RCMSG_TYPE_HEARTBEAT);
    // check heartbeat status
    self->checkHeartbeat();
  }
}

/**
 * @brief Send system control message (heartbeat, etc.)
 * 
 * Creates and sends protocol control messages like heartbeats.
 * These messages maintain connection state but don't contain user data.
 * 
 * @param msgType Type of system message:
 *   - RCMSG_TYPE_HEARTBEAT: Connection keep-alive
 *   - Future: RCMSG_TYPE_ACK, RCMSG_TYPE_NACK, etc.
 * 
 * Message structure:
 * - Type: Specified message type
 * - From address: This device's address
 * - Payload: Zeroed (no data for system messages)
 * 
 * Called automatically by:
 * - Heartbeat timer for periodic keep-alives
 * - Protocol implementations for control signaling
 */
void ESP32RemoteControl::sendSysMsg(const uint8_t msgType) {
  // Prepare system message with type and sender MAC
  RCMessage_t sysMsg = {};
  sysMsg.type = msgType;
  memcpy(sysMsg.from_addr, my_addr_, RC_ADDR_SIZE);
  memset(sysMsg.payload, 0, sizeof(sysMsg.payload));
  sendMsg(sysMsg);
}

/**
 * @brief Queue message for transmission
 * 
 * Adds message to send queue and notifies background task for transmission.
 * Behavior depends on fast_mode setting for different use cases.
 * 
 * @param msg Complete message structure to send
 * @return true if successfully queued, false on queue full/error
 * 
 * Operating modes:
 * - Normal mode (fast_mode=false): Queue up to 10 messages, blocks when full
 * - Fast mode (fast_mode=true): Single message queue, overwrites previous
 * 
 * Example usage:
 *   RCMessage_t msg = {};
 *   msg.type = RCMSG_TYPE_DATA;
 *   msg.setPayload(myData);
 *   if (!controller->sendMsg(msg)) {
 *     Serial.println("Send queue full!");
 *   }
 * 
 * Thread safety: Queue operations are thread-safe via FreeRTOS
 */
bool ESP32RemoteControl::sendMsg(const RCMessage_t& msg) {
  // Send message through queue - FINAL implementation
  if (fast_mode_) {
    // Fast mode: overwrite the queue with the new message
    BaseType_t ok = xQueueOverwrite(queue_send_, &msg);
    if (ok != pdTRUE) {
      LOG_ERROR("Failed to overwrite send queue");
      return false;
    }
  } else {
    // Normal mode: send to the queue
    BaseType_t ok = xQueueSend(queue_send_, &msg, 0);
    if (ok != pdTRUE) {
      LOG_ERROR("Failed to enqueue message for sending");
      return false;
    }
  }
  // Notify the task to send the message immediately
  xTaskNotifyGive(sendFromQueueTaskHandle_);
  return true;
}

/**
 * @brief Receive queued message from protocol layer
 * 
 * Retrieves the next available message from the receive queue.
 * Blocks briefly waiting for messages to arrive.
 * 
 * @param msg Reference to message structure to fill
 * @return true if message received, false if timeout/no messages
 * 
 * Timeout behavior:
 * - Waits up to 5ms for message arrival (RECV_MSG_TIMEOUT_MS)
 * - Returns immediately if message already queued
 * - Non-blocking after timeout expires
 * 
 * Example usage:
 *   RCMessage_t msg;
 *   if (controller->recvMsg(msg)) {
 *     Serial.printf("Received type %d\n", msg.type);
 *   }
 * 
 * Queue behavior:
 * - Normal mode: Up to 10 messages queued (oldest dropped if full)
 * - Fast mode: Single message (latest overwrites previous)
 */
bool ESP32RemoteControl::recvMsg(RCMessage_t& msg) {
  // Receive message from queue - FINAL implementation
  // Attempt to receive a message from the queue
  BaseType_t ok = xQueueReceive(queue_recv_, &msg, pdMS_TO_TICKS(RECV_MSG_TIMEOUT_MS));
  if (ok == pdTRUE) {
    // Successfully received a message
    return true;
  } else {
    // No message available
    return false;
  } 
}  // recieve message

/**
 * @brief Send user data payload (simplified interface)
 * 
 * High-level interface for sending user data without dealing with
 * message structure details. Automatically handles message formatting.
 * 
 * @param payload Data structure containing user information (25 bytes)
 * @return true if successfully queued for transmission
 * 
 * Convenience wrapper that:
 * 1. Creates RCMessage_t structure
 * 2. Sets type to RCMSG_TYPE_DATA
 * 3. Adds sender address
 * 4. Copies payload data
 * 5. Queues for transmission
 * 
 * Example usage:
 *   RCPayload_t data = {};
 *   data.value1 = 123.45f;
 *   data.id1 = 42;
 *   controller->sendData(data);
 * 
 * This is the preferred method for sending user data.
 */
bool ESP32RemoteControl::sendData(const RCPayload_t& payload) {
  // Send data payload - FINAL implementation
  RCMessage_t msg = {};
  msg.type = RCMSG_TYPE_DATA;  // Or any appropriate type for "data" messages
  memcpy(msg.from_addr, my_addr_, RC_ADDR_SIZE);
  msg.setPayload(payload);     // Copy payload into message
  return sendMsg(msg);
}

/**
 * @brief Receive user data payload (simplified interface)
 * 
 * High-level interface for receiving user data without dealing with
 * message structure details. Filters out system messages automatically.
 * 
 * @param payload Reference to data structure to fill (25 bytes)
 * @return true if data message received, false if no data available
 * 
 * Message filtering:
 * - Only returns RCMSG_TYPE_DATA messages
 * - Ignores heartbeat and other system messages
 * - Extracts payload portion from message structure
 * 
 * Example usage:
 *   RCPayload_t data;
 *   if (controller->recvData(data)) {
 *     Serial.printf("Received: %f\n", data.value1);
 *   }
 * 
 * Timeout: Brief wait (5ms) for message arrival, then returns false
 * 
 * This is the preferred method for receiving user data.
 */
bool ESP32RemoteControl::recvData(RCPayload_t& payload) {
  // Receive data payload - FINAL implementation
  RCMessage_t msg = {};

  // Try to receive a message (could be blocking or non-blocking)
  if (!recvMsg(msg)) {
    return false;  // No message received
  }

  // Optional: check if the message is the correct type
  if (msg.type != RCMSG_TYPE_DATA) {
    return false;  // Not a data message
  }
  // Extract the payload from the message
  memcpy(&payload, msg.getPayload(), sizeof(RCPayload_t));
  return true;
}

/**
 * @brief Background task for processing send queue
 * 
 * FreeRTOS task that runs continuously to process outgoing messages.
 * Handles the actual transmission via protocol-specific lowLevelSend().
 * 
 * @param arg Pointer to ESP32RemoteControl instance (cast from void*)
 * 
 * Task behavior:
 * 1. Blocks until notified of queued messages
 * 2. Processes all available messages in queue
 * 3. Calls protocol-specific lowLevelSend() for each message
 * 4. Updates transmission metrics
 * 5. Returns to blocking state until next notification
 * 
 * Performance optimization:
 * - Batch processes multiple messages when available
 * - Only wakes when messages are actually queued
 * - Runs on APP_CPU_NUM core for optimal performance
 * 
 * Priority: 3 (configurable)
 * Stack size: 4096 bytes
 * 
 * Note: This is a FreeRTOS task - not called directly by user code
 */
void ESP32RemoteControl::sendFromQueueLoop(void* arg) {
  auto* self = static_cast<ESP32RemoteControl*>(arg);  // Cast arg to class instance

  while (true) {
    // Block until notified at least once
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    do {
      RCMessage_t msg;
      while (xQueueReceive(self->queue_send_, &msg, 0) == pdTRUE) {
        LOG_DEBUG("Sending message of type %d", msg.type);
        self->lowLevelSend(msg);
        // Note: Actual success/failure tracking done in protocol-specific lowLevelSend()
        // Update send metrics
       }
    } while (ulTaskNotifyTake(pdTRUE, 0) > 0);
  }
}

/**
 * @brief Print comprehensive metrics for protocol analysis
 * 
 * Displays real-time communication statistics including success rates,
 * connection state, and performance metrics. Designed for protocol
 * comparison and system health monitoring.
 * 
 * @param forceHeader If true, always print header regardless of line count
 * 
 * Output format:
 * - Time: Seconds since startup
 * - Protocol: ESPNOW/WIFI/BLE/NRF24
 * - Conn: Connection state (CONN/DISC/CONN?/ERR)  
 * - Send: Successful/Failed/Success rate %/Transactions per second
 * - Recv: Successful/Failed/Success rate %/Transactions per second
 * - Total: Cumulative sent/received counts
 * 
 * Example output:
 *   Time(s) | Protocol | Conn | Send(OK/Fail/Rate/TPS) | Recv(OK/Fail/Rate/TPS) | Total(Sent/Recv)
 *        45 |  ESPNOW  | CONN | 42/ 3/ 93%/12.3 | 38/ 0/100%/11.2 |   45/  38
 * 
 * When metrics are globally disabled, shows warning message every 5 seconds:
 *   ⚠️  METRICS DISABLED - Use ESP32RemoteControl::enableGlobalMetrics(true) to enable
 */
void ESP32RemoteControl::printMetrics(bool forceHeader) {
  unsigned long now = millis();
  
  // Check if metrics display is enabled and interval has elapsed
  if (!forceHeader && metrics_display_enabled_) {
    if (now - last_metrics_print_ms_ < metrics_interval_ms_) {
      return;  // Not time to print yet
    }
    last_metrics_print_ms_ = now;
  }
  
  // Check if global metrics are disabled
  if (!rc_metrics_enabled) {
    // Print warning message periodically when metrics are disabled
    static uint32_t last_warning_ms = 0;
    if (now - last_warning_ms >= 5000) {  // Show warning every 5 seconds
      last_warning_ms = now;
      LOG("⚠️  METRICS DISABLED - Use ESP32RemoteControl::enableGlobalMetrics(true) to enable");
    }
    return;
  }
  
  // Print header every 20 lines for readability or when forced
  if (forceHeader || metrics_line_count_ % 20 == 0) {
    LOG("=== Protocol Communication Metrics ===");
    LOG("Time(s) | Protocol | Conn | Send(OK/Fail/Rate/TPS) | Recv(OK/Fail/Rate/TPS) | Total(Sent/Recv)");
    LOG("--------|----------|------|------------------------|------------------------|------------------");
    metrics_line_count_ = 0;
  }
  
  // Get protocol name
  const char* protocolName = "UNKNOWN";
  switch (getProtocol()) {
    case RC_PROTO_ESPNOW: protocolName = "ESPNOW"; break;
    case RC_PROTO_WIFI: protocolName = "WIFI"; break;
    case RC_PROTO_BLE: protocolName = "BLE"; break;
    case RC_PROTO_NRF24: protocolName = "NRF24"; break;
  }
  
  // Connection state string
  const char* connState = "DISC";
  switch (conn_state_) {
    case RCConnectionState_t::CONNECTED: connState = "CONN"; break;
    case RCConnectionState_t::CONNECTING: connState = "CONN?"; break;
    case RCConnectionState_t::DISCONNECTED: connState = "DISC"; break;
    case RCConnectionState_t::ERROR: connState = "ERR"; break;
  }
  
  // Calculate transmission rates
  float sendRate = send_metrics_.getTransactionRate();
  float recvRate = recv_metrics_.getTransactionRate();
  
  // Print metrics with transmission rates
  LOG("%7lu | %8s | %4s | %3u/%3u/%3.0f%%/%4.1f | %3u/%3u/%3.0f%%/%4.1f | %4u/%4u",
      now / 1000,                           // Time in seconds
      protocolName,                         // Protocol name
      connState,                            // Connection state
      send_metrics_.successful,             // Send successful
      send_metrics_.failed,                 // Send failed  
      send_metrics_.getSuccessRate(),       // Send success rate %
      sendRate,                             // Send rate (TPS)
      recv_metrics_.successful,             // Receive successful
      recv_metrics_.failed,                 // Receive failed
      recv_metrics_.getSuccessRate(),       // Receive success rate %
      recvRate,                             // Receive rate (TPS)
      send_metrics_.getTotal(),             // Total sent
      recv_metrics_.getTotal()              // Total received
  );
  
  metrics_line_count_++;
}

/**
 * @brief Enable automatic metrics display with specified interval
 * 
 * Enables periodic metrics printing for continuous monitoring.
 * Call this method once to start automatic metrics display,
 * then call printMetrics() regularly in your main loop.
 * 
 * @param enable True to enable automatic display, false to disable
 * @param interval_ms Display interval in milliseconds (default: 1000ms)
 * 
 * Usage example:
 *   controller->enableMetricsDisplay(true, 2000);  // Print every 2 seconds
 *   // In main loop:
 *   controller->printMetrics();  // Will print based on interval
 */
void ESP32RemoteControl::enableMetricsDisplay(bool enable, uint32_t interval_ms) {
  metrics_display_enabled_ = enable;
  metrics_interval_ms_ = interval_ms;
  if (enable) {
    last_metrics_print_ms_ = millis();  // Reset timer
    LOG("Metrics display enabled (interval: %lu ms, protocol: %d)", interval_ms, getProtocol());
  } else {
    LOG("Metrics display disabled");
  }
}

/**
 * @brief Enable or disable global metrics calculation
 * 
 * Controls whether metrics are calculated across all controller instances.
 * When disabled, all metrics operations become no-ops for performance.
 * 
 * @param enable True to enable metrics calculation, false to disable
 * 
 * Usage example:
 *   ESP32RemoteControl::enableGlobalMetrics(true);   // Enable metrics
 *   ESP32RemoteControl::disableGlobalMetrics();      // Disable metrics
 * 
 * Note: This affects all controller instances globally
 */
void ESP32RemoteControl::enableGlobalMetrics(bool enable) {
  rc_metrics_enabled = enable;
  LOG("Global metrics calculation %s", enable ? "ENABLED" : "DISABLED");
}
