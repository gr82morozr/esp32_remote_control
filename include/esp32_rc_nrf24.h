#pragma once

#include <SPI.h>
#include <RF24.h>
#include "esp32_rc.h"
#include "esp32_rc_user_config.h"

class ESP32_RC_NRF24 : public ESP32RemoteControl {
 public:
  ESP32_RC_NRF24(bool fast_mode);
  ~ESP32_RC_NRF24() override;

  RCProtocol_t getProtocol() const override { return RC_PROTO_NRF24; }
  
  // Configuration interface implementation
  bool setProtocolConfig(const char* key, const char* value) override;
  bool getProtocolConfig(const char* key, char* value, size_t len) override;
  
  // Address handling overrides
  uint8_t getAddressSize() const override { return 5; }  // NRF24 uses 5-byte addresses
  RCAddress_t createBroadcastAddress() const override;

protected:
  void lowLevelSend(const RCMessage_t& msg) override;
  void setPeerAddr(const uint8_t* peer_addr) override;
  void setPeerAddr(const RCAddress_t& peer_addr) override;
  void unsetPeerAddr() override;
  RCMessage_t parseRawData(const uint8_t* data, size_t len) override;
  void checkHeartbeat() override;

private:
  static ESP32_RC_NRF24* instance_;  // For static callback glue
  int pipeType_         = -1;  // Current pipe type ; 0 : broadcast, 1 : peer
  SPIClass* spiBus_     = nullptr;  // SPI bus to use, default is VSPI
  RF24 radio_           = RF24(PIN_NRF_CE, PIN_NRF_CSN);
  TaskHandle_t receiveTaskHandle_ = nullptr;
  
  // NRF24 specific addresses (5 bytes)
  uint8_t nrf_my_addr_[5] = {0};      // My NRF24 address (5 bytes)
  uint8_t nrf_peer_addr_[5] = {0};    // Peer NRF24 address (5 bytes)
  const uint8_t NRF_BROADCAST_ADDR[5] = {0xF0, 0xF0, 0xF0, 0xF0, 0xAA};  // Broadcast address
  
  // Handshake state
  bool handshake_completed_ = false;


  bool init();
  static void receiveLoopWrapper(void* arg);
  void receiveLoop(void* arg);
  void switchToBroadcastPipe();
  void switchToPeerPipe();
  
  // Address conversion and management
  void macToNrfAddress(const uint8_t mac[6], uint8_t nrf_addr[5]);
  void nrfToMacAddress(const uint8_t nrf_addr[5], uint8_t mac[6]);
  bool isSameNrfAddr(const uint8_t *a, const uint8_t *b);
  void generateMyNrfAddress();
  String formatNrfAddr(const uint8_t addr[5]);
  String formatAddr(const uint8_t addr[RC_ADDR_SIZE]);  // Legacy MAC address formatter
  
  // Handshake protocol
  void sendAddressHandshake();
  void handleHandshakeMessage(const RCMessage_t& msg);

  
};
