#include <Arduino.h>
#include <ArduinoJson.h>
#include "esp32_rc_factory.h"

/*
ESP32 Bridge - PC to Wireless Protocol Translator

This example creates a bridge between PC commands (via USB/Serial) and wireless protocols
(ESP-NOW/NRF24). It enables testing remote control functionality using PC-based commands
without requiring physical remote controllers.

Features:
- JSON command interface via Serial
- Runtime protocol switching (ESP-NOW ⇔ NRF24)  
- Bidirectional data translation
- Real-time status reporting
- Automatic peer discovery

Usage:
1. Upload this firmware to ESP32
2. Connect ESP32 to PC via USB
3. Send JSON commands via serial terminal
4. Bridge translates to wireless packets using existing RCPayload_t structure

Example Commands:
{"cmd":"data", "v1":45.0, "v2":30.0, "id1":1, "flags":3}
{"cmd":"switch", "protocol":"espnow"}
{"cmd":"status"}
{"cmd":"discover"}

Protocol Support: ESP-NOW, NRF24 (WiFi can be added later)
*/

// =============================================================================
// Protocol Management
// =============================================================================

enum BridgeProtocol {
  PROTOCOL_ESPNOW = 0,
  PROTOCOL_NRF24 = 1
};

class ProtocolManager {
private:
  ESP32RemoteControl* controller_ = nullptr;
  BridgeProtocol current_protocol_ = PROTOCOL_ESPNOW;
  bool protocol_initialized_ = false;

public:
  bool initProtocol(BridgeProtocol protocol) {
    // Clean up existing controller
    if (controller_) {
      delete controller_;
      controller_ = nullptr;
    }
    
    // Create new controller based on protocol using factory function
    RCProtocol_t rc_protocol;
    switch (protocol) {
      case PROTOCOL_ESPNOW:
        rc_protocol = RC_PROTO_ESPNOW;
        break;
      case PROTOCOL_NRF24:
        rc_protocol = RC_PROTO_NRF24;
        break;
      default:
        return false;
    }
    
    // Check if protocol is available at compile time
    if (!isProtocolAvailable(rc_protocol)) {
      Serial.printf("{\"error\":\"protocol_not_compiled\", \"protocol\":\"%s\"}\n", 
                   protocolToString(rc_protocol));
      return false;
    }
    
    // Create controller instance
    controller_ = createProtocolInstance(rc_protocol, false); // Reliable mode for bridge
    
    if (controller_) {
      current_protocol_ = protocol;
      controller_->enableMetricsDisplay(false); // Disable auto metrics for cleaner bridge output
      controller_->connect();
      protocol_initialized_ = true;
      
      Serial.printf("{\"status\":\"protocol_switched\", \"protocol\":\"%s\"}\n", 
                   getProtocolName().c_str());
      return true;
    }
    
    return false;
  }
  
  ESP32RemoteControl* getController() { return controller_; }
  
  String getProtocolName() const {
    switch (current_protocol_) {
      case PROTOCOL_ESPNOW: return "espnow";
      case PROTOCOL_NRF24: return "nrf24";
      default: return "unknown";
    }
  }
  
  BridgeProtocol getCurrentProtocol() const { return current_protocol_; }
  bool isInitialized() const { return protocol_initialized_; }
  
  ~ProtocolManager() {
    if (controller_) {
      delete controller_;
    }
  }
};

// =============================================================================
// Serial Command Parser  
// =============================================================================

class SerialCommandParser {
private:
  String input_buffer_;
  static constexpr size_t MAX_JSON_SIZE = 512;
  DynamicJsonDocument doc_;
  
public:
  SerialCommandParser() : doc_(MAX_JSON_SIZE) {}
  
  bool parseCommand(String& command, JsonObject& params) {
    // Read available serial data
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (input_buffer_.length() > 0) {
          // Try to parse JSON
          DeserializationError error = deserializeJson(doc_, input_buffer_);
          if (error) {
            Serial.printf("{\"error\":\"json_parse_error\", \"message\":\"%s\"}\n", error.c_str());
            input_buffer_ = "";
            return false;
          }
          
          JsonObject root = doc_.as<JsonObject>();
          if (root.containsKey("cmd")) {
            command = root["cmd"].as<String>();
            params = root;
            input_buffer_ = "";
            return true;
          } else {
            Serial.println("{\"error\":\"missing_cmd_field\"}");
            input_buffer_ = "";
            return false;
          }
        }
      } else if (c != '\r') {
        input_buffer_ += c;
        // Prevent buffer overflow
        if (input_buffer_.length() > MAX_JSON_SIZE) {
          Serial.println("{\"error\":\"command_too_long\"}");
          input_buffer_ = "";
          return false;
        }
      }
    }
    return false;
  }
};

// =============================================================================
// Data Bridge - Protocol Translation
// =============================================================================

class DataBridge {
private:
  ProtocolManager* protocol_manager_;
  
public:
  DataBridge(ProtocolManager* pm) : protocol_manager_(pm) {}
  
  bool sendDataCommand(const JsonObject& params) {
    if (!protocol_manager_->isInitialized()) {
      Serial.println("{\"error\":\"protocol_not_initialized\"}");
      return false;
    }
    
    // Build RCPayload_t from JSON parameters
    RCPayload_t payload = {0};
    
    payload.id1 = params["id1"] | 0;
    payload.id2 = params["id2"] | 0; 
    payload.id3 = params["id3"] | 0;
    payload.id4 = params["id4"] | 0;
    payload.value1 = params["v1"] | 0.0f;
    payload.value2 = params["v2"] | 0.0f;
    payload.value3 = params["v3"] | 0.0f;
    payload.value4 = params["v4"] | 0.0f;
    payload.value5 = params["v5"] | 0.0f;
    payload.flags = params["flags"] | 0;
    
    // Send via current protocol
    ESP32RemoteControl* controller = protocol_manager_->getController();
    if (controller && controller->sendData(payload)) {
      Serial.printf("{\"status\":\"data_sent\", \"protocol\":\"%s\", \"timestamp\":%lu}\n", 
                   protocol_manager_->getProtocolName().c_str(), millis());
      return true;
    } else {
      Serial.println("{\"error\":\"send_failed\"}");
      return false;
    }
  }
  
  void checkIncomingData() {
    if (!protocol_manager_->isInitialized()) return;
    
    ESP32RemoteControl* controller = protocol_manager_->getController();
    RCPayload_t incoming;
    
    if (controller && controller->recvData(incoming)) {
      // Forward received data to PC as JSON
      Serial.printf("{\"event\":\"data_received\", \"protocol\":\"%s\", "
                   "\"id1\":%d, \"id2\":%d, \"id3\":%d, \"id4\":%d, "
                   "\"v1\":%.2f, \"v2\":%.2f, \"v3\":%.2f, \"v4\":%.2f, \"v5\":%.2f, "
                   "\"flags\":%d, \"timestamp\":%lu}\n",
                   protocol_manager_->getProtocolName().c_str(),
                   incoming.id1, incoming.id2, incoming.id3, incoming.id4,
                   incoming.value1, incoming.value2, incoming.value3, incoming.value4, incoming.value5,
                   incoming.flags, millis());
    }
  }
  
};

// =============================================================================  
// Status Reporter & Discovery Manager
// =============================================================================

class StatusReporter {
private:
  ProtocolManager* protocol_manager_;
  
public:
  StatusReporter(ProtocolManager* pm) : protocol_manager_(pm) {}
  
  void reportStatus() {
    if (!protocol_manager_->isInitialized()) {
      Serial.println("{\"status\":\"not_initialized\", \"protocol\":\"none\"}");
      return;
    }
    
    ESP32RemoteControl* controller = protocol_manager_->getController();
    String connection_state;
    
    switch (controller->getConnectionState()) {
      case RCConnectionState_t::DISCONNECTED: connection_state = "disconnected"; break;
      case RCConnectionState_t::CONNECTING: connection_state = "connecting"; break;
      case RCConnectionState_t::CONNECTED: connection_state = "connected"; break;
      case RCConnectionState_t::ERROR: connection_state = "error"; break;
      default: connection_state = "unknown"; break;
    }
    
    // Get metrics
    auto send_metrics = controller->getSendMetrics();
    auto recv_metrics = controller->getReceiveMetrics();
    
    Serial.printf("{\"status\":{\"protocol\":\"%s\", \"connection\":\"%s\", "
                 "\"send_metrics\":{\"success\":%u, \"failed\":%u, \"total\":%u, \"rate\":%.1f, \"tps\":%.1f}, "
                 "\"recv_metrics\":{\"success\":%u, \"failed\":%u, \"total\":%u, \"rate\":%.1f, \"tps\":%.1f}, "
                 "\"uptime_ms\":%lu}}\n",
                 protocol_manager_->getProtocolName().c_str(),
                 connection_state.c_str(),
                 send_metrics.successful, send_metrics.failed, send_metrics.getTotal(), 
                 send_metrics.getSuccessRate(), send_metrics.getTransactionRate(),
                 recv_metrics.successful, recv_metrics.failed, recv_metrics.getTotal(),
                 recv_metrics.getSuccessRate(), recv_metrics.getTransactionRate(),
                 millis());
  }
  
  void reportDiscoveryResult() {
    if (!protocol_manager_->isInitialized()) {
      Serial.println("{\"error\":\"protocol_not_initialized\"}");
      return;
    }
    
    ESP32RemoteControl* controller = protocol_manager_->getController();
    auto discovery_result = controller->getDiscoveryResult();
    
    if (discovery_result.discovered) {
      // Format MAC address as string
      char mac_str[18];
      snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
               discovery_result.peer_addr[0], discovery_result.peer_addr[1],
               discovery_result.peer_addr[2], discovery_result.peer_addr[3],
               discovery_result.peer_addr[4], discovery_result.peer_addr[5]);
      
      Serial.printf("{\"discovery\":{\"status\":\"peer_found\", \"timestamp\":%lu, \"mac\":\"%s\"}}\n",
                   millis(), mac_str);
    } else {
      Serial.println("{\"discovery\":{\"status\":\"no_peers_found\"}}");
    }
  }
};

// =============================================================================
// Command Handler
// =============================================================================

void handleCommand(const String& command, const JsonObject& params) {
  if (command == "data") {
    dataBridge.sendDataCommand(params);
    
  } else if (command == "switch") {
    String protocol = params["protocol"] | "";
    if (protocol == "espnow") {
      protocolManager.initProtocol(PROTOCOL_ESPNOW);
    } else if (protocol == "nrf24") {
      protocolManager.initProtocol(PROTOCOL_NRF24);
    } else {
      Serial.println("{\"error\":\"invalid_protocol\", \"supported\":[\"espnow\", \"nrf24\"]}");
    }
    
  } else if (command == "status") {
    statusReporter.reportStatus();
    
  } else if (command == "discover") {
    statusReporter.reportDiscoveryResult();
    
  } else if (command == "help") {
    Serial.println("{\"help\":{");
    Serial.println("  \"commands\":{");
    Serial.println("    \"data\":\"Send data payload - {\\\"cmd\\\":\\\"data\\\", \\\"v1\\\":1.0, \\\"id1\\\":1}\",");
    Serial.println("    \"switch\":\"Switch protocol - {\\\"cmd\\\":\\\"switch\\\", \\\"protocol\\\":\\\"espnow|nrf24\\\"}\",");
    Serial.println("    \"status\":\"Get bridge status - {\\\"cmd\\\":\\\"status\\\"}\",");
    Serial.println("    \"discover\":\"Check peer discovery - {\\\"cmd\\\":\\\"discover\\\"}\",");
    Serial.println("    \"help\":\"Show this help - {\\\"cmd\\\":\\\"help\\\"}\"");
    Serial.println("  },");
    Serial.println("  \"payload_fields\":{");
    Serial.println("    \"id1-id4\":\"Integer IDs (0-255)\",");
    Serial.println("    \"v1-v5\":\"Float values\","); 
    Serial.println("    \"flags\":\"8-bit flags field\"");
    Serial.println("  }");
    Serial.println("}}");
    
  } else {
    Serial.printf("{\"error\":\"unknown_command\", \"received\":\"%s\"}\n", command.c_str());
  }
}

// =============================================================================
// Global Bridge Components  
// =============================================================================

ProtocolManager protocolManager;
SerialCommandParser commandParser;
DataBridge dataBridge(&protocolManager);
StatusReporter statusReporter(&protocolManager);

// =============================================================================
// Setup & Main Loop
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Bridge startup banner
  Serial.println("{\"bridge\":\"ESP32_RC_Bridge\", \"version\":\"1.0.0\", \"status\":\"starting\"}");
  
  // Initialize with default protocol (ESP-NOW)
  if (protocolManager.initProtocol(PROTOCOL_ESPNOW)) {
    Serial.println("{\"status\":\"bridge_ready\", \"default_protocol\":\"espnow\"}");
  } else {
    Serial.println("{\"error\":\"failed_to_initialize_default_protocol\"}");
  }
  
  // Print help message
  Serial.println("{\"help\":\"Available commands: data, switch, status, discover, help\"}");
  Serial.println("{\"example\":\"Send: {\\\"cmd\\\":\\\"data\\\", \\\"v1\\\":45.0, \\\"id1\\\":1}\"}");
}

void loop() {
  String command;
  JsonObject params;
  
  // Process incoming serial commands
  if (commandParser.parseCommand(command, params)) {
    handleCommand(command, params);
  }
  
  // Bridge data between PC and wireless protocols
  dataBridge.checkIncomingData();
  
  // Small delay to prevent overwhelming the system
  delay(1);
}

