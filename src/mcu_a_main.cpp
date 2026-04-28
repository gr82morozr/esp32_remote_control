#include <Arduino.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp32_rc_factory.h"

/*
ESP32 Serial-to-ESPNOW CSV Bridge

Reads newline-terminated CSV packets from USB serial and forwards them over
ESP-NOW. Incoming ESP-NOW payloads are copied in the receive callback and
printed later from loop(), keeping callback work short and non-blocking.
*/

static constexpr size_t SERIAL_LINE_MAX = 128;
static constexpr uint8_t RX_QUEUE_DEPTH = 64;
#ifndef BRIDGE_ENABLE_DROP_DETECT
#define BRIDGE_ENABLE_DROP_DETECT 1
#endif
#ifndef BRIDGE_OUTPUT_MODE_CSV_VERBOSE
#define BRIDGE_OUTPUT_MODE_CSV_VERBOSE 0
#endif
#ifndef BRIDGE_OUTPUT_MODE_CSV_COMPACT
#define BRIDGE_OUTPUT_MODE_CSV_COMPACT 1
#endif
#ifndef BRIDGE_OUTPUT_MODE_BINARY
#define BRIDGE_OUTPUT_MODE_BINARY 0
#endif

#if ((BRIDGE_OUTPUT_MODE_CSV_VERBOSE ? 1 : 0) + \
     (BRIDGE_OUTPUT_MODE_CSV_COMPACT ? 1 : 0) + \
     (BRIDGE_OUTPUT_MODE_BINARY ? 1 : 0)) != 1
#error "Select exactly one bridge output mode"
#endif

static constexpr uint8_t PACKET_TYPE_TELEMETRY = 2;
static constexpr uint8_t BRIDGE_BINARY_TYPE_RX = 1;
static constexpr uint8_t BRIDGE_BINARY_TYPE_TX = 2;
static constexpr uint8_t BRIDGE_BINARY_SYNC_0 = 0xAA;
static constexpr uint8_t BRIDGE_BINARY_SYNC_1 = 0x55;
ESP32RemoteControl* espnow_controller = nullptr;

char serial_line[SERIAL_LINE_MAX] = {};
size_t serial_line_len = 0;

struct PayloadRing {
  volatile uint8_t head = 0;
  volatile uint8_t tail = 0;
  RCPayload_t payloads[RX_QUEUE_DEPTH] = {};

  bool push(const RCPayload_t& payload) {
    const uint8_t next = (head + 1) % RX_QUEUE_DEPTH;
    if (next == tail) return false;
    payloads[head] = payload;
    __sync_synchronize();
    head = next;
    return true;
  }

  bool pop(RCPayload_t& payload) {
    if (tail == head) return false;
    __sync_synchronize();
    payload = payloads[tail];
    tail = (tail + 1) % RX_QUEUE_DEPTH;
    return true;
  }
};

PayloadRing rx_queue; 
volatile uint16_t rx_overflow_count = 0;
#if BRIDGE_ENABLE_DROP_DETECT
bool have_last_telemetry_seq = false;
uint8_t last_telemetry_seq = 0;
float last_telemetry_time = 0.0f;
#endif

bool parseUInt8Field(char* field, uint8_t& value) {
  if (!field || *field == '\0') return false;

  char* end = nullptr;
  long parsed = strtol(field, &end, 10);
  if (end == field || *end != '\0') return false;
  if (parsed < 0 || parsed > 255) return false;

  value = static_cast<uint8_t>(parsed);
  return true;
}

bool parseFloatField(char* field, float& value) {
  if (!field || *field == '\0') return false;

  char* end = nullptr;
  float parsed = strtof(field, &end);
  if (end == field || *end != '\0') return false;
  if (!isfinite(parsed)) return false;

  value = parsed;
  return true;
}

bool parsePayloadCsv(char* line, RCPayload_t& payload) {
  uint8_t field_count = 0;
  char* cursor = line;

  while (cursor) {
    if (field_count >= 10) return false;

    char* comma = strchr(cursor, ',');
    if (comma) {
      *comma = '\0';
    }

    bool ok = false;
    switch (field_count) {
      case 0: ok = parseUInt8Field(cursor, payload.id1); break;
      case 1: ok = parseUInt8Field(cursor, payload.id2); break;
      case 2: ok = parseUInt8Field(cursor, payload.id3); break;
      case 3: ok = parseUInt8Field(cursor, payload.id4); break;
      case 4: ok = parseFloatField(cursor, payload.value1); break;
      case 5: ok = parseFloatField(cursor, payload.value2); break;
      case 6: ok = parseFloatField(cursor, payload.value3); break;
      case 7: ok = parseFloatField(cursor, payload.value4); break;
      case 8: ok = parseFloatField(cursor, payload.value5); break;
      case 9: ok = parseUInt8Field(cursor, payload.flags); break;
    }

    if (!ok) return false;

    field_count++;
    cursor = comma ? comma + 1 : nullptr;
  }

  return field_count == 10;
}

void printPayloadFields(const char* prefix, const RCPayload_t& payload) {
#if BRIDGE_OUTPUT_MODE_CSV_VERBOSE
  if (prefix && prefix[0] != '\0') {
    Serial.printf("%s:", prefix);
  }
  Serial.printf(
    "id1=%u,id2=%u,id3=%u,id4=%u,value1=%.2f,value2=%.2f,value3=%.2f,value4=%.2f,value5=%.2f,flags=%u\n",
    payload.id1, payload.id2, payload.id3, payload.id4,
    payload.value1, payload.value2, payload.value3, payload.value4, payload.value5,
    payload.flags);
#elif BRIDGE_OUTPUT_MODE_CSV_COMPACT
  if (prefix && prefix[0] != '\0') {
    Serial.printf("%s:", prefix);
  }
  Serial.printf(
    "%u,%u,%u,%u,%.2f,%.2f,%.2f,%.2f,%.2f,%u\n",
    payload.id1, payload.id2, payload.id3, payload.id4,
    payload.value1, payload.value2, payload.value3, payload.value4, payload.value5,
    payload.flags);
#else
  const uint8_t* payload_bytes = reinterpret_cast<const uint8_t*>(&payload);
  const uint8_t frame_type =
      (prefix && prefix[0] != '\0') ? BRIDGE_BINARY_TYPE_TX : BRIDGE_BINARY_TYPE_RX;
  uint8_t checksum = frame_type;
  for (size_t i = 0; i < sizeof(payload); ++i) {
    checksum ^= payload_bytes[i];
  }

  const uint8_t header[] = {
    BRIDGE_BINARY_SYNC_0,
    BRIDGE_BINARY_SYNC_1,
    frame_type
  };
  Serial.write(header, sizeof(header));
  Serial.write(payload_bytes, sizeof(payload));
  Serial.write(&checksum, 1);
#endif
}

#if BRIDGE_ENABLE_DROP_DETECT
void reportTelemetryGap(const RCPayload_t& payload) {
  if (payload.id1 != PACKET_TYPE_TELEMETRY) {
    return;
  }

  if (have_last_telemetry_seq && payload.value1 + 0.5f < last_telemetry_time) {
    have_last_telemetry_seq = false;
  }

  if (have_last_telemetry_seq) {
    const uint8_t expected_seq = static_cast<uint8_t>(last_telemetry_seq + 1);
    if (payload.id2 != expected_seq) {
      const uint8_t missing_count =
          static_cast<uint8_t>(payload.id2 - expected_seq);
      Serial.printf(
        "DROP_DETECTED:last_seq=%u,current_seq=%u,missing=%u\n",
        last_telemetry_seq, payload.id2, missing_count);
    }
  }

  have_last_telemetry_seq = true;
  last_telemetry_seq = payload.id2;
  last_telemetry_time = payload.value1;
}
#endif

void printReceivedPayload(const RCPayload_t& payload) {
#if BRIDGE_ENABLE_DROP_DETECT && !BRIDGE_OUTPUT_MODE_BINARY
  reportTelemetryGap(payload);
#endif
  printPayloadFields("", payload);
}

void onDataReceived(const RCMessage_t& msg) {
  const RCPayload_t* payload = msg.getPayload();
  if (!rx_queue.push(*payload)) {
    __atomic_add_fetch(&rx_overflow_count, 1, __ATOMIC_RELAXED);
  }
}

void handleSerialLine(char* line) {
  RCPayload_t payload = {};

  if (!parsePayloadCsv(line, payload)) {
    Serial.println("RC_ERROR:bad_csv");
    return;
  }

  if (espnow_controller->sendData(payload)) {
    printPayloadFields("RC_SENT", payload);
  } else {
    Serial.println("RC_ERROR:send_failed");
  }
}

void pollSerialInput() {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());

    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      if (serial_line_len > 0) {
        serial_line[serial_line_len] = '\0';
        handleSerialLine(serial_line);
        serial_line_len = 0;
      }
      continue;
    }

    if (serial_line_len < SERIAL_LINE_MAX - 1) {
      serial_line[serial_line_len++] = ch;
    } else {
      serial_line_len = 0;
      Serial.println("RC_ERROR:line_too_long");
    }
  }
}

void flushReceivedData() {
  uint16_t overflow_count = __atomic_exchange_n(&rx_overflow_count, 0, __ATOMIC_RELAXED);
  if (overflow_count > 0) {
    Serial.printf("RC_ERROR:rx_overflow,%u\n", overflow_count);
  }

  RCPayload_t payload = {};
  while (rx_queue.pop(payload)) {
    printReceivedPayload(payload);
  }
}

void setup() {
  Serial.begin(230400);
  DELAY(1000);

  Serial.println("RC_STATUS:starting");

  espnow_controller = createProtocolInstance(RC_PROTO_ESPNOW, true);

  if (espnow_controller) {
    espnow_controller->enableMetricsDisplay(false);
    espnow_controller->setOnReceiveMsgHandler(onDataReceived);
    espnow_controller->connect();
    Serial.println("RC_STATUS:ready");
  } else {
    Serial.println("RC_ERROR:init_failed");
  }
}

void loop() {
  if (!espnow_controller) {
    DELAY(1000);
    return;
  }

  pollSerialInput();
  flushReceivedData();

  DELAY(1);
}
