#pragma once
// #if ENABLE_ESPNOW

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "esp32_rc.h"

class ESP32_RC_ESPNOW : public ESP32RemoteControl {
 public:
  ESP32_RC_ESPNOW(bool fast_mode);
  ~ESP32_RC_ESPNOW() override;

  RCProtocol_t getProtocol() const override { return RC_PROTO_ESPNOW; }
 

 protected:
  // adding ESPNOW specific paring steps
  void lowLevelSend(const RCMessage_t& msg) override;
  void setPeerAddr(const uint8_t* peer_addr) override;
  void unsetPeerAddr() override;
  RCMessage_t parseRawToRCMessage(const uint8_t* data,  int len);  // overloaded from base class

 private:
  bool init();
  // ESPNOW callback glue (static --> internal member)
  static void onDataRecvStatic(const uint8_t* mac, const uint8_t* data, int len);
  static void onDataSentStatic(const uint8_t* mac, esp_now_send_status_t status);
  void onDataSentInternal(const uint8_t* mac, esp_now_send_status_t status);
  static ESP32_RC_ESPNOW* instance_;  // For static callback glue
};

// #endif // ENABLE_ESPNOW
