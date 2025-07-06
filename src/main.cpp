#include <Arduino.h>



#include "config.h"
#include "esp32_rc_espnow.h"
//#include "esp32_rc_nrf24.h"


ESP32_RC_ESPNOW* controller = nullptr;

unsigned long last_heartbeat_ms = 0;
unsigned long last_data_send_ms = 0;

void setup() {
  Serial.begin(115200);


  Serial.println("ESP32_RC_ESPNOW Example");
  delay(1000);

  Serial.println("Starting ESP32_RC_ESPNOW demo");

  // Init the controller
 
  controller = new ESP32_RC_ESPNOW(false);  // Now safe
  controller->connect();
}



void loop() {
 
  // Send test data every 3000 ms
  if (millis() - last_data_send_ms > 3000) {
    RCPayload_t payload = {
        .id1 = 1, .id2 = 2, .id3 = 3, .id4 = 4,
        .value1 = 10.1f, .value2 = 20.2f,
        .value3 = 30.3f, .value4 = 40.4f,
        .flags = 0xA5
    };
    controller->sendData(payload);
    Serial.println("Sent test data");
    last_data_send_ms = millis();
  }

  // Try to receive a message
  RCPayload_t incoming;
  if (controller->recvData(incoming)) {
    Serial.printf("RECEIVED data: id1=%d val1=%.2f id2=%d val2=%.2f\n",
                  incoming.id1, incoming.value1,
                  incoming.id2, incoming.value2);
  }
}