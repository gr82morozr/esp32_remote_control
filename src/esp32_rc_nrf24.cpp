#include "esp32_rc_nrf24.h"


ESP32_RC_NRF24* ESP32_RC_NRF24::instance_ = nullptr;

ESP32_RC_NRF24::ESP32_RC_NRF24(bool fast_mode)
    : ESP32RemoteControl(fast_mode) {
    instance_ = this;
    LOG("Initializing NRF24...");
    spiBus_ = new SPIClass(NRF_SPI_BUS);
    spiBus_->begin(PIN_NRF_SCK,PIN_NRF_MISO , PIN_NRF_MOSI);
    radio_  = RF24(PIN_NRF_CE, PIN_NRF_CSN);

    if (!init()) {
        LOG_ERROR("NRF24 init failed!");
        SYS_HALT;
    }

    LOG("Initialized successfully.");

    if (receiveTaskHandle_) {
      vTaskDelete(receiveTaskHandle_);
    }

    xTaskCreatePinnedToCore(
      instance_->receiveLoopWrapper,
      "NRF24Receive", 
      2048, 
      instance_, 
      2, 
      &receiveTaskHandle_, 
      0
    );
    LOG("Receiver task created.");
}

ESP32_RC_NRF24::~ESP32_RC_NRF24() {
  if (receiveTaskHandle_) {
    vTaskDelete(receiveTaskHandle_);
  }
  radio_.powerDown();
}

bool ESP32_RC_NRF24::init() {
  genUniqueAddr(my_addr_);
  if (!radio_.begin(spiBus_)) {


    
    LOG_ERROR("NRF24 begin failed!");
    SYS_HALT ;
  }
  radio_.setChannel(NRF24_CHANNEL);
  radio_.setPALevel(RF24_PA_HIGH);
  radio_.setDataRate(RF24_1MBPS);
  radio_.setAutoAck(true);
  radio_.setRetries(5, 15);
  radio_.enableAckPayload();

  radio_.stopListening();
  // Set up the broadcast address
  radio_.openReadingPipe(1, BROADCAST_ADDR);
  radio_.setAutoAck(1, false);  
  // Set up the peer address
  radio_.openReadingPipe(0, my_addr_);
  radio_.setAutoAck(0, true);
  radio_.startListening();
  
  LOG("My Address is: %s\n", formatAddr(my_addr_));

  switchToBroadcastPipe();
  return true;
}

void ESP32_RC_NRF24::receiveLoopWrapper(void* arg) {
  auto* self = static_cast<ESP32_RC_NRF24*>(arg);
  self->receiveLoop(arg);  // Call instance method
}

bool ESP32_RC_NRF24::isSameAddr(const uint8_t *a, const uint8_t *b) {
  return memcmp(a, b, RC_ADDR_SIZE) == 0;
}

void ESP32_RC_NRF24::lowLevelSend(const RCMessage_t& msg) {
  // Decide which address to send to
  bool sendSuccess = false;

  radio_.stopListening();
  sendSuccess = radio_.write(&msg, sizeof(msg));
  radio_.startListening();

  if (!sendSuccess && pipeType_ == 1) { // 
    LOG("[SEND ERROR] Failed to send message of type: %d - %d", msg.type, pipeType_);
    return;
  } 
  LOG("[SENT SUCCESS] Type: %d", msg.type); 

}

void ESP32_RC_NRF24::checkHeartbeat()  {
    ESP32RemoteControl::checkHeartbeat();  // Call base class method

    if (conn_state_ == RCConnectionState_t::DISCONNECTED) {
      switchToBroadcastPipe();  // Switch to broadcast pipe on disconnect
    }
}



String ESP32_RC_NRF24::formatAddr(const uint8_t addr[RC_ADDR_SIZE]) {
  String out;
  for (int i = 0; i < RC_ADDR_SIZE; ++i) {
    char buf[3];
    sprintf(buf, "%02X", addr[i]);
    out += buf;
  }
  return out;
}

void ESP32_RC_NRF24::receiveLoop(void* arg) {
  while (true) {
    if (radio_.available()) {
      RCMessage_t msg;
      radio_.read(&msg, sizeof(msg));

      if (isSameAddr(msg.from_addr, my_addr_)) { // Ignore messages from self
        continue;
      };

      LOG("[RECEIVED] Type= %d , Address= %s", msg.type, formatAddr(msg.from_addr));

      if (msg.type == RCMSG_TYPE_HEARTBEAT) {
        onHeartbeatReceived(msg);

        switchToPeerPipe();
      } else if (msg.type == RCMSG_TYPE_DATA) {
        LOG("[DATA] Received from peer");
        
        onDataReceived(msg);
        switchToPeerPipe();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void ESP32_RC_NRF24::genUniqueAddr(uint8_t out[RC_ADDR_SIZE]) {
  uint64_t chipId = ESP.getEfuseMac();
  out[0] = 0xD2;
  out[1] = (chipId >> 0) & 0xFF;
  out[2] = (chipId >> 8) & 0xFF;
  out[3] = (chipId >> 16) & 0xFF;
  out[4] = (chipId >> 24) & 0xFF;
  out[5] = (chipId >> 32) & 0xFF;
}

void ESP32_RC_NRF24::switchToBroadcastPipe() {
  if (pipeType_ == 0) {
    return;
  };
  radio_.openWritingPipe(BROADCAST_ADDR);
  pipeType_ = 0;  // Set to broadcast pipe type
  LOG("Switched to BROADCAST pipe");
}

void ESP32_RC_NRF24::switchToPeerPipe() {
  if (pipeType_ == 1) {
    return;
  };
  radio_.openWritingPipe(peer_addr_);
  pipeType_ = 1;  // Set to peer pipe type
  LOG("Switched to PEER pipe, PeerAddress = %s", formatAddr(peer_addr_));
}