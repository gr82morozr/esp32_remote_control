#include <Arduino.h>
#include <WiFi.h>

/*
ESP32 Dummy Sensor Collector With Wi-Fi Lock

This variant of the dummy sensor connects to a Wi-Fi AP before starting
ESP-NOW. That forces the ESP-NOW transport to lock to the AP channel and
exercises the HELLO/channel-negotiation path against an unlocked peer.
*/

#define ESP32_RC_PROTOCOL RC_PROTO_ESPNOW

#include "esp32_rc_factory.h"

#ifndef BUILTIN_LED
#ifdef LED_BUILTIN
#define BUILTIN_LED LED_BUILTIN
#endif
#endif

static constexpr uint32_t TELEMETRY_INTERVAL_MS = 11;
static constexpr uint8_t PACKET_TYPE_TELEMETRY = 2;

ESP32RemoteControl* controller = nullptr;

portMUX_TYPE command_lock = portMUX_INITIALIZER_UNLOCKED;
bool command_received = false;
RCPayload_t latest_command = {};
RCPayload_t telemetry = {};

uint32_t last_telemetry_ms = 0;
uint32_t telemetry_counter = 0;

void connectWiFiForEspNowLock() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(RC_WIFI_SSID, RC_WIFI_PASSWORD);

  const uint32_t start_ms = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - start_ms) < RC_WIFI_TIMEOUT_MS) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("SENSOR_WIFI:connected,ssid=%s,channel=%u,ip=%s\n",
      RC_WIFI_SSID,
      WiFi.channel(),
      WiFi.localIP().toString().c_str());
  } else {
    Serial.printf("SENSOR_WIFI:failed,ssid=%s\n", RC_WIFI_SSID);
  }
}

void onCommandReceived(const RCMessage_t& msg) {
  const RCPayload_t* payload = msg.getPayload();

  portENTER_CRITICAL(&command_lock);
  latest_command = *payload;
  command_received = true;
  portEXIT_CRITICAL(&command_lock);

#ifdef BUILTIN_LED
  digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED));
#endif
}

void populateTelemetry(RCPayload_t& payload) {
  RCPayload_t command_snapshot = {};
  bool command_seen = false;

  portENTER_CRITICAL(&command_lock);
  command_snapshot = latest_command;
  command_seen = command_received;
  portEXIT_CRITICAL(&command_lock);

  telemetry_counter++;

  const float time_sec = millis() / 1000.0f;

  payload.id1 = PACKET_TYPE_TELEMETRY;
  payload.id2 = telemetry_counter & 0xFF;
  payload.id3 = command_snapshot.id1;
  payload.id4 = command_snapshot.id2;

  payload.value1 = time_sec;
  payload.value2 = 20.0f + sin(time_sec) * 5.0f;
  payload.value3 = 3.3f + sin(time_sec * 0.5f) * 0.1f;
  payload.value4 = command_snapshot.value1;
  payload.value5 = command_snapshot.value2;

  payload.flags = command_seen ? 0x01 : 0x00;
}

void setup() {
  Serial.begin(230400);
  delay(1000);

#ifdef BUILTIN_LED
  pinMode(BUILTIN_LED, OUTPUT);
#endif

  Serial.println("SENSOR_STATUS:starting_wifi_locked");

  connectWiFiForEspNowLock();

  controller = createProtocolInstance(RC_PROTO_ESPNOW, false);
  if (!controller) {
    Serial.println("SENSOR_ERROR:init_failed");
    return;
  }

  controller->enableMetricsDisplay(false);
  controller->setOnReceiveMsgHandler(onCommandReceived);
  controller->connect();

  Serial.println("SENSOR_STATUS:ready");
}

void loop() {
  if (!controller) {
    delay(1000);
    return;
  }

  const uint32_t now = millis();

  if (now - last_telemetry_ms >= TELEMETRY_INTERVAL_MS) {
    last_telemetry_ms = now;

    populateTelemetry(telemetry);

    if (!controller->sendData(telemetry)) {
      Serial.println("SENSOR_ERROR:send_failed");
    }
  }

  delay(1);
}
