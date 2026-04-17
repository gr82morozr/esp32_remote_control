#include <Arduino.h>
#include "esp32_rc_factory.h"

/*
ESP32 Serial-to-ESPNOW CSV Bridge

Simple bridge that reads CSV serial input and forwards it via ESPNOW.
- Reads newline-terminated CSV packets
- Maps to ESPNOW packet format (RCPayload_t)
- Bidirectional structured text passthrough
*/

ESP32RemoteControl* espnow_controller = nullptr;

bool parseUInt8Field(String field, uint8_t& value) {
  field.trim();
  if (field.length() == 0) return false;

  char* end = nullptr;
  long parsed = strtol(field.c_str(), &end, 10);
  if (end == field.c_str() || *end != '\0') return false;
  if (parsed < 0 || parsed > 255) return false;

  value = static_cast<uint8_t>(parsed);
  return true;
}

bool parseFloatField(String field, float& value) {
  field.trim();
  if (field.length() == 0) return false;

  char* end = nullptr;
  float parsed = strtof(field.c_str(), &end);
  if (end == field.c_str() || *end != '\0') return false;

  value = parsed;
  return true;
}

bool parsePayloadCsv(const String& line, RCPayload_t& payload) {
  int field_count = 0;
  int start_index = 0;

  for (int i = 0; i <= line.length(); i++) {
    if (i == line.length() || line[i] == ',') {
      if (field_count >= 10) return false;

      String field = line.substring(start_index, i);
      bool ok = false;

      switch (field_count) {
        case 0: ok = parseUInt8Field(field, payload.id1); break;
        case 1: ok = parseUInt8Field(field, payload.id2); break;
        case 2: ok = parseUInt8Field(field, payload.id3); break;
        case 3: ok = parseUInt8Field(field, payload.id4); break;
        case 4: ok = parseFloatField(field, payload.value1); break;
        case 5: ok = parseFloatField(field, payload.value2); break;
        case 6: ok = parseFloatField(field, payload.value3); break;
        case 7: ok = parseFloatField(field, payload.value4); break;
        case 8: ok = parseFloatField(field, payload.value5); break;
        case 9: ok = parseUInt8Field(field, payload.flags); break;
      }

      if (!ok) return false;

      field_count++;
      start_index = i + 1;
    }
  }

  return field_count == 10;
}

// Callback function for handling received ESPNOW data
void onDataReceived(const RCMessage_t& msg) {
  // Extract payload from message
  const RCPayload_t* payload = msg.getPayload();
  
  // Output structured text data with unique flag
  Serial.printf("RC_DATA:%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%d\n",
    payload->id1, payload->id2, payload->id3, payload->id4,
    payload->value1, payload->value2, payload->value3, payload->value4, payload->value5,
    payload->flags);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("ESP32 Serial-to-ESPNOW Bridge Starting...");
  
  // Create ESPNOW controller instance
  espnow_controller = createProtocolInstance(RC_PROTO_ESPNOW, false);
  
  if (espnow_controller) {
    espnow_controller->enableMetricsDisplay(false);
    espnow_controller->setOnReceiveMsgHandler(onDataReceived);  // Register callback
    espnow_controller->connect();
    Serial.println("ESPNOW controller initialized with callback");
  } else {
    Serial.println("Failed to initialize ESPNOW controller");
    return;
  }
}

void loop() {
  if (!espnow_controller) {
    delay(1000);
    return;
  }
  
  // Read serial input and forward to ESPNOW
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    
    if (line.length() > 0) {
      // Parse CSV format: id1,id2,id3,id4,value1,value2,value3,value4,value5,flags
      RCPayload_t payload = {0};
      
      if (!parsePayloadCsv(line, payload)) {
        Serial.println("RC_ERROR:bad_csv");
        return;
      }
      
      if (espnow_controller->sendData(payload)) {
        // Output structured text data with unique flag for sent data
        Serial.printf("RC_SENT:%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%d\n",
          payload.id1, payload.id2, payload.id3, payload.id4,
          payload.value1, payload.value2, payload.value3, payload.value4, payload.value5,
          payload.flags);
      }
    }
  }
  
  // Incoming ESPNOW data is now handled by callback (onDataReceived)
  // No need for polling recvData() anymore
  
  delay(1);
}
