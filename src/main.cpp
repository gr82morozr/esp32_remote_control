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
  
  // Forward raw payload bytes to serial
  const uint8_t* raw_data = reinterpret_cast<const uint8_t*>(payload);
  Serial.write(raw_data, sizeof(RCPayload_t));
  
  Serial.print("Received: ");
  for (size_t i = 0; i < sizeof(RCPayload_t); i++) {
    Serial.printf("%02X ", raw_data[i]);
  }
  Serial.println();
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
    // Read serial data into buffer
    uint8_t buffer[25];  // RCPayload_t size
    size_t bytes_read = 0;
    
    // Read up to 25 bytes from serial
    while (Serial.available() && bytes_read < sizeof(buffer)) {
      buffer[bytes_read] = Serial.read();
      bytes_read++;
    }
    
    if (bytes_read > 0) {
      // Map serial data to RCPayload_t structure
      RCPayload_t payload = {0};
      
      // Simple mapping: copy bytes directly to payload structure
      if (bytes_read >= sizeof(RCPayload_t)) {
        memcpy(&payload, buffer, sizeof(RCPayload_t));
      } else {
        // If less data, copy what we have
        memcpy(&payload, buffer, bytes_read);
      }
      
      // Send via ESPNOW
      if (espnow_controller->sendData(payload)) {
        Serial.print("Sent: ");
        for (size_t i = 0; i < bytes_read; i++) {
          Serial.printf("%02X ", buffer[i]);
        }
        Serial.println();
      }
    }
  }
  
  // Incoming ESPNOW data is now handled by callback (onDataReceived)
  // No need for polling recvData() anymore
  
  delay(1);
}