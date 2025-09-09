#include <Arduino.h>
#include "esp32_rc_factory.h"

/*
ESP32 Keyboard Remote Control Receiver
======================================

This firmware receives keyboard commands from the PC Serial Bridge and provides
feedback by echoing the commands to its serial output. It demonstrates the 
keyboard remote control system and can be used as a base for robot control.

Features:
- Receives keyboard commands via ESP-NOW or NRF24L01+
- Interprets robot movement commands (Forward, Backward, Turn Left/Right, Stop)
- Echoes received commands to Serial console with timestamps
- Provides human-readable command interpretation
- LED feedback for received commands
- Connection status monitoring

Hardware Required:
- ESP32 development board
- Built-in LED (or external LED on GPIO 2)
- Optional: NRF24L01+ module for NRF24 testing

Usage:
1. Flash this firmware to ESP32 robot/receiver device
2. Power both bridge and receiver devices
3. Use Python robot_controller.py to send keyboard commands
4. Watch command feedback on receiver's serial monitor

Command Interpretation:
- value1 = 0: STOP
- value1 = 1: FORWARD  
- value1 = 2: BACKWARD
- value1 = 3: TURN_LEFT
- value1 = 4: TURN_RIGHT
- value2 = speed percentage (0-100)
- value3 = turn rate in degrees/second
*/

// =============================================================================
// Configuration
// =============================================================================

// Protocol selection - choose one protocol to compile with
#define RECEIVER_PROTOCOL RC_PROTO_ESPNOW    // or RC_PROTO_NRF24

// LED configuration for command feedback
#define LED_PIN BUILTIN_LED               // Use built-in LED
#define LED_ACTIVE_LOW true               // Most ESP32 boards have active-low LED

// Debug output control
#define ENABLE_COMMAND_ECHO true          // Echo keyboard commands to Serial
#define ENABLE_LED_FEEDBACK true          // Visual LED feedback for commands

// =============================================================================
// Command Definitions (matching Python controller)
// =============================================================================

enum RobotCommand {
    CMD_STOP = 0,        // Robot stops
    CMD_FORWARD = 1,     // Robot moves forward
    CMD_BACKWARD = 2,    // Robot moves backward  
    CMD_TURN_LEFT = 3,   // Robot turns left
    CMD_TURN_RIGHT = 4   // Robot turns right
};

// =============================================================================
// Global Variables
// =============================================================================

ESP32RemoteControl* controller = nullptr;
RCPayload_t last_command = {0};

// Command feedback variables
unsigned long last_command_time = 0;
unsigned long command_count = 0;
unsigned long connection_start_time = 0;

// LED feedback variables
unsigned long led_on_time = 0;
bool led_feedback_active = false;
const unsigned long LED_FEEDBACK_DURATION = 200; // LED on duration per command (ms)

// =============================================================================
// Function Declarations
// =============================================================================
void setLED(bool state);
void triggerLEDFeedback();
void handleLEDFeedback();
String getCommandName(int command_value);
String getCommandDirection(int command_value);
void processKeyboardCommand(const RCPayload_t& data);
void echoCommandToSerial(const RCPayload_t& data);
void printConnectionStats();

// =============================================================================
// LED Feedback Functions
// =============================================================================

void setLED(bool state) {
    if (LED_ACTIVE_LOW) {
        digitalWrite(LED_PIN, !state);  // Invert for active-low LED
    } else {
        digitalWrite(LED_PIN, state);
    }
}

void triggerLEDFeedback() {
    if (!ENABLE_LED_FEEDBACK) return;
    
    led_feedback_active = true;
    led_on_time = millis();
    setLED(true);
}

void handleLEDFeedback() {
    if (!ENABLE_LED_FEEDBACK) return;
    
    // Turn off LED after feedback duration
    if (led_feedback_active && (millis() - led_on_time >= LED_FEEDBACK_DURATION)) {
        setLED(false);
        led_feedback_active = false;
    }
}

// =============================================================================
// Command Processing Functions
// =============================================================================

String getCommandName(int command_value) {
    switch (command_value) {
        case CMD_STOP: return "STOP";
        case CMD_FORWARD: return "FORWARD";
        case CMD_BACKWARD: return "BACKWARD";
        case CMD_TURN_LEFT: return "TURN LEFT";
        case CMD_TURN_RIGHT: return "TURN RIGHT";
        default: return "UNKNOWN";
    }
}

String getCommandDirection(int command_value) {
    switch (command_value) {
        case CMD_STOP: return "[STOP]";
        case CMD_FORWARD: return "[UP]";
        case CMD_BACKWARD: return "[DOWN]";
        case CMD_TURN_LEFT: return "[LEFT]";
        case CMD_TURN_RIGHT: return "[RIGHT]";
        default: return "[?]";
    }
}

void processKeyboardCommand(const RCPayload_t& data) {
    last_command = data;
    command_count++;
    last_command_time = millis();
    
    // Trigger LED feedback for received command
    triggerLEDFeedback();
    
    // Echo command to Serial console
    if (ENABLE_COMMAND_ECHO) {
        echoCommandToSerial(data);
    }
}

void echoCommandToSerial(const RCPayload_t& data) {
    // Get timestamp
    unsigned long now = millis();
    
    // Extract command information
    int command = (int)data.value1;
    float speed = data.value2;
    float turn_rate = data.value3;
    
    String command_name = getCommandName(command);
    String direction = getCommandDirection(command);
    
    // Get timestamp for command (similar to Python controller format)
    String timestamp = String(now / 1000) + "." + String((now % 1000), 3);
    while (timestamp.indexOf('.') + 4 < timestamp.length()) {
        timestamp.remove(timestamp.length() - 1);
    }
    
    // Print concise command feedback (similar to Python controller)
    Serial.printf("[%s] <- %s", timestamp.c_str(), command_name.c_str());
    
    // Add parameter info for movement commands
    if (command == CMD_FORWARD || command == CMD_BACKWARD) {
        Serial.printf(" (%.0f%%)", speed);
    } else if (command == CMD_TURN_LEFT || command == CMD_TURN_RIGHT) {
        Serial.printf(" (%.0f°/s)", turn_rate);
    }
    
    // Show simple robot action
    Serial.print(" -> ");
    switch (command) {
        case CMD_STOP:
            Serial.println("Robot STOPPED");
            break;
        case CMD_FORWARD:
            Serial.printf("Moving FORWARD at %.0f%% speed\n", speed);
            break;
        case CMD_BACKWARD:
            Serial.printf("Moving BACKWARD at %.0f%% speed\n", speed);
            break;
        case CMD_TURN_LEFT:
            Serial.printf("Turning LEFT at %.0f°/s\n", turn_rate);
            break;
        case CMD_TURN_RIGHT:
            Serial.printf("Turning RIGHT at %.0f°/s\n", turn_rate);
            break;
        default:
            Serial.println("Unknown command");
            break;
    }
}

void printConnectionStats() {
    static unsigned long last_stats_print = 0;
    unsigned long now = millis();
    
    // Print brief stats every 60 seconds when connected and receiving commands
    if (now - last_stats_print >= 60000) {
        if (controller->getConnectionState() == RCConnectionState_t::CONNECTED && command_count > 0) {
            Serial.printf("[INFO] %lu commands received, uptime: %.0fs, protocol: %s\n", 
                         command_count, now / 1000.0, protocolToString(controller->getProtocol()));
        }
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
    setLED(false);  // Start with LED off
    
    // Startup banner
    Serial.println("ESP32 Keyboard Remote Control Receiver");
    Serial.printf("Protocol: %s | LED: GPIO%d\n", protocolToString(RECEIVER_PROTOCOL), LED_PIN);
    
    // Check if selected protocol is available at compile time
    if (!isProtocolAvailable(RECEIVER_PROTOCOL)) {
        Serial.printf("[ERROR] Protocol %s not available (not compiled in)\n", protocolToString(RECEIVER_PROTOCOL));
        Serial.println("Check ENABLE_ESP32_RC_* macros in esp32_rc_user_config.h");
        while(1) delay(1000);  // Halt
    }
    
    // Initialize controller with selected protocol using factory function
    controller = createProtocolInstance(RECEIVER_PROTOCOL, false);  // Reliable mode
    
    if (controller) {
        Serial.printf("[OK] Controller initialized: %s\n", protocolToString(controller->getProtocol()));
        
        // Disable metrics display to keep command output clean
        controller->enableMetricsDisplay(false);
        
        // Connect to start listening
        controller->connect();
        connection_start_time = millis();
        
        Serial.println("[OK] Listening for keyboard commands from Python controller...");
        Serial.println();
    } else {
        Serial.println("[ERROR] Failed to initialize controller!");
        while(1) delay(1000);  // Halt on failure
    }
}

void loop() {
    RCPayload_t received_data;
    
    // Check for incoming keyboard commands
    if (controller && controller->recvData(received_data)) {
        // Process the keyboard command
        processKeyboardCommand(received_data);
    }
    
    // Handle LED feedback timing
    handleLEDFeedback();
    
    // Print periodic connection statistics
    printConnectionStats();
    
    // Small delay to prevent overwhelming the system
    delay(1);
}