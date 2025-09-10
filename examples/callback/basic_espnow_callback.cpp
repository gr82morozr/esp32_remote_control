#include <Arduino.h>

/*
ESP32 Remote Control - Basic ESP-NOW Example with Callbacks

This example demonstrates how to use the ESP32RemoteControl library with callback functions
instead of polling for received data. The callback approach is more efficient and responsive.

Key differences from basic_espnow.cpp:
- Uses setOnRecieveMsgHandler() to register a callback function
- Data is processed automatically when received (no polling needed)
- More responsive to incoming data
- Cleaner separation of send/receive logic

The same dynamic dummy data generation is used to demonstrate the data flow.

For peer-to-peer communication, you can use either NRF24 or ESPNOW.
The exact same code works for both protocols, just change the protocol definition.

fast_mode:
- false (default): Messages queued and sent in background (queue size: 10 messages)  
- true: Messages sent immediately

RCMessage_t structure:
- type: Message type (RCMSG_TYPE_DATA or RCMSG_TYPE_HEARTBEAT)
- from_addr: Sender MAC address
- payload: RCPayload_t data (id1-id4, value1-value5, flags)
*/

// PROTOCOL OVERRIDE: Define before including headers to override user_config.h default
#define ESP32_RC_PROTOCOL RC_PROTO_ESPNOW

#include "esp32_rc_factory.h"

ESP32RemoteControl* controller = nullptr;  // Declare globally

RCPayload_t outgoing = {0};
unsigned long messages_received = 0;
unsigned long last_message_time = 0;

/**
 * @brief Callback function for received messages
 * 
 * This function is automatically called whenever a message is received.
 * It processes the incoming RCMessage_t and extracts the payload data.
 * 
 * @param msg The received message containing type, from_addr, and payload
 */
void onMessageReceived(const RCMessage_t& msg) {
    toggleGPIO(BUILTIN_LED);
}

/**
 * @brief Callback function for device discovery
 * 
 * Called when another ESP32 device is discovered on the network.
 * This enables automatic peer detection and connection.
 * 
 * @param result Discovery result containing peer address and status
 */
void onDeviceDiscovered(const RCDiscoveryResult_t& result) {
    if (result.discovered) {
        LOG("Device discovered: %02X:%02X:%02X:%02X:%02X:%02X",
            result.peer_addr[0], result.peer_addr[1], result.peer_addr[2],
            result.peer_addr[3], result.peer_addr[4], result.peer_addr[5]);
        LOG("Peer connection established");
    }
}

/**
 * @brief Populate RCPayload_t with dynamic dummy data for testing
 * @param payload Reference to payload structure to fill
 * 
 * Generates realistic test data that changes over time:
 * - id1-id4: Rotating counter values (0-255)
 * - value1: Time in seconds (float)
 * - value2: Sine wave (-1000.0 to +1000.0)
 * - value3: Random voltage (0.0 to 5.0V)
 * - value4: Temperature simulation (10.0 to 30.0°C)
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
    LOG("ESP32_RC Callback Example");
    DELAY(1000);
    LOG("Starting ESP32_RC callback demo - Protocol: %s", protocolToString(controller->getProtocol()));
    
    // Global metrics control (affects all controller instances)
    ESP32RemoteControl::enableGlobalMetrics(true);  // Enable metrics calculation
    
    // Enable automatic metrics display every 2 seconds (less frequent for callback version)
    controller->enableMetricsDisplay(true, 2000);
    
    // REGISTER CALLBACKS - This is the key difference from the basic example!
    controller->setOnRecieveMsgHandler(onMessageReceived);
    controller->setOnDiscoveryHandler(onDeviceDiscovered);
    
    LOG("Callbacks registered:");
    LOG("- Message reception callback: ACTIVE");
    LOG("- Device discovery callback: ACTIVE");
    
    // Initialize the controller
    controller->connect();
    
    LOG("Ready to send/receive data via callbacks...");
}

void loop() {
    // Populate and send outgoing packet with dynamic dummy data
    populateDummyData(outgoing);
    controller->sendData(outgoing);  // Send the data
    
    // NO NEED to call recvData() - callbacks handle incoming data automatically!
    // This makes the loop much simpler and more efficient

    // Print metrics automatically (handled by base class)
    controller->printMetrics();
    
    // Small delay to avoid overwhelming the system
    DELAY(5);
}