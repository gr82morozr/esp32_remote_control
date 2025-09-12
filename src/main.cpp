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

// PROTOCOL OVERRIDE: Define before including headers to override user_config.h default
#define ESP32_RC_PROTOCOL RC_PROTO_ESPNOW

#include "esp32_rc_factory.h"

ESP32RemoteControl* controller = nullptr;  // Declare globally


unsigned long last_data_send_ms = 0;
RCPayload_t outgoing = {0};
RCPayload_t incoming;

/**
 * @brief Populate RCPayload_t with dynamic dummy data for testing
 * @param payload Reference to payload structure to fill
 * 
 * Generates realistic test data that changes over time:
 * - id1-id4: Rotating counter values (0-255)
 * - value1: Time in seconds (float)
 * - value2: Sine wave (-1.0 to +1.0)
 * - value3: Random voltage (0.0 to 5.0V)
 * - value4: Temperature simulation (15.0 to 35.0°C)
 * - value5: Rotating percentage (0.0 to 100.0%)
 * - flags: Bit pattern with rotating bits
 */
void populateDummyData(RCPayload_t& payload) {
  static uint32_t counter = 0;
  counter++;
  
  // Time-based calculations
  float time_sec = millis() / 1000.0f;
  float phase = time_sec * 0.1f;  // Slow phase for sine wave
  
  // ID fields - rotating counters with different rates
  payload.id1 = (counter / 10) % 256;        // Slow counter
  payload.id2 = (counter / 5) % 256;         // Medium counter  
  payload.id3 = counter % 256;               // Fast counter
  payload.id4 = (counter * 3) % 256;         // Fast counter with multiplier
  
  // Value fields - dynamic floating point data
  payload.value1 = time_sec;                 // Current time in seconds
  payload.value2 = sin(phase) * 1000.0f;     // Sine wave oscillation (±1000)
  payload.value3 = (random(0, 5000) / 1000.0f); // Random voltage 0.0-5.0V
  payload.value4 = 20.0f + sin(phase * 2) * 10.0f; // Temperature 10-30°C
  payload.value5 = (counter % 1000) / 10.0f; // Percentage 0.0-100.0%
  
  // Flags field - rotating bit patterns
  uint8_t bit_pos = counter % 8;
  payload.flags = (1 << bit_pos) | (counter & 0x0F); // Rotating bit + counter
}
void setup() {
  Serial.begin(115200);
  
  // Check if protocol is available at compile time
  if (!isProtocolAvailable(ESP32_RC_PROTOCOL)) {
    LOG_ERROR("Protocol %s not available (not compiled in)", protocolToString(ESP32_RC_PROTOCOL));
    LOG_ERROR("Check ESP32_RC_PROTOCOL macro in esp32_rc_user_config.h");
    SYS_HALT;
  }
  
  // Create controller using factory function
  controller = createProtocolInstance(ESP32_RC_PROTOCOL, true);  // Fast mode
  if (!controller) {
    LOG_ERROR("Failed to create protocol instance");
    SYS_HALT;
  }
  
  pinMode(BUILTIN_LED, OUTPUT);
  LOG("ESP32_RC Example");
  DELAY(1000);
  LOG("Starting ESP32_RC demo - Protocol: %s ", protocolToString(controller->getProtocol()));
  
  // Global metrics control (affects all controller instances)
  ESP32RemoteControl::enableGlobalMetrics(true);  // Enable metrics calculation
  // ESP32RemoteControl::disableGlobalMetrics();  // Uncomment to disable metrics
  
  // Enable automatic metrics display every 1 second
  controller->enableMetricsDisplay(true, 1000);
  
  // Init the controller
  controller->connect();
}


void loop() {
  // Populate outgoing packet with dynamic dummy data
  populateDummyData(outgoing);
  controller->sendData(outgoing);  // Send the data
  
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