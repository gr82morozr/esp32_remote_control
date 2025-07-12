#include "esp32_rc_wifi.h"

ESP32_RC_WIFI* ESP32_RC_WIFI::instance_ = nullptr;

ESP32_RC_WIFI::ESP32_RC_WIFI(bool fast_mode) : ESP32RemoteControl(fast_mode) {
  uint32_t mac = (uint32_t)ESP.getEfuseMac();
  instance_ = this;  // Set the static instance pointer
  LOG("Initializing WiFi RC with MAC: %08X", mac);

  // Initialize Receive Task
  xTaskCreatePinnedToCore(instance_->receiveLoopWrapper, "NRF24Receive", 2048,
                          instance_, 2, &receiveTaskHandle_, 0);
  LOG("Receiver task created.");
};

ESP32_RC_WIFI::~ESP32_RC_WIFI() {

};

bool ESP32_RC_WIFI::init() {
  // Get current date (MMDD)
  randomSeed(esp_random());
  uint16_t rnd = random(0, 10000);
  sprintf(rc_ssid_, "ESP32_RC_%04u", rnd);

  return true;  // Initialization successful
}

void ESP32_RC_WIFI::autoNegotiateRole() {
  LOG("[WiFi] Generated SSID: %s", rc_ssid_);
  LOG("[WiFi] Scanning for peer AP...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  DELAY(200);

  int n = WiFi.scanNetworks();
  bool found_peer = false;

  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid.startsWith("ESP32_RC_") && strcmp(ssid.c_str(), rc_ssid_) != 0) {
      strcpy(rc_ssid_, ssid.c_str());
      found_peer = true;
      break;
    }
    LOG("[WiFi] Found peer AP: %s\n", ssid.c_str());
  }

  if (found_peer) {
    LOG("[WiFi] Peer found (%s), connecting as STA...", rc_ssid_);
    WiFi.begin((const char*)rc_ssid_, (const char*)RC_WIFI_PASSWORD);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
      DELAY(200);
    }
    if (WiFi.status() == WL_CONNECTED) {
      LOG("[WiFi] STA Connected.");
      is_ap_ = false;
      waitForAPConnection();
    }
  } else {
    LOG("[WiFi] No peer found, starting AP: %s\n", rc_ssid_);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(rc_ssid_, RC_WIFI_PASSWORD);
    DELAY(200);
    is_ap_ = true;
    waitForSTAConnection();
  }
}

void ESP32_RC_WIFI::waitForAPConnection() {
  tcp_client_.stop();
  tcp_client_.connect(IPAddress(192, 168, 4, 1), RC_WIFI_PORT);
  unsigned long t0 = millis();
  while (!tcp_client_.connected() && millis() - t0 < 5000) {
    tcp_client_.connect(IPAddress(192, 168, 4, 1), RC_WIFI_PORT);
    delay(200);
  }
  connected_ = tcp_client_.connected();
  conn_state_ = connected_ ? RCConnectionState_t::CONNECTED
                           : RCConnectionState_t::DISCONNECTED;
  Serial.printf("[TCP] STA connection %s\n", connected_ ? "OK" : "FAIL");
}

void ESP32_RC_WIFI::waitForSTAConnection() {
  tcp_server_.begin();
  tcp_server_.setNoDelay(true);
  unsigned long t0 = millis();
  while (!tcp_client_ && millis() - t0 < 10000) {
    tcp_client_ = tcp_server_.available();
    delay(100);
  }
  connected_ = tcp_client_ && tcp_client_.connected();
  conn_state_ = connected_ ? RCConnectionState_t::CONNECTED
                           : RCConnectionState_t::DISCONNECTED;
  Serial.printf("[TCP] AP accept connection %s\n", connected_ ? "OK" : "FAIL");
}

void ESP32_RC_WIFI::receiveLoopWrapper(void* arg) {
  auto* self = static_cast<ESP32_RC_WIFI*>(arg);
  self->receiveLoop(arg);  // Call instance method
}

void ESP32_RC_WIFI::receiveLoop(void* arg) {
  while (true) {
    // Implement the WiFi receive logic here
    // This is a placeholder for actual WiFi data reception
    RCMessage_t msg;

    // Simulate receiving a message

    DELAY(100);  // Simulate some delay for receiving
  }
}
void ESP32_RC_WIFI::connect() {
  init();
  autoNegotiateRole();
};

void ESP32_RC_WIFI::lowLevelSend(const RCMessage_t& msg) {

};