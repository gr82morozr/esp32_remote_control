#pragma once

#include <RF24.h>
#include "esp32_rc.h"

class ESP32_RC_NRF24 : public ESP32RemoteControl {
 public:
  ESP32_RC_NRF24(bool fast_mode);
  ~ESP32_RC_NRF24() override;

  RCProtocol_t getProtocol() const override { return RC_PROTO_NRF24; }

protected:
  void lowLevelSend(const RCMessage_t& msg) override;
  void checkHeartbeat() override;

private:
  static ESP32_RC_NRF24* instance_;  // For static callback glue
  int pipeType_ = -1;  // Current pipe type ; 0 : broadcast, 1 : peer
  RF24 radio_ = RF24(PIN_NRF_CE, PIN_NRF_CSN);
  String formatAddr(const uint8_t addr[RC_ADDR_SIZE]);
  TaskHandle_t receiveTaskHandle_ = nullptr;
  const uint64_t BROADCAST_ADDR = 0xF0F0F0F0AALL;


  bool init();
  static void receiveLoopWrapper(void* arg);
  void receiveLoop(void* arg);
  void switchToBroadcastPipe();
  void switchToPeerPipe();
  bool isSameAddr(const uint8_t *a, const uint8_t *b);
  void genUniqueAddr(uint8_t out[RC_ADDR_SIZE]);

  
};
