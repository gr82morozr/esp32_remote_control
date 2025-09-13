#include <Arduino.h>
#include "esp32_rc_factory.h"

/*
ESP32 Serial-to-ESPNOW Transparent Bridge

Simple transparent bridge that reads serial input and forwards it via ESPNOW.
- Reads raw serial data 
- Maps to ESPNOW packet format (RCPayload_t)
- No protocol-specific logic
- Bidirectional transparent passthrough
*/

ESP32RemoteControl* espnow_controller = nullptr;

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
    espnow_controller->setOnRecieveMsgHandler(onDataReceived);  // Register callback
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
      
      // Parse CSV values
      int field_count = 0;
      int start_index = 0;
      
      for (int i = 0; i <= line.length(); i++) {
        if (i == line.length() || line[i] == ',') {
          String field = line.substring(start_index, i);
          
          switch (field_count) {
            case 0: payload.id1 = field.toInt(); break;
            case 1: payload.id2 = field.toInt(); break;
            case 2: payload.id3 = field.toInt(); break;
            case 3: payload.id4 = field.toInt(); break;
            case 4: payload.value1 = field.toFloat(); break;
            case 5: payload.value2 = field.toFloat(); break;
            case 6: payload.value3 = field.toFloat(); break;
            case 7: payload.value4 = field.toFloat(); break;
            case 8: payload.value5 = field.toFloat(); break;
            case 9: payload.flags = field.toInt(); break;
          }
          field_count++;
          start_index = i + 1;
        }
      }
      
      // Send via ESPNOW if we got at least the basic fields
      if (field_count >= 6) {
        if (espnow_controller->sendData(payload)) {
          // Output structured text data with unique flag for sent data
          Serial.printf("RC_SENT:%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%d\n",
            payload.id1, payload.id2, payload.id3, payload.id4,
            payload.value1, payload.value2, payload.value3, payload.value4, payload.value5,
            payload.flags);
        }
      }
    }
  }
  
  // Incoming ESPNOW data is now handled by callback (onDataReceived)
  // No need for polling recvData() anymore
  
  DELAY(1);
}