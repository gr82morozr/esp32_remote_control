#pragma once

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <time.h> 
#include "esp32_rc.h"

class ESP32_RC_WIFI : public ESP32RemoteControl {
 public:
  ESP32_RC_WIFI(bool fast_mode);
  ~ESP32_RC_WIFI() override;
  void connect() override;

  RCProtocol_t getProtocol() const override { return RC_PROTO_WIFI; }

 protected:
  void lowLevelSend(const RCMessage_t& msg) override;


 private:
  char rc_ssid_[32];
  WiFiServer tcp_server_;
  WiFiClient tcp_client_;

  bool is_ap_ = false;
  bool connected_ = false;
   
    
  bool init();
  // Auto-negotiate role (controller or peer)
  void autoNegotiateRole();

  static ESP32_RC_WIFI* instance_;  // For static callback glue

  void waitForAPConnection();
  void waitForSTAConnection();
  void receiveLoop(void* arg);
  static void receiveLoopWrapper(void* arg);
  TaskHandle_t receiveTaskHandle_ = nullptr;
};