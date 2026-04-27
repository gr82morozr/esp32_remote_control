#include <Arduino.h>

/*
ESP32 Dummy Sensor Collector

Flash this sketch to the second ESP32. It receives command packets from the
serial ESP-NOW bridge, keeps the latest command state, and sends dummy telemetry
back over ESP-NOW for verification from the PC serial UI/log. The application
role differs from the bridge, but the ESP-NOW lifecycle is still symmetric:
create the same controller type, register receive handling, call connect(), and
use sendData() for outbound payloads.
*/

#define ESP32_RC_PROTOCOL RC_PROTO_ESPNOW

#include "esp32_rc_factory.h"

#ifndef BUILTIN_LED
#ifdef LED_BUILTIN
#define BUILTIN_LED LED_BUILTIN
#endif
#endif

static constexpr uint32_t TELEMETRY_INTERVAL_MS = 10;
static constexpr uint8_t PACKET_TYPE_TELEMETRY = 2;

ESP32RemoteControl* controller = nullptr;

portMUX_TYPE command_lock = portMUX_INITIALIZER_UNLOCKED;
bool command_received = false;
RCPayload_t latest_command = {};
RCPayload_t telemetry = {};

uint32_t last_telemetry_ms = 0;
uint32_t telemetry_counter = 0;

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

  // Bridge-side mapping:
  // id1     = packet type (telemetry)
  // id2     = telemetry sequence counter
  // id3/id4 = echoed command IDs for correlation
  // value1  = time in seconds
  // value2  = temperature in degrees C
  // value3  = voltage in V
  // value4  = echoed command value1
  // value5  = echoed command value2
  // flags.0 = 1 once any command has been received
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

void printTelemetryStatus(const RCPayload_t& payload) {
  Serial.printf(
    "time:%.2f,temp:%.2f,voltage:%.2f,cmd_value1:%.2f,cmd_value2:%.2f,command_seen:%u\n",
    payload.value1,
    payload.value2,
    payload.value3,
    payload.value4,
    payload.value5,
    payload.flags & 0x01);
}

void setup() {
  Serial.begin(230400);
  delay(1000);

#ifdef BUILTIN_LED
  pinMode(BUILTIN_LED, OUTPUT);
#endif

  Serial.println("SENSOR_STATUS:starting");

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

    if (controller->sendData(telemetry)) {
      printTelemetryStatus(telemetry);
    } else {
      Serial.println("SENSOR_ERROR:send_failed");
    }
  }

  delay(1);
}
