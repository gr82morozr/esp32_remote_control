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



#define ESP32_RC_PROTOCOL ESP32_RC_ESPNOW

ESP32_RC_PROTOCOL* controller = nullptr;  // Declare globally


unsigned long last_data_send_ms = 0;
RCPayload_t Outgoing = {0};
RCPayload_t incoming;
void setup() {
  Serial.begin(115200);
  controller = new ESP32_RC_PROTOCOL(false);  // Initialize inside setup
  pinMode(BUILTIN_LED, OUTPUT);
  LOG("ESP32_RC Example");
  DELAY(1000);
  LOG("Starting ESP32_RC demo - Protocol: %s ", String(controller->getProtocol()));
  
  // Global metrics control (affects all controller instances)
  ESP32_RC_PROTOCOL::enableGlobalMetrics(true);  // Enable metrics calculation
  // ESP32_RC_PROTOCOL::disableGlobalMetrics();  // Uncomment to disable metrics
  
  // Enable automatic metrics display every 1 second
  controller->enableMetricsDisplay(true, 1000);
  
  // Init the controller
  controller->connect();
}


void loop() {
  // Send data
  Outgoing.value1 = millis() / 1000.0f;  // Update value1 with current time in seconds
  controller->sendData(Outgoing);  // Send the data
  
  // Receive data (but don't print it - metrics will show success/failure)
  if (controller->recvData(incoming)) {
    // Data received successfully - metrics automatically tracked
    // Optional: Toggle LED or other indicator
    toggleGPIO(BUILTIN_LED);
  }
  
  // Print metrics automatically every second (handled by base class)
  controller->printMetrics();
  
  // Small delay to avoid overwhelming the system
  DELAY(5);
}