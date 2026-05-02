#include <Arduino.h>

/*
ESP32 Dummy Sensor Collector

Flash this sketch to the second ESP32. It receives command packets from the
serial ESP-NOW bridge, keeps the latest command state, and sends dummy telemetry
back over ESP-NOW for verification from the PC serial UI/log.
*/

#define ESP32_RC_PROTOCOL RC_PROTO_ESPNOW

#include "esp32_rc_factory.h"

#ifndef BUILTIN_LED
#ifdef LED_BUILTIN
#define BUILTIN_LED LED_BUILTIN
#endif
#endif

static constexpr uint32_t TELEMETRY_INTERVAL_MS = 10;

ESP32RemoteControl* controller = nullptr;

portMUX_TYPE command_lock = portMUX_INITIALIZER_UNLOCKED;
bool command_received = false;
RCPayload_t latest_command = {};
RCPayload_I16x8_Time_t telemetry = {};

uint32_t last_telemetry_ms = 0;
uint32_t telemetry_counter = 0;

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

  // Fixed-point telemetry mapping:
  // seq       = 16-bit telemetry sequence counter
  // sample_us = timestamp from this ESP32
  // value[0]  = temperature, scale 0.01 C
  // value[1]  = voltage, scale 0.001 V
  // value[2]  = echoed command value1, scale 0.01
  // value[3]  = echoed command value2, scale 0.01
  // value[4]  = echoed command id1
  // value[5]  = echoed command id2
  // value[6]  = echoed command flags
  // value[7]  = telemetry interval in ms
  // flags.0   = 1 once any command has been received
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
  payload.reserved1 = 0;
  payload.reserved2 = 0;
}

void printPlotterTelemetry(const RCPayload_I16x8_Time_t& payload) {
  Serial.printf(
    "seq:%u,sample_us:%lu,temp:%.2f,voltage:%.3f,cmd_value1:%.2f,cmd_value2:%.2f,command_seen:%u\n",
    payload.seq,
    static_cast<unsigned long>(payload.sample_us),
    rcDecodeScaledInt16(payload.value[0], 0.01f),
    rcDecodeScaledInt16(payload.value[1], 0.001f),
    rcDecodeScaledInt16(payload.value[2], 0.01f),
    rcDecodeScaledInt16(payload.value[3], 0.01f),
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
      //printPlotterTelemetry(telemetry);
    } else {
      Serial.println("SENSOR_ERROR:send_failed");
    }
  }

  delay(1);
}
