#include "esp32_rc_espnow.h"


ESP32_RC_ESPNOW* ESP32_RC_ESPNOW::instance_ = nullptr;

ESP32_RC_ESPNOW::ESP32_RC_ESPNOW(bool fast_mode)
    : ESP32RemoteControl(fast_mode) {
    // Initialize the ESPNOW instance
    LOG_DEBUG( "[ESP32_RC_ESPNOW] Initializing ESPNOW...");
    instance_ = this;
    if (!init()) {
        LOG_DEBUG("[ERROR] ESPNOW init failed!");
        SYS_HALT;   
    }
}

ESP32_RC_ESPNOW::~ESP32_RC_ESPNOW() {
    esp_now_deinit();
}

bool ESP32_RC_ESPNOW::init() {
    WiFi.mode(WIFI_STA);
      
    // Set WiFi channel for ESPNOW
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
   
    esp_wifi_set_max_tx_power(ESPNOW_OUTPUT_POWER);
   
    esp_err_t initResult = esp_now_init();
    LOG_DEBUG("esp_now_init: "); LOG_DEBUG(initResult);
    if (initResult != ESP_OK) {
        return false;
    }

    // Get my MAC address
    WiFi.macAddress(my_addr_);  // Get my MAC address

    // Always add broadcast peer so send always works
    esp_now_peer_info_t peerInfo = {};
    uint8_t bcast[RC_ADDR_SIZE] = RC_BROADCAST_MAC;
    memcpy(peerInfo.peer_addr, bcast, RC_ADDR_SIZE);
    peerInfo.channel = ESPNOW_CHANNEL;
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;
    if (!esp_now_is_peer_exist(bcast)) {
        esp_err_t addPeerRes = esp_now_add_peer(&peerInfo);
        LOG_DEBUG("esp_now_add_peer (broadcast): ");
        LOG_DEBUG(addPeerRes);
    }

    esp_now_register_recv_cb(ESP32_RC_ESPNOW::onDataRecvStatic);
    esp_now_register_send_cb(ESP32_RC_ESPNOW::onDataSentStatic);

    return true;
}



void ESP32_RC_ESPNOW::lowLevelSend(const RCMessage_t &msg) {
  if (conn_state_ == RCConnectionState_t::CONNECTED) {
    esp_err_t sendRes =  esp_now_send(peer_addr_, reinterpret_cast<const uint8_t *>(&msg), sizeof(RCMessage_t));
  } else {
    static uint8_t bcast[RC_ADDR_SIZE] = RC_BROADCAST_MAC;
    esp_err_t sendRes = esp_now_send(bcast, reinterpret_cast<const uint8_t *>(&msg), sizeof(RCMessage_t));
  }
}

void ESP32_RC_ESPNOW::setPeerAddr(const uint8_t *peer_addr) {
  // Set peer address and update connection state, 
  // ESPNOW specific pairing steps
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peer_addr, RC_ADDR_SIZE);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = 0;
  peerInfo.ifidx = WIFI_IF_STA;
  if (!esp_now_is_peer_exist(peer_addr)) {
    esp_err_t res = esp_now_add_peer(&peerInfo);
  }

  // Set the peer address in the base class
  ESP32RemoteControl::setPeerAddr(peer_addr);  // Call base class method
};

void ESP32_RC_ESPNOW::unsetPeerAddr() {
  // Remove peer from ESPNOW
  if (esp_now_is_peer_exist(peer_addr_)) {
    esp_err_t res = esp_now_del_peer(peer_addr_);
  }
  ESP32RemoteControl::unsetPeerAddr();  // Call base class method
}

// --- Static Callback Wrappers ---
void ESP32_RC_ESPNOW::onDataRecvStatic(const uint8_t *mac, const uint8_t *data, int len) {
  // Called when data is received
  LOG_DEBUG("ESPNOW: Data received");
  if (instance_) {
    RCMessage_t msg = {};
    msg = instance_->parseRawToRCMessage(data, len);

    // Overwrite from_addr with the actual sender MAC from ESPNOW
    memcpy(msg.from_addr, mac, RC_ADDR_SIZE);
    instance_->onDataReceived(msg);
  }
}

void ESP32_RC_ESPNOW::onDataSentStatic(const uint8_t *mac,  esp_now_send_status_t status) {
    // Called when data is sent
   if (instance_) instance_->onDataSentInternal(mac, status);
}

RCMessage_t ESP32_RC_ESPNOW::parseRawToRCMessage(const uint8_t *data, int len) {
  // Parse raw data into RCMessage_t structure
  RCMessage_t msg = {};
  if (len == sizeof(RCMessage_t)) {
    memcpy(&msg, data, sizeof(RCMessage_t));
  }
  return msg;
}



void ESP32_RC_ESPNOW::onDataSentInternal(const uint8_t *mac,   esp_now_send_status_t status) {
 
}

