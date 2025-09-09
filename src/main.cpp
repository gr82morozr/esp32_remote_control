#include <Arduino.h>
#include "esp32_rc_factory.h"

/*
Mock Receiver - LED Control Demo

This firmware demonstrates the ESP32 Remote Control library by creating a simple
receiver that blinks the built-in LED based on commands from the PC Serial Bridge.

Features:
- Receives data via ESP-NOW or NRF24L01+
- Blinks LED based on received values
- Prints received data to Serial console
- Automatic protocol detection
- Configurable LED behaviors

Hardware Required:
- ESP32 development board
- Built-in LED (or external LED on GPIO 2)
- Optional: NRF24L01+ module for NRF24 testing

Usage:
1. Flash this firmware to a second ESP32 (different from bridge device)
2. Power both devices
3. Use PC Serial Bridge + Python client to send commands
4. Watch LED blink patterns and serial output

LED Behaviors:
- v1 > 50: Fast blink (100ms on/off)
- v1 20-50: Medium blink (300ms on/off) 
- v1 < 20: Slow blink (800ms on/off)
- flags & 1: LED stays ON
- flags & 2: LED stays OFF
- id1: Controls blink count in burst mode
*/

// =============================================================================
// Configuration
// =============================================================================

// Protocol selection - choose one protocol to compile with
#define MOCK_PROTOCOL RC_PROTO_ESPNOW    // or RC_PROTO_NRF24

// LED configuration
#define LED_PIN BUILTIN_LED               // Use built-in LED
#define LED_ACTIVE_LOW true               // Most ESP32 boards have active-low LED

// Debug output control
#define ENABLE_SERIAL_OUTPUT true         // Print received data to Serial
#define ENABLE_LED_CONTROL true           // Control LED based on received data

// =============================================================================
// Global Variables
// =============================================================================

ESP32RemoteControl* controller = nullptr;
RCPayload_t last_received_data = {0};

// LED control variables
unsigned long last_led_toggle = 0;
unsigned long led_blink_interval = 500;  // Default: 500ms (slow blink)
bool led_state = false;
bool led_override_on = false;
bool led_override_off = false;
int burst_blinks_remaining = 0;
unsigned long burst_blink_start = 0;

// Statistics
unsigned long total_packets_received = 0;
unsigned long last_packet_time = 0;
unsigned long connection_start_time = 0;

void printReceivedData(const RCPayload_t& data); 
// =============================================================================
// LED Control Functions
// =============================================================================

void writeGPIO(bool state) {
    if (LED_ACTIVE_LOW) {
        digitalWrite(LED_PIN, !state);  // For active-low: invert state (true->LOW, false->HIGH)
    } else {
        digitalWrite(LED_PIN, state);   // For active-high: direct state (true->HIGH, false->LOW)
    }
    led_state = state;
}

void updateLEDBehavior(const RCPayload_t& data) {
    if (!ENABLE_LED_CONTROL) return;
    
    // Check flag overrides first
    if (data.flags & 2) {
        // Flag bit 1: Force LED OFF
        led_override_off = true;
        led_override_on = false;
        writeGPIO(true);  // Inverted: call true for OFF
        return;
    } else if (data.flags & 1) {
        // Flag bit 0: Force LED ON
        led_override_on = true;
        led_override_off = false;
        writeGPIO(false);  // Inverted: call false for ON
        return;
    } else {
        // Clear overrides - return to normal blinking
        led_override_on = false;
        led_override_off = false;
    }
    
    // Set blink interval based on v1 value
    if (data.value1 > 50.0f) {
        led_blink_interval = 100;  // Fast blink
    } else if (data.value1 > 20.0f) {
        led_blink_interval = 300;  // Medium blink
    } else {
        led_blink_interval = 800;  // Slow blink
    }
    
    // Burst mode: blink N times based on id1
    if (data.id1 > 0 && burst_blinks_remaining == 0) {
        burst_blinks_remaining = data.id1 * 2;  // *2 for on/off cycles
        burst_blink_start = millis();
        led_blink_interval = 100;  // Fast blinks for burst
    }
}

void handleLEDBlinking() {
    if (!ENABLE_LED_CONTROL) return;
    
    // Check connection state - LED should be off when not connected
    if (!controller || controller->getConnectionState() != RCConnectionState_t::CONNECTED) {
        writeGPIO(false);
        return;
    }
    
    // Check if we have recent data (within last 5 seconds)
    unsigned long now = millis();
    if (last_packet_time == 0 || (now - last_packet_time) > 5000) {
        writeGPIO(false);  // No recent data, turn off LED
        return;
    }
    
    // Handle LED overrides
    if (led_override_on || led_override_off) {
        return;  // LED state is fixed, no blinking
    }
    
    // Handle burst mode
    if (burst_blinks_remaining > 0) {
        if (now - last_led_toggle >= 100) {  // Fast burst blinks
            writeGPIO(!led_state);
            last_led_toggle = now;
            burst_blinks_remaining--;
        }
        return;
    }
    
    // Normal blinking mode
    if (now - last_led_toggle >= led_blink_interval) {
        writeGPIO(!led_state);
        last_led_toggle = now;
    }
}

// =============================================================================
// Data Processing Functions
// =============================================================================

void processReceivedData(const RCPayload_t& data) {
    last_received_data = data;
    total_packets_received++;
    last_packet_time = millis();
    
    // Update LED behavior based on received data
    updateLEDBehavior(data);
    
    // Print received data to Serial console
    if (ENABLE_SERIAL_OUTPUT) {
        printReceivedData(data);
    }
}

void printReceivedData(const RCPayload_t& data) {
    Serial.printf("[%lu] RECEIVED DATA:\n", millis());
    Serial.printf("  IDs: %d, %d, %d, %d\n", data.id1, data.id2, data.id3, data.id4);
    Serial.printf("  Values: %.2f, %.2f, %.2f, %.2f, %.2f\n", 
                 data.value1, data.value2, data.value3, data.value4, data.value5);
    Serial.printf("  Flags: 0x%02X (%d)\n", data.flags, data.flags);
    
    // Interpret flags for user
    String flag_meaning = "Flags: ";
    if (data.flags & 1) flag_meaning += "LED_ON ";
    if (data.flags & 2) flag_meaning += "LED_OFF ";
    if (data.flags & 4) flag_meaning += "FLAG_2 ";
    if (data.flags & 8) flag_meaning += "FLAG_3 ";
    if (data.flags == 0) flag_meaning += "NORMAL_BLINK";
    Serial.printf("  %s\n", flag_meaning.c_str());
    
    // LED behavior feedback
    if (data.flags & 2) {
        Serial.println("  LED: FORCED OFF");
    } else if (data.flags & 1) {
        Serial.println("  LED: FORCED ON");
    } else if (data.id1 > 0) {
        Serial.printf("  LED: BURST BLINK x%d\n", data.id1);
    } else {
        String blink_speed = (data.value1 > 50) ? "FAST" : 
                           (data.value1 > 20) ? "MEDIUM" : "SLOW";
        Serial.printf("  LED: %s BLINK (%.0f%%)\n", blink_speed.c_str(), data.value1);
    }
    
    Serial.printf("  Total packets: %lu, Protocol: %s\n\n", 
                 total_packets_received, 
                 protocolToString(controller->getProtocol()));
}

void printStatistics() {
    static unsigned long last_stats_print = 0;
    unsigned long now = millis();
    
    // Print stats every 10 seconds
    if (now - last_stats_print >= 10000) {
        Serial.println("=== RECEIVER STATISTICS ===");
        Serial.printf("Protocol: %s\n", protocolToString(controller->getProtocol()));
        Serial.printf("Connection State: %s\n", 
                     controller->getConnectionState() == RCConnectionState_t::CONNECTED ? "CONNECTED" :
                     controller->getConnectionState() == RCConnectionState_t::CONNECTING ? "CONNECTING" :
                     controller->getConnectionState() == RCConnectionState_t::DISCONNECTED ? "DISCONNECTED" : "ERROR");
        Serial.printf("Total Packets Received: %lu\n", total_packets_received);
        Serial.printf("Uptime: %.1f seconds\n", now / 1000.0);
        if (last_packet_time > 0) {
            Serial.printf("Last Packet: %.1f seconds ago\n", (now - last_packet_time) / 1000.0);
        }
        
        // Print current LED state
        Serial.printf("LED State: %s", led_state ? "ON" : "OFF");
        if (led_override_on) Serial.print(" (FORCED ON)");
        else if (led_override_off) Serial.print(" (FORCED OFF)");
        else if (burst_blinks_remaining > 0) Serial.printf(" (BURST %d remaining)", burst_blinks_remaining);
        else Serial.printf(" (BLINK %lums)", led_blink_interval);
        Serial.println();
        
        Serial.println("===========================\n");
        last_stats_print = now;
    }
}

// =============================================================================
// Setup & Main Loop
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    // Initialize LED pin
    pinMode(LED_PIN, OUTPUT);
    writeGPIO(false);  // Start with LED off - will stay off until connected and receiving data
    
    // Startup banner
    Serial.println("========================================");
    Serial.println("ESP32 Remote Control - Mock Receiver");
    Serial.println("LED Control Demo");
    Serial.println("========================================");
    Serial.printf("Protocol: %s\n", protocolToString(MOCK_PROTOCOL));
    Serial.printf("LED Pin: GPIO %d (%s)\n", LED_PIN, 
                 LED_ACTIVE_LOW ? "Active Low" : "Active High");
    Serial.println();
    
    // Check if selected protocol is available at compile time
    if (!isProtocolAvailable(MOCK_PROTOCOL)) {
        Serial.printf("❌ Protocol %s not available (not compiled in)\n", protocolToString(MOCK_PROTOCOL));
        Serial.println("Check ENABLE_ESP32_RC_* macros in esp32_rc_user_config.h");
        while(1) delay(1000);  // Halt
    }
    
    // Initialize controller with selected protocol using factory function
    controller = createProtocolInstance(MOCK_PROTOCOL, false);  // Reliable mode
    
    if (controller) {
        Serial.printf("Controller initialized: %s\n", protocolToString(controller->getProtocol()));
        
        // Disable metrics display to keep output clean
        controller->enableMetricsDisplay(false);
        
        // Connect to start listening
        controller->connect();
        connection_start_time = millis();
        
        Serial.println("Listening for remote control data...");
        Serial.println("Commands from PC Serial Bridge:");
        Serial.println("- v1 > 50: Fast LED blink (100ms)");
        Serial.println("- v1 20-50: Medium LED blink (300ms)"); 
        Serial.println("- v1 < 20: Slow LED blink (800ms)");
        Serial.println("- flags & 1: LED ON");
        Serial.println("- flags & 2: LED OFF");
        Serial.println("- id1 > 0: Burst blink N times");
        Serial.println();
    } else {
        Serial.println("❌ Failed to initialize controller!");
        while(1) delay(1000);  // Halt on failure
    }
}

void loop() {
    RCPayload_t received_data;
    
    // Check for incoming data
    if (controller && controller->recvData(received_data)) {
        processReceivedData(received_data);
    }
    
    // Handle LED blinking behavior
    handleLEDBlinking();
    
    // Print periodic statistics  
    printStatistics();
    
    // Small delay to prevent overwhelming the system
    delay(1);
}