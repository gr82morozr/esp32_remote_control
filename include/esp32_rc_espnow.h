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
  
  // Configuration interface implementation
  bool setProtocolConfig(const char* key, const char* value) override;
  bool getProtocolConfig(const char* key, char* value, size_t len) override;
  
  // Address handling overrides
  uint8_t getAddressSize() const override { return RC_ADDR_SIZE; }  // ESP-NOW uses MAC addresses
  void createBroadcastAddress(RCAddress_t& broadcast_addr) const override;
 

 protected:
  // adding ESPNOW specific paring steps
  void lowLevelSend(const RCMessage_t& msg) override;
  void setPeerAddr(const uint8_t* peer_addr) override;
  // Using base class setPeerAddr implementation
  void unsetPeerAddr() override;
  RCMessage_t parseRawData(const uint8_t* data, size_t len) override;  // Unified interface from base class

 private:
  // ESPNOW-specific state , to track sent message status
  static const int _ring_cap = 8;  // Must be power of 2
  struct BoolRing {
    volatile uint16_t head = 0, tail = 0;
    bool buf[_ring_cap];
    inline bool push(bool v) {
      uint16_t h = head, n = uint16_t((h + 1) % _ring_cap);
      if (n == tail) return false;      // full
      buf[h] = v;
      __sync_synchronize();             // publish before head moves
      head = n;
      return true;
    }
    inline bool pop(bool& out) {
      uint16_t t = tail;
      if (t == head) return false;      // empty
      __sync_synchronize();             // acquire
      out = buf[t];
      tail = uint16_t((t + 1) % _ring_cap);
      return true;
    }
  };
  BoolRing _ring;

  bool init();
  // ESPNOW callback glue (static --> internal member)
  static void onDataRecvStatic(const uint8_t* mac, const uint8_t* data, int len);
  static void onDataSentStatic(const uint8_t* mac, esp_now_send_status_t status);
  void onDataSentInternal(const uint8_t* mac, esp_now_send_status_t status);
  static ESP32_RC_ESPNOW* instance_;  // For static callback glue
};

// #endif // ENABLE_ESPNOW
