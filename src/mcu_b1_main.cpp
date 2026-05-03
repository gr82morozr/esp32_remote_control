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

static constexpr uint32_t TELEMETRY_INTERVAL_MS = 5;
static constexpr uint32_t SCHEMA_INTERVAL_MS = RC_SCHEMA_INTERVAL_MS;
static constexpr uint8_t TELEMETRY_SCHEMA_ID = 1;
static constexpr uint8_t TELEMETRY_SCHEMA_VERSION = 1;
static const char TELEMETRY_SCHEMA[] =
  "n=i16x8t;f=seq:u16:1,"
  "s_us:u32:us,"
  "v0:i16:.01:temp,"
  "v1:i16:.001:volt,"
  "v2:i16:.01:cmd1,"
  "v3:i16:.01:cmd2,"
  "v4:i16:1:id1,"
  "v5:i16:1:id2,"
  "v6:i16:1:cflg,"
  "v7:i16:ms:dt,"
  "fl:u8:seen,"
  "r1:u8:sid,"
  "r2:u8:sver";

ESP32RemoteControl* controller = nullptr;

portMUX_TYPE command_lock = portMUX_INITIALIZER_UNLOCKED;
bool command_received = false;
RCPayload_t latest_command = {};
RCPayload_I16x8_Time_t telemetry = {};

uint32_t last_telemetry_ms = 0;
uint32_t last_schema_ms = 0;
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
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    Serial.println("SENSOR_WIFI:released_for_espnow_channel_hopping");
  }
}

void onCommandReceived(const RCMessage_t& msg) {
  RCPayload_t payload = {};
  msg.copyPayloadTo(payload);

  portENTER_CRITICAL(&command_lock);
  latest_command = payload;
  command_received = true;
  portEXIT_CRITICAL(&command_lock);

#ifdef BUILTIN_LED
  digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED));
#endif
}

void populateTelemetry(RCPayload_I16x8_Time_t& payload) {
  RCPayload_t command_snapshot = {};
  bool command_seen = false;

  portENTER_CRITICAL(&command_lock);
  command_snapshot = latest_command;
  command_seen = command_received;
  portEXIT_CRITICAL(&command_lock);

  const float time_sec = millis() / 1000.0f;
  const float temperature_c = 20.0f + sin(time_sec) * 5.0f;
  const float voltage_v = 3.3f + sin(time_sec * 0.5f) * 0.1f;

  payload.seq = static_cast<uint16_t>(++telemetry_counter);
  payload.sample_us = micros();
  payload.value[0] = rcEncodeScaledFloat(temperature_c, 0.01f);
  payload.value[1] = rcEncodeScaledFloat(voltage_v, 0.001f);
  payload.value[2] = rcEncodeScaledFloat(command_snapshot.value1, 0.01f);
  payload.value[3] = rcEncodeScaledFloat(command_snapshot.value2, 0.01f);
  payload.value[4] = command_snapshot.id1;
  payload.value[5] = command_snapshot.id2;
  payload.value[6] = command_snapshot.flags;
  payload.value[7] = TELEMETRY_INTERVAL_MS;
  payload.flags = command_seen ? 0x01 : 0x00;
  payload.reserved1 = TELEMETRY_SCHEMA_ID;
  payload.reserved2 = TELEMETRY_SCHEMA_VERSION;
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

  if (controller->getConnectionState() == RCConnectionState_t::CONNECTED &&
      (last_schema_ms == 0 || now - last_schema_ms >= SCHEMA_INTERVAL_MS)) {
    if (controller->sendSchema(TELEMETRY_SCHEMA, TELEMETRY_SCHEMA_ID)) {
      last_schema_ms = now;
    }
  }

  if (now - last_telemetry_ms >= TELEMETRY_INTERVAL_MS) {
    last_telemetry_ms = now;

    populateTelemetry(telemetry);

    if (!controller->sendData(telemetry)) {
      Serial.println("SENSOR_ERROR:send_failed");
    }
  }

  delay(1);
}
