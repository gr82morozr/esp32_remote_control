#include <Arduino.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp32_rc_factory.h"

#ifndef BUILTIN_LED
#ifdef LED_BUILTIN
#define BUILTIN_LED LED_BUILTIN
#endif
#endif

/*
ESP32 Serial-to-ESPNOW CSV Bridge

Reads newline-terminated CSV packets from USB serial and forwards them over
ESP-NOW. Incoming ESP-NOW payloads are copied in the receive callback and
printed later from loop(), keeping callback work short and non-blocking.
*/

static constexpr size_t SERIAL_LINE_MAX = 128;
static constexpr uint8_t RX_QUEUE_DEPTH = 64;
#ifndef BRIDGE_ENABLE_DROP_DETECT
#define BRIDGE_ENABLE_DROP_DETECT 0
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

static constexpr uint8_t BRIDGE_BINARY_TYPE_RX = 1;
static constexpr uint8_t BRIDGE_BINARY_TYPE_TX = 2;
static constexpr uint8_t BRIDGE_BINARY_SYNC_0 = 0xAA;
static constexpr uint8_t BRIDGE_BINARY_SYNC_1 = 0x55;
static constexpr uint8_t SCHEMA_QUEUE_DEPTH = 32;
static constexpr size_t SCHEMA_BUFFER_MAX = 512;
ESP32RemoteControl* espnow_controller = nullptr;

char serial_line[SERIAL_LINE_MAX] = {};
size_t serial_line_len = 0;
bool serial_line_discarding = false;

struct PayloadRing {
  volatile uint8_t head = 0;
  volatile uint8_t tail = 0;
  RCPayload_I16x8_Time_t payloads[RX_QUEUE_DEPTH] = {};

  bool push(const RCPayload_I16x8_Time_t& payload) {
    const uint8_t next = (head + 1) % RX_QUEUE_DEPTH;
    if (next == tail) return false;
    payloads[head] = payload;
    __sync_synchronize();
    head = next;
    return true;
  }

  bool pop(RCPayload_I16x8_Time_t& payload) {
    if (tail == head) return false;
    __sync_synchronize();
    payload = payloads[tail];
    tail = (tail + 1) % RX_QUEUE_DEPTH;
    return true;
  }
};

PayloadRing rx_queue; 
struct SchemaRing {
  volatile uint8_t head = 0;
  volatile uint8_t tail = 0;
  RCSchemaChunk_t chunks[SCHEMA_QUEUE_DEPTH] = {};

  bool push(const RCSchemaChunk_t& chunk) {
    const uint8_t next = (head + 1) % SCHEMA_QUEUE_DEPTH;
    if (next == tail) return false;
    chunks[head] = chunk;
    __sync_synchronize();
    head = next;
    return true;
  }

  bool pop(RCSchemaChunk_t& chunk) {
    if (tail == head) return false;
    __sync_synchronize();
    chunk = chunks[tail];
    tail = (tail + 1) % SCHEMA_QUEUE_DEPTH;
    return true;
  }
};

SchemaRing schema_queue;
volatile uint16_t rx_overflow_count = 0;
volatile uint16_t schema_overflow_count = 0;
char schema_buffer[SCHEMA_BUFFER_MAX] = {};
size_t schema_buffer_len = 0;
uint8_t active_schema_id = 0;
uint8_t expected_schema_chunk = 0;
uint8_t expected_schema_chunks = 0;
#if BRIDGE_ENABLE_DROP_DETECT
bool have_last_telemetry_seq = false;
uint16_t last_telemetry_seq = 0;
uint32_t last_telemetry_sample_us = 0;
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

void printBinaryPayload(const char* prefix, const void* payload_data, size_t payload_size) {
  const uint8_t* payload_bytes = static_cast<const uint8_t*>(payload_data);
  const uint8_t frame_type =
      (prefix && prefix[0] != '\0') ? BRIDGE_BINARY_TYPE_TX : BRIDGE_BINARY_TYPE_RX;
  uint8_t checksum = frame_type;
  for (size_t i = 0; i < payload_size; ++i) {
    checksum ^= payload_bytes[i];
  }

  const uint8_t header[] = {
    BRIDGE_BINARY_SYNC_0,
    BRIDGE_BINARY_SYNC_1,
    frame_type
  };
  Serial.write(header, sizeof(header));
  Serial.write(payload_bytes, payload_size);
  Serial.write(&checksum, 1);
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
  printBinaryPayload(prefix, &payload, sizeof(payload));
#endif
}

void printTelemetryPayloadFields(const char* prefix, const RCPayload_I16x8_Time_t& payload) {
#if BRIDGE_OUTPUT_MODE_CSV_VERBOSE
  if (prefix && prefix[0] != '\0') {
    Serial.printf("%s:", prefix);
  }
  Serial.printf(
    "seq=%u,sample_us=%lu,value0=%d,value1=%d,value2=%d,value3=%d,value4=%d,value5=%d,value6=%d,value7=%d,flags=%u,reserved1=%u,reserved2=%u\n",
    payload.seq,
    static_cast<unsigned long>(payload.sample_us),
    payload.value[0], payload.value[1], payload.value[2], payload.value[3],
    payload.value[4], payload.value[5], payload.value[6], payload.value[7],
    payload.flags, payload.reserved1, payload.reserved2);
#elif BRIDGE_OUTPUT_MODE_CSV_COMPACT
  if (prefix && prefix[0] != '\0') {
    Serial.printf("%s:", prefix);
  }
  Serial.printf(
    "%u,%lu,%d,%d,%d,%d,%d,%d,%d,%d,%u,%u,%u\n",
    payload.seq,
    static_cast<unsigned long>(payload.sample_us),
    payload.value[0], payload.value[1], payload.value[2], payload.value[3],
    payload.value[4], payload.value[5], payload.value[6], payload.value[7],
    payload.flags, payload.reserved1, payload.reserved2);
#else
  printBinaryPayload(prefix, &payload, sizeof(payload));
#endif
}

#if BRIDGE_ENABLE_DROP_DETECT
void reportTelemetryGap(const RCPayload_I16x8_Time_t& payload) {
  if (have_last_telemetry_seq && payload.sample_us < last_telemetry_sample_us) {
    have_last_telemetry_seq = false;
  }

  if (have_last_telemetry_seq) {
    const uint16_t expected_seq = static_cast<uint16_t>(last_telemetry_seq + 1);
    if (payload.seq != expected_seq) {
      const uint16_t missing_count =
          static_cast<uint16_t>(payload.seq - expected_seq);
      Serial.printf(
        "DROP_DETECTED:last_seq=%u,current_seq=%u,missing=%u\n",
        last_telemetry_seq, payload.seq, missing_count);
    }
  }

  have_last_telemetry_seq = true;
  last_telemetry_seq = payload.seq;
  last_telemetry_sample_us = payload.sample_us;
}
#endif

void printReceivedPayload(const RCPayload_I16x8_Time_t& payload) {
#if BRIDGE_ENABLE_DROP_DETECT && !BRIDGE_OUTPUT_MODE_BINARY
  reportTelemetryGap(payload);
#endif
  printTelemetryPayloadFields("", payload);
}

void onDataReceived(const RCMessage_t& msg) {
  if (msg.type == RCMSG_TYPE_SCHEMA) {
    RCSchemaChunk_t chunk = {};
    msg.copyPayloadTo(chunk);
    if (!schema_queue.push(chunk)) {
      __atomic_add_fetch(&schema_overflow_count, 1, __ATOMIC_RELAXED);
    }
    return;
  }

#ifdef BUILTIN_LED
  if (espnow_controller &&
      espnow_controller->getConnectionState() == RCConnectionState_t::CONNECTED) {
    toggleGPIO(BUILTIN_LED);
  }
#endif

  RCPayload_I16x8_Time_t payload = {};
  msg.copyPayloadTo(payload);
  if (!rx_queue.push(payload)) {
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
      if (serial_line_discarding) {
        serial_line_discarding = false;
        serial_line_len = 0;
        continue;
      }

      if (serial_line_len > 0) {
        serial_line[serial_line_len] = '\0';
        handleSerialLine(serial_line);
        serial_line_len = 0;
      }
      continue;
    }

    if (serial_line_discarding) {
      continue;
    }

    if (serial_line_len < SERIAL_LINE_MAX - 1) {
      serial_line[serial_line_len++] = ch;
    } else {
      serial_line_len = 0;
      serial_line_discarding = true;
      Serial.println("RC_ERROR:line_too_long");
    }
  }
}

void flushReceivedData() {
  uint16_t overflow_count = __atomic_exchange_n(&rx_overflow_count, 0, __ATOMIC_RELAXED);
  if (overflow_count > 0) {
    Serial.printf("RC_ERROR:rx_overflow,%u\n", overflow_count);
  }

  uint16_t schema_overflows = __atomic_exchange_n(&schema_overflow_count, 0, __ATOMIC_RELAXED);
#if !BRIDGE_OUTPUT_MODE_BINARY
  if (schema_overflows > 0) {
    Serial.printf("RC_ERROR:schema_overflow,%u\n", schema_overflows);
  }
#else
  (void)schema_overflows;
#endif

  RCSchemaChunk_t chunk = {};
  while (schema_queue.pop(chunk)) {
#if BRIDGE_OUTPUT_MODE_BINARY
    continue;
#else
    if (chunk.chunk_count == 0 ||
        chunk.chunk_index >= chunk.chunk_count ||
        chunk.text_len > sizeof(chunk.text)) {
      Serial.println("RC_ERROR:bad_schema_chunk");
      active_schema_id = 0;
      expected_schema_chunk = 0;
      expected_schema_chunks = 0;
      schema_buffer_len = 0;
      continue;
    }

    if (chunk.chunk_index == 0) {
      active_schema_id = chunk.schema_id;
      expected_schema_chunk = 0;
      expected_schema_chunks = chunk.chunk_count;
      schema_buffer_len = 0;
      schema_buffer[0] = '\0';
    }

    if (chunk.schema_id != active_schema_id ||
        chunk.chunk_count != expected_schema_chunks ||
        chunk.chunk_index != expected_schema_chunk ||
        schema_buffer_len + chunk.text_len >= SCHEMA_BUFFER_MAX) {
      Serial.println("RC_ERROR:schema_sequence");
      active_schema_id = 0;
      expected_schema_chunk = 0;
      expected_schema_chunks = 0;
      schema_buffer_len = 0;
      continue;
    }

    memcpy(schema_buffer + schema_buffer_len, chunk.text, chunk.text_len);
    schema_buffer_len += chunk.text_len;
    schema_buffer[schema_buffer_len] = '\0';
    expected_schema_chunk++;

    if (expected_schema_chunk == expected_schema_chunks) {
      Serial.printf("RC_SCHEMA:%s\n", schema_buffer);
      active_schema_id = 0;
      expected_schema_chunk = 0;
      expected_schema_chunks = 0;
      schema_buffer_len = 0;
    }
#endif
  }

  RCPayload_I16x8_Time_t payload = {};
  while (rx_queue.pop(payload)) {
    printReceivedPayload(payload);
  }
}

void setup() {
  Serial.begin(230400);
  DELAY(1000);

  Serial.println("RC_STATUS:starting");

#ifdef BUILTIN_LED
  pinMode(BUILTIN_LED, OUTPUT);
#endif

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
