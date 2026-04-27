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
  void connect() override;
  
  // Configuration interface implementation
  bool setProtocolConfig(const char* key, const char* value) override;
  bool getProtocolConfig(const char* key, char* value, size_t len) override;
  
  // Address handling overrides
  uint8_t getAddressSize() const override { return RC_ADDR_SIZE; }  // ESP-NOW uses MAC addresses
  void createBroadcastAddress(RCAddress_t& broadcast_addr) const override;
 

 protected:
  // adding ESPNOW specific paring steps
  void checkHeartbeat() override;
  void sendSysMsg(const uint8_t msgType) override;
  void lowLevelSend(const RCMessage_t& msg) override;
  void setPeerAddr(const uint8_t* peer_addr) override;
  // Using base class setPeerAddr implementation
  void unsetPeerAddr() override;
  RCMessage_t parseRawData(const uint8_t* data, size_t len) override;  // Unified interface from base class

 private:
  struct HelloPayload {
    uint8_t version;
    uint8_t current_channel;
    uint8_t flags;
    uint8_t priority;
    uint8_t device_id;
    uint8_t reserved[20];
  };
  static_assert(sizeof(HelloPayload) == RC_PAYLOAD_MAX_SIZE, "HelloPayload must fit RC payload");

  static constexpr uint8_t kHelloVersion = 1;
  static constexpr uint8_t kHelloFlagChannelLocked = 0x01;
  static constexpr uint8_t kMinEspnowChannel = 1;
  static constexpr uint8_t kMaxEspnowChannel = 13;

  bool init();
  bool applyChannel(uint8_t channel);
  void determineInitialChannelState();
  bool ensurePeerRegistered(const uint8_t* peer_addr);
  bool ensureBroadcastPeerRegistered();
  void handleHelloMessage(const uint8_t* mac, const RCMessage_t& msg);
  void processPendingNegotiation();
  void completeNegotiationWithPeer(const uint8_t* peer_mac, uint8_t agreed_channel);
  uint8_t chooseNegotiatedChannel(const HelloPayload& peer_hello, const uint8_t* peer_mac, bool& impossible) const;
  uint8_t getCurrentChannel() const;
  uint8_t calculatePriority() const;
  void advanceDiscoveryChannel();
  RCMessage_t makeHelloMessage() const;
  String formatAddr(const uint8_t addr[RC_ADDR_SIZE]) const;

  uint8_t preferred_channel_ = ESPNOW_CHANNEL;
  uint8_t current_channel_ = ESPNOW_CHANNEL;
  uint8_t negotiated_channel_ = 0;
  uint8_t discovery_hop_step_ = 1;
  uint8_t node_priority_ = 0;
  uint8_t device_id_ = 0;
  uint32_t negotiation_started_ms_ = 0;
  uint8_t pending_negotiation_channel_ = 0;
  uint8_t pending_peer_mac_[RC_ADDR_SIZE] = {0};
  bool channel_locked_ = false;
  bool negotiation_impossible_ = false;
  bool awaiting_link_confirmation_ = false;
  bool pending_negotiation_ready_ = false;
  // ESPNOW callback glue (static --> internal member)
  static void onDataRecvStatic(const uint8_t* mac, const uint8_t* data, int len);
  static void onDataSentStatic(const uint8_t* mac, esp_now_send_status_t status);
  void onDataSentInternal(const uint8_t* mac, esp_now_send_status_t status);
  static ESP32_RC_ESPNOW* instance_;  // For static callback glue
};

// #endif // ENABLE_ESPNOW
