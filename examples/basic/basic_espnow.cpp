#include <Arduino.h>

/*
ESP32 Remote Control - ESP-NOW 5 ms Stress Test

Flash this same sketch to two ESP32 boards to test peer discovery, signal
transfer reliability, and sustained packet throughput. The sketch uses fast
mode and attempts to send one data packet every 5 ms.
*/

#define ESP32_RC_PROTOCOL RC_PROTO_ESPNOW

#include "esp32_rc_factory.h"

#ifndef BUILTIN_LED
#ifdef LED_BUILTIN
#define BUILTIN_LED LED_BUILTIN
#endif
#endif

static constexpr uint32_t SEND_INTERVAL_MS = 5;

ESP32RemoteControl* controller = nullptr;

uint32_t last_data_send_ms = 0;
RCPayload_t outgoing = {};
RCPayload_t incoming = {};

void populateDummyData(RCPayload_t& payload) {
  static uint32_t counter = 0;
  counter++;

  const float time_sec = millis() / 1000.0f;
  const float phase = time_sec * 0.1f;

  payload.id1 = (counter / 10) % 256;
  payload.id2 = (counter / 5) % 256;
  payload.id3 = counter % 256;
  payload.id4 = (counter * 3) % 256;

  payload.value1 = time_sec;
  payload.value2 = sin(phase) * 1000.0f;
  payload.value3 = random(0, 5000) / 1000.0f;
  payload.value4 = 20.0f + sin(phase * 2.0f) * 10.0f;
  payload.value5 = (counter % 1000) / 10.0f;

  const uint8_t bit_pos = counter % 8;
  payload.flags = (1 << bit_pos) | (counter & 0x0F);
}

void setup() {
  Serial.begin(115200);
  DELAY(1000);

  if (!isProtocolAvailable(ESP32_RC_PROTOCOL)) {
    LOG_ERROR("Protocol %s not available", protocolToString(ESP32_RC_PROTOCOL));
    SYS_HALT;
  }

  controller = createProtocolInstance(ESP32_RC_PROTOCOL, true);
  if (!controller) {
    LOG_ERROR("Failed to create protocol instance");
    SYS_HALT;
  }

#ifdef BUILTIN_LED
  pinMode(BUILTIN_LED, OUTPUT);
#endif

  ESP32RemoteControl::enableGlobalMetrics(true);
  controller->enableMetricsDisplay(true, 1000);
  controller->connect();

  LOG("ESP32_RC ESP-NOW 5 ms stress test started");
  LOG("Protocol: %s, send interval: %lu ms", protocolToString(controller->getProtocol()), SEND_INTERVAL_MS);
}

void loop() {
  const uint32_t now = millis();

  if (now - last_data_send_ms >= SEND_INTERVAL_MS) {
    last_data_send_ms = now;
    populateDummyData(outgoing);
    controller->sendData(outgoing);
  }

  if (controller->recvData(incoming)) {
#ifdef BUILTIN_LED
    toggleGPIO(BUILTIN_LED);
#endif
  }

  controller->printMetrics();
  DELAY(1);
}
