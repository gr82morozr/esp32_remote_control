#pragma once

#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include "esp32_rc.h"

// Raw 802.11 Frame Injection Discovery Protocol
// Based on Build Details: Ultra-fast discovery using custom 802.11 frames

// Custom vendor OUI for RC discovery frames
#define RC_VENDOR_OUI_0    0x12
#define RC_VENDOR_OUI_1    0x34  
#define RC_VENDOR_OUI_2    0x56

// Discovery channel (fixed channel 6 as per spec)
#define RC_DISCOVERY_CHANNEL    6

// Discovery frame types
#define RC_FRAME_TYPE_DISCOVERY    0x01
#define RC_FRAME_TYPE_RESPONSE     0x02

// Custom 802.11 probe request frame structure for discovery
struct __attribute__((packed)) RCDiscoveryFrame_t {
    // 802.11 Management Frame Header (24 bytes)
    uint16_t frame_control;     // Frame control field (probe request = 0x0040)
    uint16_t duration;          // Duration field  
    uint8_t  dest_addr[6];      // Destination MAC (broadcast)
    uint8_t  src_addr[6];       // Source MAC (our MAC)
    uint8_t  bssid[6];          // BSSID (broadcast for probe request)
    uint16_t sequence_control;  // Sequence control
    
    // Probe request frame body - Custom vendor IE
    uint8_t  element_id;        // Information element ID (221 = vendor specific)
    uint8_t  length;           // Length of vendor IE data
    uint8_t  oui[3];           // Our vendor OUI
    uint8_t  oui_type;         // OUI type
    uint8_t  frame_type;       // Discovery/Response
    uint8_t  node_priority;    // Node priority for AP election
    uint32_t timestamp_ms;     // Discovery timestamp
    uint8_t  node_mac[6];      // Node MAC address
} ;

class ESP32_RC_WIFI : public ESP32RemoteControl {
public:
    ESP32_RC_WIFI(bool fast_mode = false);
    ~ESP32_RC_WIFI() override;
    
    // Core interface implementation
    void connect() override;
    RCProtocol_t getProtocol() const override { return RC_PROTO_WIFI; }
    
    // Address handling for WiFi (uses IP addresses)
    uint8_t getAddressSize() const override { return 4; }
    RCAddress_t createBroadcastAddress() const override;

protected:
    // Required base class implementations
    void lowLevelSend(const RCMessage_t& msg) override;
    RCMessage_t parseRawData(const uint8_t* data, size_t len) override;
    void checkHeartbeat() override;
    void setPeerAddr(const uint8_t* peer_addr) override;
    void setPeerAddr(const RCAddress_t& peer_addr) override;
    void unsetPeerAddr() override;

private:
    // WiFi connection state
    bool is_ap_mode_ = false;
    IPAddress my_ip_;
    IPAddress peer_ip_;
    uint8_t peer_mac_[6] = {0};
    uint8_t node_priority_ = 0;
    uint8_t peer_priority_ = 0;
    
    // Communication
    WiFiUDP udp_;
    uint16_t udp_port_ = 12345;
    
    // Raw 802.11 discovery state
    bool peer_discovered_ = false;
    uint32_t discovery_start_ms_ = 0;
    uint32_t last_frame_inject_ms_ = 0;
    uint16_t sequence_number_ = 0;
    
    // Step 3: Continuous handshake monitoring state
    bool continuous_listening_active_ = false;
    uint32_t last_peer_discovery_ms_ = 0;
    uint32_t peer_silence_timeout_ms_ = 10000;  // 10 seconds silence = potential reboot
    
    // Discovery phases
    enum DiscoveryPhase {
        PHASE_RAW_DISCOVERY,    // Raw 802.11 frame injection discovery
        PHASE_ROLE_NEGOTIATE,   // Determine AP vs Station roles
        PHASE_WIFI_CONNECT,     // Establish WiFi connection
        PHASE_UDP_HANDSHAKE,    // UDP handshake exchange
        PHASE_CONNECTED         // Ready for data/heartbeat
    };
    DiscoveryPhase discovery_phase_ = PHASE_RAW_DISCOVERY;
    
    // Raw 802.11 frame injection methods
    void startRawDiscovery();
    bool performRawDiscovery();
    void injectDiscoveryFrame();
    void scanForPeerAPs();
    void setupSniffMode();
    void stopSniffMode();
    static void IRAM_ATTR sniffCallback(void* buf, wifi_promiscuous_pkt_type_t type);
    void processReceivedFrame(const wifi_promiscuous_pkt_t* pkt);
    
    // Step 3: Continuous handshake listening for reboot detection
    void startContinuousHandshakeListening();
    void monitorForRehandshake();
    void handlePeerRebootDetected();
    
    // Role negotiation and WiFi setup
    bool negotiateRoles();
    bool establishWiFiConnection();
    bool performUDPHandshake();
    
    // AP/Station role management
    bool becomeAccessPoint();
    bool connectAsStation();
    
    // UDP handshake protocol
    bool sendUDPHandshake();
    bool listenForUDPHandshake();
    
    // Utility functions
    uint8_t calculatePriority();
    String generateDynamicSSID();
    String formatIP(IPAddress ip) const;
    
    // Static instance for sniff callback
    static ESP32_RC_WIFI* instance_;
};