#pragma once

#include <Arduino.h>
#include <Common/Common.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/timers.h>

#include "esp32_rc_common.h"

// ESP32RemoteControl class
// This is the base class for all ESP32 remote control protocols.
// It provides the common interface and logic for handling messages,
// heartbeats, and connection states.
// Protocol-specific implementations should inherit from this class
// and implement the pure virtual methods for sending messages,
// connecting, and handling protocol-specific logic.

// fast_mode_ :     is used to enable faster message sync processing (
//                  queue depth is 1, also message sent immediately, no garantee
//                  of delivery) For sending - this is useful for low-latency
//                  applications For receiving - this is useful for
//                  high-frequency data streams, only recieve the latest message

class ESP32RemoteControl {
 public:
  using recv_cb_t = void (*)(const RCMessage_t& msg);

  ESP32RemoteControl(bool fast_mode = false);
  virtual ~ESP32RemoteControl();

  // Pure virtuals for protocol implementations
  virtual void connect();
  virtual RCProtocol_t getProtocol() const = 0;

  // User/upper-layer interface
  void setOnRecieveMsgHandler(recv_cb_t cb);

  virtual bool sendMsg(const RCMessage_t& msg);       // Send a message
  virtual bool recvMsg(RCMessage_t& msg);             // recieve message
  virtual bool sendData(const RCPayload_t& payload);  // Send a message
  virtual bool recvData(RCPayload_t& payload);        // recieve message

  RCConnectionState_t getConnectionState() const;

 protected:
  // Protocol logic helpers
  void onDataReceived(const RCMessage_t& msg);

  // Heartbeat logic
  virtual void checkHeartbeat();
  // Called when a heartbeat message is received
  virtual void onHeartbeatReceived(const RCMessage_t& msg);

  // Low-level send methods
  virtual void sendSysMsg(const uint8_t msgType);
  // Low-level send method to be implemented by protocol
  virtual void lowLevelSend(const RCMessage_t& msg) = 0;

  // Parse raw data into RCMessage_t structure, to be implemented by protocol
  RCMessage_t parseRawToRCMessage();

  // Set or unset the peer address for communication
  virtual void setPeerAddr(const uint8_t* peer_addr);
  // Unset the peer address, typically called on disconnect
  virtual void unsetPeerAddr();

  // Default connection state
  RCConnectionState_t conn_state_ = RCConnectionState_t::DISCONNECTED;

  // default mode
  bool fast_mode_ = false;

  SemaphoreHandle_t data_lock_ = nullptr;
  // Queues for sending messages
  QueueHandle_t queue_send_ = nullptr;
  // Queues for receiving messages
  QueueHandle_t queue_recv_ = nullptr;

  // Heartbeat timer
  TimerHandle_t timer_heartbeat_ = nullptr;

  // Heartbeat parameters
  const uint32_t heartbeat_interval_ms_ = HEARTBEAT_INTERVAL_MS;
  const uint32_t heartbeat_timeout_ms_ = HEARTBEAT_TIMEOUT_MS;
  uint32_t last_heartbeat_rx_ms_ = 0;

  // Peer address
  uint8_t peer_addr_[RC_ADDR_SIZE] = {0};
  // My address )
  uint8_t my_addr_[RC_ADDR_SIZE] = {0};

  // Metrics for send/receive operations
  Metrics_t send_metrics_{}, recv_metrics_{};

 private:
  // Internal method to handle received messages
  recv_cb_t recv_callback_ = nullptr;

  // Task handle for the sendFromQueue loop
  TaskHandle_t sendFromQueueTaskHandle = nullptr;
  static void heartbeatTimerCallback(TimerHandle_t xTimer);
  static void sendFromQueueLoop(void* arg);
};