#pragma once

#include <Arduino.h>
#include "esp32_rc_common.h"
#include <Common/Common.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/timers.h>



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
  using discovery_cb_t = void (*)(const RCDiscoveryResult_t& result);

  ESP32RemoteControl(bool fast_mode = false);
  virtual ~ESP32RemoteControl();

  // ========== STABLE PUBLIC API (LOCKED DOWN) ==========
  
  // Core protocol identification - MUST be implemented by protocols
  virtual RCProtocol_t getProtocol() const = 0;
  
  // Connection management - has default implementation, can be overridden
  virtual void connect();
  
  // Optional protocol-specific configuration interface
  virtual bool setProtocolConfig(const char* key, const char* value) { return false; }
  virtual bool getProtocolConfig(const char* key, char* value, size_t len) { return false; }

  // ========== STABLE USER API (DO NOT OVERRIDE) ==========
  
  // Callback management - stable implementation
  void setOnRecieveMsgHandler(recv_cb_t cb);
  void setOnDiscoveryHandler(discovery_cb_t cb);
  
  // Discovery status access - stable implementation
  RCDiscoveryResult_t getDiscoveryResult() const { return discovery_result_; }

  // High-level message interface - stable implementation
  bool sendMsg(const RCMessage_t& msg);       // Send a message
  bool recvMsg(RCMessage_t& msg);             // Receive message
  bool sendData(const RCPayload_t& payload);  // Send data payload
  bool recvData(RCPayload_t& payload);        // Receive data payload

  // Connection state access - stable implementation
  RCConnectionState_t getConnectionState() const;
  
  // Metrics access - stable implementation
  Metrics_t getSendMetrics() const { return send_metrics_; }
  Metrics_t getReceiveMetrics() const { return recv_metrics_; }
  void resetMetrics() { send_metrics_.reset(); recv_metrics_.reset(); }
  
  // Global metrics control
  static void enableGlobalMetrics(bool enable = true);
  static void disableGlobalMetrics() { enableGlobalMetrics(false); }
  static bool isGlobalMetricsEnabled() { return rc_metrics_enabled; }
  
  // Metrics display and analysis
  void printMetrics(bool forceHeader = false);
  void enableMetricsDisplay(bool enable = true, uint32_t interval_ms = 1000);
  void disableMetricsDisplay() { enableMetricsDisplay(false); }

 protected:
  // ========== PROTOCOL IMPLEMENTATION INTERFACE ==========
  
  // Called by protocol implementations when data arrives
  void onDataReceived(const RCMessage_t& msg);
  
  // PURE VIRTUAL - Must be implemented by all protocols
  virtual void lowLevelSend(const RCMessage_t& msg) = 0;  // Low-level protocol send
  virtual RCMessage_t parseRawData(const uint8_t* data, size_t len) = 0;  // Parse raw protocol data
  
  // VIRTUAL WITH DEFAULTS - Can be overridden if needed
  virtual void checkHeartbeat();  // Connection timeout checking
  virtual uint8_t getAddressSize() const { return RC_ADDR_SIZE; }  // Address size
  virtual void createBroadcastAddress(RCAddress_t& broadcast_addr) const;  // Broadcast address
  
  // ADDRESS MANAGEMENT - Can be overridden for protocol-specific handling
  virtual void setPeerAddr(const uint8_t* peer_addr);  // Legacy interface
  virtual void unsetPeerAddr();  // Clear peer address
  
  // ========== HELPER METHODS FOR PROTOCOLS ==========
  
  // Called by protocol implementations - stable implementation  
  void onPeerDiscovered(const RCAddress_t& addr, const char* info = nullptr);  // Peer discovery
  
  // System message sending - can be overridden if needed
  virtual void sendSysMsg(const uint8_t msgType);
  
  // Legacy heartbeat handler - deprecated but kept for compatibility
  virtual void onHeartbeatReceived(const RCMessage_t& msg);
  
  // Simplified address interface - stable implementation
  void setPeerAddr(const RCAddress_t& peer_addr);

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

  // Address handling - simplified
  uint8_t peer_addr_[RC_ADDR_SIZE] = {0};  // Legacy 6-byte peer address (for compatibility)
  uint8_t my_addr_[RC_ADDR_SIZE] = {0};    // Legacy 6-byte my address (for compatibility)
  
  RCAddress_t peer_address_;  // Simplified peer address
  RCAddress_t my_address_;    // Simplified my address

  // Metrics for send/receive operations
  Metrics_t send_metrics_{}, recv_metrics_{};
  
  // Metrics display control
  bool metrics_display_enabled_ = false;
  uint32_t metrics_interval_ms_ = 1000;
  uint32_t last_metrics_print_ms_ = 0;
  static int metrics_line_count_;  // Static for header management across instances

 private:
  // Internal method to handle received messages
  recv_cb_t recv_callback_ = nullptr;
  discovery_cb_t discovery_callback_ = nullptr;
  
  // Discovery state
  RCDiscoveryResult_t discovery_result_;

  // Task handle for the sendFromQueue loop
  TaskHandle_t sendFromQueueTaskHandle_ = nullptr;
  static void heartbeatTimerCallback(TimerHandle_t xTimer);
  static void sendFromQueueLoop(void* arg);
};