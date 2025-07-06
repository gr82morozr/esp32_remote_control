#include "esp32_rc.h"

ESP32RemoteControl::ESP32RemoteControl(bool fast_mode) : fast_mode_(fast_mode) {

  LOG("Initializing...");
  conn_state_ = RCConnectionState_t::DISCONNECTED;
  memset(peer_addr_, 0, RC_ADDR_SIZE);



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
      &sendFromQueueTaskHandle, 
      APP_CPU_NUM
  );

  LOG("SendFromQueueTask created.");

  if (sendFromQueueTaskHandle == nullptr) {
    LOG_ERROR("Failed to create SendFromQueueTask");
    SYS_HALT;
  } else {
    xTaskNotifyGive(sendFromQueueTaskHandle); // Notify the task to start immediately
  }
  LOG("Initialization complete.");
}

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


void ESP32RemoteControl::setOnRecieveMsgHandler(recv_cb_t cb) {
  // Set the custom callback for received messages
  recv_callback_ = cb;
}

RCConnectionState_t ESP32RemoteControl::getConnectionState() const {
  return conn_state_;
}

void ESP32RemoteControl::onDataReceived(const RCMessage_t& msg) {
  LOG("Received message of type: %d", msg.type);
  switch (msg.type) {
    case RCMSG_TYPE_HEARTBEAT:
      onHeartbeatReceived(msg);
      break;
    default:
      setPeerAddr(msg.from_addr);
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
          LOG("[ERROR] Failed to enqueue message");
          recv_metrics_.err++;
        }

        // If we successfully added the message, call the custom callback
        // handler
        if (recv_callback_) recv_callback_(msg);
          ++recv_metrics_.in;
        }
      break;
  }
}

void ESP32RemoteControl::onHeartbeatReceived(const RCMessage_t& msg) {
  if (xSemaphoreTakeRecursive(data_lock_, pdMS_TO_TICKS(5)) == pdTRUE) {
    if (conn_state_ != RCConnectionState_t::CONNECTED) {
      // If we didn't have a peer before, set the peer address to the sender's
      // address
      //memcpy(peer_addr_, msg.from_addr, RC_ADDR_SIZE);
      setPeerAddr(msg.from_addr);
      conn_state_ = RCConnectionState_t::CONNECTED;
      LOG("Peer set and connected!");
    } 

    // reset heartbeat timer
    last_heartbeat_rx_ms_ = millis();
    xSemaphoreGive(data_lock_);
  }
}

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

void ESP32RemoteControl::setPeerAddr(const uint8_t* peer_addr) {
  // Check if peer_addr is valid
  memcpy(peer_addr_, peer_addr, RC_ADDR_SIZE);
}

void ESP32RemoteControl::unsetPeerAddr() {
  memset(peer_addr_, 0, RC_ADDR_SIZE);
}

// --- Heartbeat timer ISR ---
void ESP32RemoteControl::heartbeatTimerCallback(TimerHandle_t xTimer) {
  ESP32RemoteControl* self = static_cast<ESP32RemoteControl*>(pvTimerGetTimerID(xTimer));
  if (self) {
    toggleGPIO(LED_BUILTIN);  // Toggle LED for heartbeat indication
    self->sendSysMsg(RCMSG_TYPE_HEARTBEAT);
    // check heartbeat status
    self->checkHeartbeat();
  }
}

void ESP32RemoteControl::sendSysMsg(const uint8_t msgType) {
  // Prepare system message with type and sender MAC
  RCMessage_t sysMsg = {};
  sysMsg.type = msgType;
  memcpy(sysMsg.from_addr, my_addr_, RC_ADDR_SIZE);
  memset(sysMsg.payload, 0, sizeof(sysMsg.payload));
  sendMsg(sysMsg);
}

bool ESP32RemoteControl::sendMsg(const RCMessage_t& msg) {
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
  xTaskNotifyGive(sendFromQueueTaskHandle);
  return true;
}


// Send a message
bool ESP32RemoteControl::recvMsg(RCMessage_t& msg) {
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

bool ESP32RemoteControl::sendData(const RCPayload_t& payload) {
  RCMessage_t msg = {};
  msg.type = RCMSG_TYPE_DATA;  // Or any appropriate type for "data" messages
  memcpy(msg.from_addr, my_addr_, RC_ADDR_SIZE);
  msg.setPayload(payload);     // Copy payload into message
  return sendMsg(msg);
}

bool ESP32RemoteControl::recvData(RCPayload_t& payload) {
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

void ESP32RemoteControl::sendFromQueueLoop(void* arg) {
  auto* self = static_cast<ESP32RemoteControl*>(arg);  // Cast arg to class instance

  while (true) {
    // Block until notified at least once
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    do {
      RCMessage_t msg;
      while (xQueueReceive(self->queue_send_, &msg, 0) == pdTRUE) {
        LOG("Sending message of type %d", msg.type);
        self->lowLevelSend(msg);
        self->send_metrics_.out++;
      }
    } while (ulTaskNotifyTake(pdTRUE, 0) > 0);
  }
}
