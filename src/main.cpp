#include <Arduino.h>

/*
This library is an example of how to use the ESP32RemoteControl library
there are two protocols implemented: NRF24 and ESPNOW

For peer-to-peer communication, you can use either NRF24 or ESPNOW.
the exact same code works for both nodes, just change the protocol in the constructor.
For example, to use NRF24, you would do:
  controller = new ESP32_RC_ESPNOW(false);
  controller = new ESP32_RC_NRF24(false); 
  
  
  fast_mode is set to false by default, it measn the message will be queued and sent in the background. (queue size is 10 messages by default)
  fast_mode is set to true, it means the message will be sent immediately

  RCMessage_t is the message structure used to send data between the nodes. It supports up to 10 channels of data.
  
  RCMessage_t has the following structure:
  struct RCMessage_t {  
    uint8_t type;  // Message type (RCMSG_TYPE_DATA or RCMSG_TYPE_HEARTBEAT)
    uint8_t from_addr[RC_ADDR_SIZE];  // Sender address
    uint8_t to_addr[RC_ADDR_SIZE];    // Receiver address
    RCPayload_t payload;               // Payload data
  };


*/


#include "esp32_rc_espnow.h"
#include "esp32_rc_nrf24.h"
#include "esp32_rc_wifi.h"

//#define ESP32_RC_PROTOCOL ESP32_RC_NRF24
//#define ESP32_RC_PROTOCOL ESP32_RC_ESPNOW
#define ESP32_RC_PROTOCOL ESP32_RC_WIFI

ESP32_RC_PROTOCOL* controller = new ESP32_RC_PROTOCOL(true);

unsigned long last_data_send_ms = 0;

void setup() {
  Serial.begin(115200);


  LOG("ESP32_RC Example");
  delay(1000);

  LOG("Starting ESP32_RC demo - Protocol: %s ", String(controller->getProtocol()));

  // Init the controller
  controller->connect();
}



void loop() {
 
  // Send test data every 3000 ms
  if (millis() - last_data_send_ms > 300) {
    RCPayload_t payload = {
        .id1 = 1, .id2 = 2, .id3 = 3, .id4 = 4,
        .value1 = 10.1f, .value2 = 20.2f,
        .value3 = 30.3f, .value4 = last_data_send_ms/1000.0f,
        .flags = 0xA5
    };
    controller->sendData(payload);
    Serial.println("Sent test data");
    last_data_send_ms = millis();
  }

  // Try to receive a message
  RCPayload_t incoming;
  if (controller->recvData(incoming)) {
    Serial.printf("RECEIVED data: id1=%d val1=%.2f id2=%d val2=%.2f val3=%.2f val4=%.2f\n",
                  incoming.id1, incoming.value1,
                  incoming.id2, incoming.value2, incoming.value3, incoming.value4);
  }
}