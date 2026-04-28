#include "esp32_rc_espnow.h"

ESP32_RC_ESPNOW* ESP32_RC_ESPNOW::instance_ = nullptr;

ESP32_RC_ESPNOW::ESP32_RC_ESPNOW(bool fast_mode)
    : ESP32RemoteControl(fast_mode) {
  LOG_INFO("[ESP32_RC_ESPNOW] Initializing ESPNOW...");
  instance_ = this;
  if (!init()) {
    LOG_ERROR("ESPNOW init failed!");
    if (instance_ == this) {
      instance_ = nullptr;
    }
    cleanupResources();
    SYS_HALT;
  }
}

ESP32_RC_ESPNOW::~ESP32_RC_ESPNOW() {
  esp_now_unregister_recv_cb();
  esp_now_unregister_send_cb();
  esp_now_deinit();
  if (instance_ == this) {
    instance_ = nullptr;
  }
}

void ESP32_RC_ESPNOW::connect() {
  determineInitialChannelState();
  negotiated_channel_ = 0;
  negotiation_started_ms_ = 0;
  pending_negotiation_channel_ = 0;
  memset(pending_peer_mac_, 0, sizeof(pending_peer_mac_));
  negotiation_impossible_ = false;
  awaiting_link_confirmation_ = false;
  pending_negotiation_ready_ = false;
  ensureBroadcastPeerRegistered();
  ESP32RemoteControl::connect();
}

bool ESP32_RC_ESPNOW::init() {
  WiFi.mode(WIFI_STA);

  esp_wifi_set_max_tx_power(ESPNOW_OUTPUT_POWER);

  esp_err_t initResult = esp_now_init();
  LOG_DEBUG("esp_now_init: %s", esp_err_to_name(initResult));
  if (initResult != ESP_OK) {
    return false;
  }

  WiFi.macAddress(my_addr_);
  memcpy(my_address_, my_addr_, RC_ADDR_SIZE);

  node_priority_ = calculatePriority();
  device_id_ = my_addr_[5];
  discovery_hop_step_ = static_cast<uint8_t>(((node_priority_ % 6) * 2) + 1);
  determineInitialChannelState();

  if (!ensureBroadcastPeerRegistered()) {
    return false;
  }

  esp_now_register_recv_cb(ESP32_RC_ESPNOW::onDataRecvStatic);
  esp_now_register_send_cb(ESP32_RC_ESPNOW::onDataSentStatic);

  LOG_INFO("ESP-NOW ready on channel %u (locked=%s, hop_step=%u)",
           current_channel_, channel_locked_ ? "yes" : "no", discovery_hop_step_);
  return true;
}

bool ESP32_RC_ESPNOW::applyChannel(uint8_t channel) {
  if (channel < kMinEspnowChannel || channel > kMaxEspnowChannel) {
    LOG_ERROR("Invalid ESP-NOW channel: %u", channel);
    return false;
  }

  const uint8_t current = getCurrentChannel();
  if (channel_locked_) {
    if (current != 0 && current != channel) {
      LOG_ERROR("Cannot switch from locked WiFi channel %u to %u", current, channel);
      return false;
    }
    current_channel_ = current != 0 ? current : channel;
    return true;
  }

  esp_wifi_set_promiscuous(true);
  const esp_err_t result = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  if (result != ESP_OK) {
    LOG_ERROR("esp_wifi_set_channel(%u) failed: %s", channel, esp_err_to_name(result));
    return false;
  }

  current_channel_ = channel;
  return true;
}

void ESP32_RC_ESPNOW::determineInitialChannelState() {
  const bool was_locked = channel_locked_;
  const uint8_t previous_channel = current_channel_;
  refreshChannelStateFromWiFi();

  if (channel_locked_) {
    if (!was_locked || previous_channel != current_channel_) {
      LOG_INFO("ESP-NOW channel locked to AP channel %u", current_channel_);
    }
    return;
  }

  if (preferred_channel_ < kMinEspnowChannel || preferred_channel_ > kMaxEspnowChannel) {
    preferred_channel_ = ESPNOW_CHANNEL;
  }

  if (!applyChannel(preferred_channel_)) {
    current_channel_ = preferred_channel_;
  }
  LOG_INFO("ESP-NOW discovery starts on channel %u", current_channel_);
}

void ESP32_RC_ESPNOW::refreshChannelStateFromWiFi() {
  const wl_status_t wifi_status = WiFi.status();
  const uint8_t live_channel = getCurrentChannel();

  if (wifi_status == WL_CONNECTED &&
      live_channel >= kMinEspnowChannel &&
      live_channel <= kMaxEspnowChannel) {
    channel_locked_ = true;
    current_channel_ = live_channel;
    preferred_channel_ = live_channel;
    return;
  }

  channel_locked_ = false;
}

bool ESP32_RC_ESPNOW::ensurePeerRegistered(const uint8_t* peer_addr) {
  if (!peer_addr) {
    return false;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peer_addr, RC_ADDR_SIZE);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  esp_err_t res = ESP_OK;
  if (esp_now_is_peer_exist(peer_addr)) {
    res = esp_now_mod_peer(&peerInfo);
  } else {
    res = esp_now_add_peer(&peerInfo);
  }

  if (res != ESP_OK) {
    LOG_ERROR("Failed to register ESP-NOW peer %s: %s",
              formatAddr(peer_addr).c_str(), esp_err_to_name(res));
    return false;
  }

  return true;
}

bool ESP32_RC_ESPNOW::ensureBroadcastPeerRegistered() {
  static const uint8_t bcast[RC_ADDR_SIZE] = RC_BROADCAST_MAC;
  return ensurePeerRegistered(bcast);
}

uint8_t ESP32_RC_ESPNOW::getCurrentChannel() const {
  uint8_t primary = 0;
  wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
  if (esp_wifi_get_channel(&primary, &secondary) == ESP_OK) {
    return primary;
  }
  return 0;
}

uint8_t ESP32_RC_ESPNOW::calculatePriority() const {
  uint32_t priority = 0;
  for (int i = 0; i < RC_ADDR_SIZE; ++i) {
    priority += my_addr_[i];
  }
  return static_cast<uint8_t>(priority % 256);
}

void ESP32_RC_ESPNOW::advanceDiscoveryChannel() {
  if (channel_locked_) {
    return;
  }

  uint8_t next_channel = current_channel_;
  if (next_channel < kMinEspnowChannel || next_channel > kMaxEspnowChannel) {
    next_channel = preferred_channel_;
  }

  const uint8_t channel_count = kMaxEspnowChannel - kMinEspnowChannel + 1;
  next_channel = static_cast<uint8_t>(
      (((next_channel - kMinEspnowChannel) + discovery_hop_step_) % channel_count) +
      kMinEspnowChannel);

  if (applyChannel(next_channel)) {
    LOG_DEBUG("ESP-NOW discovery hop -> channel %u", current_channel_);
  }
}

RCMessage_t ESP32_RC_ESPNOW::makeHelloMessage() const {
  RCMessage_t msg = {};
  msg.type = RCMSG_TYPE_HELLO;
  memcpy(msg.from_addr, my_addr_, RC_ADDR_SIZE);

  HelloPayload hello = {};
  hello.version = kHelloVersion;
  hello.current_channel = current_channel_;
  hello.flags = channel_locked_ ? kHelloFlagChannelLocked : 0;
  hello.priority = node_priority_;
  hello.device_id = device_id_;
  memcpy(msg.payload, &hello, sizeof(hello));

  return msg;
}

String ESP32_RC_ESPNOW::formatAddr(const uint8_t addr[RC_ADDR_SIZE]) const {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1],
           addr[2], addr[3], addr[4], addr[5]);
  return String(buf);
}

void ESP32_RC_ESPNOW::checkHeartbeat() {
  refreshChannelStateFromWiFi();

  if (conn_state_ == RCConnectionState_t::CONNECTED) {
    awaiting_link_confirmation_ = false;

    if ((millis() - last_heartbeat_rx_ms_) > heartbeat_timeout_ms_) {
      LOG_INFO("ESP-NOW peer timed out, restarting discovery");
      unsetPeerAddr();
      determineInitialChannelState();
      conn_state_ = RCConnectionState_t::CONNECTING;
    }
    return;
  }

  if (!awaiting_link_confirmation_) {
    return;
  }

  if ((millis() - negotiation_started_ms_) <= heartbeat_timeout_ms_) {
    return;
  }

  LOG_INFO("ESP-NOW link confirmation timed out, returning to discovery");
  unsetPeerAddr();
  determineInitialChannelState();
  conn_state_ = RCConnectionState_t::CONNECTING;
}

void ESP32_RC_ESPNOW::sendSysMsg(const uint8_t msgType) {
  refreshChannelStateFromWiFi();

  if (msgType != RCMSG_TYPE_HEARTBEAT) {
    ESP32RemoteControl::sendSysMsg(msgType);
    return;
  }

  if (conn_state_ == RCConnectionState_t::CONNECTED ||
      awaiting_link_confirmation_) {
    ESP32RemoteControl::sendSysMsg(msgType);
    return;
  }

  if (negotiation_impossible_) {
    return;
  }

  if (!ensureBroadcastPeerRegistered()) {
    return;
  }

  RCMessage_t hello = makeHelloMessage();
  sendMsg(hello);
}

void ESP32_RC_ESPNOW::processPendingNegotiation() {
  if (!pending_negotiation_ready_) {
    return;
  }

  uint8_t peer_mac[RC_ADDR_SIZE] = {0};
  memcpy(peer_mac, pending_peer_mac_, RC_ADDR_SIZE);
  const uint8_t agreed_channel = pending_negotiation_channel_;

  pending_negotiation_ready_ = false;
  pending_negotiation_channel_ = 0;
  memset(pending_peer_mac_, 0, sizeof(pending_peer_mac_));

  if (agreed_channel < kMinEspnowChannel || agreed_channel > kMaxEspnowChannel) {
    LOG_ERROR("Discarding pending negotiation with invalid channel %u", agreed_channel);
    return;
  }

  completeNegotiationWithPeer(peer_mac, agreed_channel);
}

void ESP32_RC_ESPNOW::lowLevelSend(const RCMessage_t& msg) {
  processPendingNegotiation();

  esp_err_t sendResult = ESP_FAIL;
  uint8_t* target_addr;
  static uint8_t bcast[RC_ADDR_SIZE] = RC_BROADCAST_MAC;
  const bool is_system_message =
      (msg.type == RCMSG_TYPE_HEARTBEAT || msg.type == RCMSG_TYPE_HELLO);

  // HELLO is only valid during discovery. Once a peer/channel has been negotiated,
  // stale queued HELLO frames must not keep hopping channels or interfere with the
  // first unicast heartbeat exchange.
  if (msg.type == RCMSG_TYPE_HELLO &&
      (awaiting_link_confirmation_ ||
       conn_state_ == RCConnectionState_t::CONNECTED)) {
    LOG_DEBUG("Dropping stale HELLO after negotiation");
    return;
  }

  if (conn_state_ == RCConnectionState_t::CONNECTED ||
      awaiting_link_confirmation_) {
    target_addr = peer_addr_;
  } else {
    target_addr = bcast;
  }

  for (int retry = 0; retry <= MAX_SEND_RETRIES; retry++) {
    sendResult = esp_now_send(
        target_addr, reinterpret_cast<const uint8_t*>(&msg), sizeof(RCMessage_t));

    if (sendResult == ESP_OK) {
      if (retry > 0) {
        LOG_DEBUG("ESP-NOW send succeeded on retry %d", retry);
      }
      break;
    }

    if (sendResult != ESP_ERR_ESPNOW_NO_MEM || retry >= MAX_SEND_RETRIES) {
      break;
    }

    LOG_DEBUG("ESP-NOW send queue full (attempt %d/%d), retrying after %d ms",
              retry + 1, MAX_SEND_RETRIES + 1, RETRY_DELAY_MS);
    DELAY(pdMS_TO_TICKS(RETRY_DELAY_MS));
  }

  if (is_system_message) {
    if (sendResult != ESP_OK && sendResult != ESP_ERR_ESPNOW_NO_MEM) {
      LOG_ERROR("ESP-NOW system message type %u failed: %s",
                msg.type, esp_err_to_name(sendResult));
    } else if (sendResult == ESP_ERR_ESPNOW_NO_MEM) {
      LOG_DEBUG("ESP-NOW system message type %u skipped after queue stayed full",
                msg.type);
    }
    if (msg.type == RCMSG_TYPE_HELLO &&
        !awaiting_link_confirmation_ &&
        conn_state_ != RCConnectionState_t::CONNECTED) {
      advanceDiscoveryChannel();
    }
    return;
  }

  if (sendResult == ESP_OK) {
    send_metrics_.addSuccess();
    LOG_DEBUG("ESP-NOW accepted message type %u for transmit", msg.type);
    return;
  }

  if (sendResult == ESP_ERR_ESPNOW_NO_MEM) {
    LOG_DEBUG("ESP-NOW data send queue stayed full after %d attempts",
              MAX_SEND_RETRIES + 1);
  } else {
    LOG_ERROR("ESP-NOW send failed after %d attempts: %s",
              MAX_SEND_RETRIES + 1, esp_err_to_name(sendResult));
  }
  send_metrics_.addFailure();
}

void ESP32_RC_ESPNOW::setPeerAddr(const uint8_t* peer_addr) {
  if (!peer_addr) {
    LOG_ERROR("Invalid peer address: null pointer");
    return;
  }

  uint8_t null_mac[RC_ADDR_SIZE] = {0};
  if (memcmp(peer_addr, null_mac, RC_ADDR_SIZE) == 0) {
    LOG_ERROR("Invalid peer address: null MAC");
    return;
  }

  if (!ensurePeerRegistered(peer_addr)) {
    return;
  }

  ESP32RemoteControl::setPeerAddr(peer_addr);
}

void ESP32_RC_ESPNOW::unsetPeerAddr() {
  if (esp_now_is_peer_exist(peer_addr_)) {
    esp_err_t res = esp_now_del_peer(peer_addr_);
    if (res != ESP_OK) {
      LOG_ERROR("Failed to remove ESP-NOW peer: %s", esp_err_to_name(res));
    } else {
      LOG_DEBUG("ESP-NOW peer removed successfully");
    }
  }

  negotiated_channel_ = 0;
  negotiation_started_ms_ = 0;
  pending_negotiation_channel_ = 0;
  memset(pending_peer_mac_, 0, sizeof(pending_peer_mac_));
  awaiting_link_confirmation_ = false;
  pending_negotiation_ready_ = false;
  negotiation_impossible_ = false;
  if (!channel_locked_) {
    preferred_channel_ = current_channel_;
  }

  ESP32RemoteControl::unsetPeerAddr();
}

void ESP32_RC_ESPNOW::handleHelloMessage(const uint8_t* mac, const RCMessage_t& msg) {
  refreshChannelStateFromWiFi();

  if (!mac) {
    return;
  }

  if (memcmp(mac, my_addr_, RC_ADDR_SIZE) == 0) {
    return;
  }

  if ((conn_state_ == RCConnectionState_t::CONNECTED ||
       awaiting_link_confirmation_) &&
      memcmp(peer_addr_, mac, RC_ADDR_SIZE) != 0) {
    LOG_DEBUG("Ignoring HELLO from unexpected peer %s while linked to %s",
              formatAddr(mac).c_str(), formatAddr(peer_addr_).c_str());
    return;
  }

  HelloPayload hello = {};
  memcpy(&hello, msg.payload, sizeof(hello));
  if (hello.version != kHelloVersion) {
    LOG_DEBUG("Ignoring HELLO with unsupported version %u", hello.version);
    return;
  }

  bool impossible = false;
  const uint8_t agreed_channel = chooseNegotiatedChannel(hello, mac, impossible);
  if (impossible) {
    negotiation_impossible_ = false;
    conn_state_ = RCConnectionState_t::CONNECTING;
    LOG_INFO(
        "ESP-NOW cannot pair yet: both nodes are WiFi-locked on different channels "
        "(mine=%u, peer=%u). Waiting for channel conditions to change.",
        current_channel_, hello.current_channel);
    return;
  }

  if (agreed_channel < kMinEspnowChannel || agreed_channel > kMaxEspnowChannel) {
    LOG_DEBUG("HELLO from %s did not produce a usable channel", formatAddr(mac).c_str());
    return;
  }

  memcpy(pending_peer_mac_, mac, RC_ADDR_SIZE);
  pending_negotiation_channel_ = agreed_channel;
  pending_negotiation_ready_ = true;
}

void ESP32_RC_ESPNOW::completeNegotiationWithPeer(const uint8_t* peer_mac,
                                                  uint8_t agreed_channel) {
  if ((conn_state_ == RCConnectionState_t::CONNECTED ||
       awaiting_link_confirmation_) &&
      memcmp(peer_addr_, peer_mac, RC_ADDR_SIZE) == 0 &&
      negotiated_channel_ == agreed_channel) {
    return;
  }

  if (!applyChannel(agreed_channel)) {
    LOG_ERROR("Failed to switch to negotiated ESP-NOW channel %u", agreed_channel);
    return;
  }

  if (!ensureBroadcastPeerRegistered()) {
    return;
  }

  if (!ensurePeerRegistered(peer_mac)) {
    return;
  }

  negotiation_impossible_ = false;
  preferred_channel_ = agreed_channel;
  negotiated_channel_ = agreed_channel;
  negotiation_started_ms_ = millis();
  awaiting_link_confirmation_ = true;

  RCAddress_t peer_addr = {};
  memcpy(peer_addr, peer_mac, RC_ADDR_SIZE);
  onPeerDiscovered(peer_addr, formatAddr(peer_mac).c_str());

  setPeerAddr(peer_mac);
  conn_state_ = RCConnectionState_t::CONNECTING;

  LOG_INFO("ESP-NOW negotiated peer %s on channel %u, awaiting heartbeat",
           formatAddr(peer_mac).c_str(), agreed_channel);
}

uint8_t ESP32_RC_ESPNOW::chooseNegotiatedChannel(const HelloPayload& peer_hello,
                                                 const uint8_t* peer_mac,
                                                 bool& impossible) const {
  impossible = false;
  const bool peer_locked = (peer_hello.flags & kHelloFlagChannelLocked) != 0;
  const uint8_t peer_channel = peer_hello.current_channel;

  if (peer_channel < kMinEspnowChannel || peer_channel > kMaxEspnowChannel) {
    return 0;
  }

  if (channel_locked_ && peer_locked) {
    if (current_channel_ != peer_channel) {
      impossible = true;
      return 0;
    }
    return current_channel_;
  }

  if (channel_locked_) {
    return current_channel_;
  }

  if (peer_locked) {
    return peer_channel;
  }

  if (preferred_channel_ >= kMinEspnowChannel && preferred_channel_ <= kMaxEspnowChannel &&
      preferred_channel_ == peer_channel) {
    return preferred_channel_;
  }

  const int mac_cmp = memcmp(my_addr_, peer_mac, RC_ADDR_SIZE);
  if (mac_cmp < 0) {
    return current_channel_;
  }
  if (mac_cmp > 0) {
    return peer_channel;
  }

  return current_channel_ < peer_channel ? current_channel_ : peer_channel;
}

void ESP32_RC_ESPNOW::onDataRecvStatic(const uint8_t* mac, const uint8_t* data, int len) {
  if (!instance_) {
    return;
  }

  if (!mac || !data) {
    LOG_ERROR("ESP-NOW receive callback received null data");
    return;
  }

  if (len != sizeof(RCMessage_t)) {
    LOG_ERROR("Invalid ESP-NOW message size: expected %d, got %d", sizeof(RCMessage_t),
              len);
    return;
  }

  const uint8_t msg_type = data[0];
  if (msg_type != RCMSG_TYPE_DATA && msg_type != RCMSG_TYPE_HEARTBEAT &&
      msg_type != RCMSG_TYPE_HELLO) {
    LOG_ERROR("Invalid ESP-NOW message type: %u", msg_type);
    return;
  }

  RCMessage_t msg = instance_->parseRawData(data, len);
  memcpy(msg.from_addr, mac, RC_ADDR_SIZE);

  if (msg.type == RCMSG_TYPE_HELLO) {
    instance_->handleHelloMessage(mac, msg);
    return;
  }

  const bool is_expected_pending_peer =
      instance_->awaiting_link_confirmation_ &&
      memcmp(mac, instance_->peer_addr_, RC_ADDR_SIZE) == 0;

  if (msg.type == RCMSG_TYPE_DATA &&
      instance_->conn_state_ != RCConnectionState_t::CONNECTED &&
      !is_expected_pending_peer) {
    LOG_DEBUG("Ignoring ESP-NOW data packet before HELLO negotiation completes");
    return;
  }

  if (is_expected_pending_peer) {
    instance_->awaiting_link_confirmation_ = false;
  }

  instance_->onDataReceived(msg);
}

void ESP32_RC_ESPNOW::onDataSentStatic(const uint8_t* mac,
                                       esp_now_send_status_t status) {
  if (instance_) {
    instance_->onDataSentInternal(mac, status);
  }
}

RCMessage_t ESP32_RC_ESPNOW::parseRawData(const uint8_t* data, size_t len) {
  RCMessage_t msg = {};

  if (!data) {
    LOG_ERROR("Invalid data: null pointer");
    return msg;
  }

  if (len != sizeof(RCMessage_t)) {
    LOG_ERROR("Invalid message size: expected %d, got %d", sizeof(RCMessage_t), len);
    return msg;
  }

  if (len > RC_MESSAGE_MAX_SIZE) {
    LOG_ERROR("Message too large: %d bytes (max: %d)", len, RC_MESSAGE_MAX_SIZE);
    return msg;
  }

  memcpy(&msg, data, sizeof(RCMessage_t));

  if (msg.type != RCMSG_TYPE_DATA && msg.type != RCMSG_TYPE_HEARTBEAT &&
      msg.type != RCMSG_TYPE_HELLO) {
    LOG_ERROR("Invalid message type: %u", msg.type);
    memset(&msg, 0, sizeof(RCMessage_t));
  }

  return msg;
}

void ESP32_RC_ESPNOW::onDataSentInternal(const uint8_t* mac,
                                         esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    LOG_DEBUG("ESP-NOW message delivered successfully");
  } else {
    LOG_DEBUG("ESP-NOW send delivery failed to peer %s",
              mac ? formatAddr(mac).c_str() : "<null>");
  }
}

bool ESP32_RC_ESPNOW::setProtocolConfig(const char* key, const char* value) {
  if (!key || !value) {
    return false;
  }

  if (strcmp(key, "channel") == 0) {
    const int channel = atoi(value);
    if (channel >= kMinEspnowChannel && channel <= kMaxEspnowChannel) {
      preferred_channel_ = static_cast<uint8_t>(channel);
      if (channel_locked_) {
        return (current_channel_ == preferred_channel_);
      }
      return applyChannel(preferred_channel_);
    }
  } else if (strcmp(key, "tx_power") == 0) {
    const int power = atoi(value);
    if (power >= 8 && power <= 84) {
      esp_err_t result = esp_wifi_set_max_tx_power(power);
      return (result == ESP_OK);
    }
  }

  return false;
}

bool ESP32_RC_ESPNOW::getProtocolConfig(const char* key, char* value, size_t len) {
  if (!key || !value || len == 0) {
    return false;
  }

  if (strcmp(key, "protocol") == 0) {
    strncpy(value, "ESPNOW", len - 1);
    value[len - 1] = '\0';
    return true;
  }

  if (strcmp(key, "channel") == 0) {
    snprintf(value, len, "%u", getCurrentChannel());
    return true;
  }

  if (strcmp(key, "channel_locked") == 0) {
    strncpy(value, channel_locked_ ? "1" : "0", len - 1);
    value[len - 1] = '\0';
    return true;
  }

  return false;
}

void ESP32_RC_ESPNOW::createBroadcastAddress(RCAddress_t& broadcast_addr) const {
  uint8_t broadcast_mac[RC_ADDR_SIZE] = RC_BROADCAST_MAC;
  memcpy(broadcast_addr, broadcast_mac, RC_ADDR_SIZE);
}
