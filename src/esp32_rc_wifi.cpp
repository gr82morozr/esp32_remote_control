#include "esp32_rc_wifi.h"

// Discovery timing constants
#define RAW_DISCOVERY_TIMEOUT_MS     3000    // 3s for raw frame discovery
#define FRAME_INJECT_INTERVAL_MS     500     // Inject frame every 500ms
#define UDP_HANDSHAKE_TIMEOUT_MS     5000    // 5s for UDP handshake
#define CONNECTION_TIMEOUT_MS        15000   // 15s total connection timeout

// Static instance for sniff callback
ESP32_RC_WIFI* ESP32_RC_WIFI::instance_ = nullptr;

// ========== Constructor / Destructor ==========

ESP32_RC_WIFI::ESP32_RC_WIFI(bool fast_mode) : ESP32RemoteControl(fast_mode) {
    // Set static instance for callbacks
    instance_ = this;
    
    // Initialize MAC address
    uint8_t mac[6];
    WiFi.macAddress(mac);
    memcpy(my_addr_, mac, 6);
    memcpy(my_address_, my_addr_, RC_ADDR_SIZE);
    
    // Calculate priority for role negotiation
    node_priority_ = calculatePriority();
    
    LOG("[STEP1] Raw 802.11 WiFi Init - MAC: %02X:%02X:%02X:%02X:%02X:%02X, Priority: %d",
        my_addr_[0], my_addr_[1], my_addr_[2], 
        my_addr_[3], my_addr_[4], my_addr_[5], node_priority_);
}

ESP32_RC_WIFI::~ESP32_RC_WIFI() {
    stopSniffMode();
    udp_.stop();
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    instance_ = nullptr;
}

// ========== Core Interface Implementation ==========

void ESP32_RC_WIFI::connect() {
    LOG("[STEP1+3] Starting raw 802.11 frame discovery protocol with continuous listening...");
    
    conn_state_ = RCConnectionState_t::CONNECTING;
    discovery_phase_ = PHASE_RAW_DISCOVERY;
    startRawDiscovery();
    
    // STEP 1 & 3: Discovery with continuous handshake listening capability
    unsigned long connection_start = millis();
    
    while (conn_state_ == RCConnectionState_t::CONNECTING && 
           (millis() - connection_start) < CONNECTION_TIMEOUT_MS) {
        
        switch (discovery_phase_) {
            case PHASE_RAW_DISCOVERY:
                if (performRawDiscovery()) {
                    discovery_phase_ = PHASE_ROLE_NEGOTIATE;
                    if (peer_discovered_) {
                        LOG("[STEP1+3] ✅ Raw discovery complete - peer found!");
                    } else {
                        LOG("[STEP1+3] ⏰ Raw discovery timeout - proceeding without peer");
                    }
                }
                break;
                
            case PHASE_ROLE_NEGOTIATE:
                if (negotiateRoles()) {
                    // STEP 1: Complete discovery and role negotiation
                    // STEP 3: Start continuous handshake listening
                    discovery_phase_ = PHASE_CONNECTED;
                    conn_state_ = RCConnectionState_t::CONNECTED;
                    LOG("[STEP1+3] ✅ STEP 1 COMPLETE - Discovery and role negotiation finished");
                    LOG("[STEP1+3] Final result: %s mode", is_ap_mode_ ? "AP" : "Station");
                    LOG("[STEP3] 🔄 Starting continuous handshake listening for peer reboots...");
                    startContinuousHandshakeListening();
                }
                break;
                
            // STEP 1: Disable WiFi connection, UDP handshake, and data phases for now
            case PHASE_WIFI_CONNECT:
            case PHASE_UDP_HANDSHAKE:
                // Skip these phases in Step 1
                discovery_phase_ = PHASE_CONNECTED;
                conn_state_ = RCConnectionState_t::CONNECTED;
                break;
                
            case PHASE_CONNECTED:
                // Discovery and role negotiation complete
                // STEP 3: Monitor for peer reboots and re-handshake requests
                monitorForRehandshake();
                break;
        }
        
        delay(50);  // Small delay for state machine
    }
    
    if (conn_state_ != RCConnectionState_t::CONNECTED) {
        LOG("[STEP1+3] ❌ Discovery timeout");
        conn_state_ = RCConnectionState_t::ERROR;
        return;
    }
    
    // STEP 1: Skip UDP setup and heartbeat for now
    // STEP 3: Continuous listening is now active
    LOG("[STEP1+3] ✅ Discovery complete with continuous handshake monitoring active");
}

// ========== Raw 802.11 Frame Discovery Implementation ==========

void ESP32_RC_WIFI::startRawDiscovery() {
    LOG("[STEP1] Starting raw 802.11 frame discovery on channel %d", RC_DISCOVERY_CHANNEL);
    
    discovery_start_ms_ = millis();
    last_frame_inject_ms_ = 0;
    peer_discovered_ = false;
    sequence_number_ = 0;
    
    // Setup sniff mode on fixed channel
    setupSniffMode();
}

bool ESP32_RC_WIFI::performRawDiscovery() {
    // Inject discovery frames periodically
    if (millis() - last_frame_inject_ms_ >= FRAME_INJECT_INTERVAL_MS) {
        injectDiscoveryFrame();
        last_frame_inject_ms_ = millis();
    }
    
    // Check if peer was discovered through sniff callback
    if (peer_discovered_) {
        LOG("[STEP1] ✅ Peer discovered via raw 802.11 frames!");
        stopSniffMode();
        return true;
    }
    
    // Check if discovery timeout
    if (millis() - discovery_start_ms_ >= RAW_DISCOVERY_TIMEOUT_MS) {
        LOG("[STEP1] Raw discovery timeout - no peers found");
        stopSniffMode();
        peer_discovered_ = false;
        return true; // Continue to role negotiation
    }
    
    return false; // Continue discovery
}

void ESP32_RC_WIFI::injectDiscoveryFrame() {
    LOG("[STEP1] Injecting discovery frame (seq: %d)", sequence_number_);
    
    // Create custom 802.11 probe request frame
    RCDiscoveryFrame_t frame = {};
    
    // 802.11 Management Frame Header - Probe Request
    frame.frame_control = 0x0040;  // Management frame, Probe Request subtype
    frame.duration = 0;
    
    // Broadcast destination and BSSID
    memset(frame.dest_addr, 0xFF, 6);
    memset(frame.bssid, 0xFF, 6);
    
    // Source = our MAC
    memcpy(frame.src_addr, my_addr_, 6);
    
    // Sequence control
    frame.sequence_control = (sequence_number_ << 4);
    sequence_number_++;
    
    // Vendor-specific Information Element (IE)
    frame.element_id = 221;  // Vendor specific IE
    frame.length = 3 + 1 + 1 + 1 + 4 + 6;  // OUI + oui_type + frame_type + priority + timestamp + MAC
    frame.oui[0] = RC_VENDOR_OUI_0;
    frame.oui[1] = RC_VENDOR_OUI_1; 
    frame.oui[2] = RC_VENDOR_OUI_2;
    frame.oui_type = 1;  // Discovery type
    frame.frame_type = RC_FRAME_TYPE_DISCOVERY;
    frame.node_priority = node_priority_;
    frame.timestamp_ms = millis();
    memcpy(frame.node_mac, my_addr_, 6);
    
    // Inject the frame
    esp_err_t result = esp_wifi_80211_tx(WIFI_IF_STA, &frame, sizeof(frame), false);
    
    if (result == ESP_OK) {
        LOG("[STEP1] ✅ Discovery frame injected successfully");
    } else {
        LOG("[STEP1] ❌ Frame injection failed: %s", esp_err_to_name(result));
    }
}

void ESP32_RC_WIFI::scanForPeerAPs() {
    // Simple scan for existing peer APs (not discovery APs)
    int n = WiFi.scanNetworks();
    
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.startsWith("rc-") && !ssid.startsWith("rc-discovery-")) {
            // Found a peer main AP!
            LOG("[STEP1] Found peer AP: %s", ssid.c_str());
            
            // Extract MAC from SSID (format: rc-XXYYZZ)
            if (ssid.length() >= 9) { // "rc-" + 6 hex chars
                String mac_suffix = ssid.substring(3); // Remove "rc-"
                if (mac_suffix.length() == 6) {
                    // Parse hex MAC suffix
                    peer_mac_[3] = (uint8_t)strtol(mac_suffix.substring(0, 2).c_str(), NULL, 16);
                    peer_mac_[4] = (uint8_t)strtol(mac_suffix.substring(2, 4).c_str(), NULL, 16);
                    peer_mac_[5] = (uint8_t)strtol(mac_suffix.substring(4, 6).c_str(), NULL, 16);
                    
                    // For role negotiation, we need the full MAC. We'll use the BSSID.
                    uint8_t* bssid = WiFi.BSSID(i);
                    memcpy(peer_mac_, bssid, 6);
                    
                    peer_discovered_ = true;
                    LOG("[STEP1] ✅ Peer AP discovered: %s, MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                        ssid.c_str(), peer_mac_[0], peer_mac_[1], peer_mac_[2],
                        peer_mac_[3], peer_mac_[4], peer_mac_[5]);
                    break;
                }
            }
        }
    }
    WiFi.scanDelete();
}

void ESP32_RC_WIFI::setupSniffMode() {
    LOG("[STEP1] Setting up sniff mode on channel %d", RC_DISCOVERY_CHANNEL);
    
    // Initialize WiFi in STA mode first
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    // Set fixed channel for discovery
    esp_wifi_set_channel(RC_DISCOVERY_CHANNEL, WIFI_SECOND_CHAN_NONE);
    
    // Enable sniff mode with callback
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&ESP32_RC_WIFI::sniffCallback);
    
    LOG("[STEP1] Sniff mode active - listening for discovery frames");
}

void ESP32_RC_WIFI::stopSniffMode() {
    LOG("[STEP1] Stopping sniff mode");
    esp_wifi_set_promiscuous(false);
    WiFi.scanDelete();
}

void IRAM_ATTR ESP32_RC_WIFI::sniffCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (instance_ && type == WIFI_PKT_MGMT) {
        const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
        instance_->processReceivedFrame(pkt);
    }
}

void ESP32_RC_WIFI::processReceivedFrame(const wifi_promiscuous_pkt_t* pkt) {
    if (!pkt || pkt->rx_ctrl.sig_len < sizeof(RCDiscoveryFrame_t)) {
        return;
    }
    
    const RCDiscoveryFrame_t* frame = (const RCDiscoveryFrame_t*)pkt->payload;
    
    // Check if this is a management frame with probe request subtype
    if (frame->frame_control != 0x0040) {  // Probe request frame check
        return;
    }
    
    // Check if this is our vendor specific information element
    if (frame->element_id != 221 ||  // Vendor specific IE
        frame->oui[0] != RC_VENDOR_OUI_0 || 
        frame->oui[1] != RC_VENDOR_OUI_1 || 
        frame->oui[2] != RC_VENDOR_OUI_2 ||
        frame->oui_type != 1 ||
        frame->frame_type != RC_FRAME_TYPE_DISCOVERY) {
        return;
    }
    
    // Ignore our own frames
    if (memcmp(frame->node_mac, my_addr_, 6) == 0) {
        return;
    }
    
    // Found a peer discovery frame!
    LOG("[STEP1] ✅ Peer discovery frame received from: %02X:%02X:%02X:%02X:%02X:%02X (priority: %d)",
        frame->node_mac[0], frame->node_mac[1], frame->node_mac[2],
        frame->node_mac[3], frame->node_mac[4], frame->node_mac[5], 
        frame->node_priority);
    
    // Store peer information
    memcpy(peer_mac_, frame->node_mac, 6);
    peer_discovered_ = true;
    
    // Store peer priority for role negotiation
    peer_priority_ = frame->node_priority;
    
    // STEP 3: Update last peer discovery time for reboot detection
    last_peer_discovery_ms_ = millis();
}

// ========== Role Negotiation and WiFi Setup ==========

bool ESP32_RC_WIFI::negotiateRoles() {
    LOG("[STEP1] Role negotiation - Peer discovered: %s", 
        peer_discovered_ ? "YES" : "NO");
    
    if (peer_discovered_) {
        // Compare MAC addresses to determine role
        // Higher MAC becomes AP, lower MAC becomes Station
        int mac_comparison = memcmp(my_addr_, peer_mac_, 6);
        
        if (mac_comparison > 0) {
            // Our MAC is higher - become AP
            is_ap_mode_ = true;
            LOG("[STEP1] MAC comparison: Our MAC higher - becoming AP");
            LOG("[STEP1] Our MAC:  %02X:%02X:%02X:%02X:%02X:%02X", 
                my_addr_[0], my_addr_[1], my_addr_[2], my_addr_[3], my_addr_[4], my_addr_[5]);
            LOG("[STEP1] Peer MAC: %02X:%02X:%02X:%02X:%02X:%02X", 
                peer_mac_[0], peer_mac_[1], peer_mac_[2], peer_mac_[3], peer_mac_[4], peer_mac_[5]);
        } else if (mac_comparison < 0) {
            // Peer MAC is higher - become Station
            is_ap_mode_ = false;
            LOG("[STEP1] MAC comparison: Peer MAC higher - becoming Station");
            LOG("[STEP1] Our MAC:  %02X:%02X:%02X:%02X:%02X:%02X", 
                my_addr_[0], my_addr_[1], my_addr_[2], my_addr_[3], my_addr_[4], my_addr_[5]);
            LOG("[STEP1] Peer MAC: %02X:%02X:%02X:%02X:%02X:%02X", 
                peer_mac_[0], peer_mac_[1], peer_mac_[2], peer_mac_[3], peer_mac_[4], peer_mac_[5]);
        } else {
            // Identical MACs (shouldn't happen) - use priority as fallback
            is_ap_mode_ = (node_priority_ > peer_priority_);
            LOG("[STEP1] Identical MACs - using priority. Our: %d, Peer: %d - becoming %s", 
                node_priority_, peer_priority_, is_ap_mode_ ? "AP" : "Station");
        }
    } else {
        // No peer found - become AP
        is_ap_mode_ = true;
        LOG("[STEP1] No peer discovered - becoming AP");
    }
    
    LOG("[STEP1] ✅ Role decided: %s", is_ap_mode_ ? "AP" : "Station");
    return true;
}

bool ESP32_RC_WIFI::establishWiFiConnection() {
    if (is_ap_mode_) {
        return becomeAccessPoint();
    } else {
        return connectAsStation();
    }
}

bool ESP32_RC_WIFI::becomeAccessPoint() {
    String ssid = generateDynamicSSID();
    String password = "esp32remote";
    
    LOG("[STEP1] Creating AP: %s", ssid.c_str());
    
    WiFi.mode(WIFI_AP);
    bool success = WiFi.softAP(ssid.c_str(), password.c_str(), RC_DISCOVERY_CHANNEL);
    
    if (success) {
        my_ip_ = WiFi.softAPIP();
        LOG("[STEP1] ✅ AP ready - IP: %s", formatIP(my_ip_).c_str());
        return true;
    } else {
        LOG("[STEP1] ❌ AP creation failed");
        return false;
    }
}

bool ESP32_RC_WIFI::connectAsStation() {
    // Check if we have valid peer MAC
    if (peer_mac_[0] == 0 && peer_mac_[1] == 0 && peer_mac_[2] == 0 && 
        peer_mac_[3] == 0 && peer_mac_[4] == 0 && peer_mac_[5] == 0) {
        LOG("[STEP1] ❌ No valid peer MAC for station connection");
        return false;
    }
    
    // Generate peer's SSID based on their MAC (proper hex formatting)
    String peer_ssid = String("rc-") + 
                      String(peer_mac_[3] < 16 ? "0" : "") + String(peer_mac_[3], HEX) +
                      String(peer_mac_[4] < 16 ? "0" : "") + String(peer_mac_[4], HEX) +
                      String(peer_mac_[5] < 16 ? "0" : "") + String(peer_mac_[5], HEX);
    String password = "esp32remote";
    
    LOG("[STEP1] Connecting to peer AP: %s (Peer MAC: %02X:%02X:%02X:%02X:%02X:%02X)", 
        peer_ssid.c_str(), peer_mac_[0], peer_mac_[1], peer_mac_[2],
        peer_mac_[3], peer_mac_[4], peer_mac_[5]);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(peer_ssid.c_str(), password.c_str());
    
    // Wait for connection
    unsigned long start_time = millis();
    while (WiFi.status() != WL_CONNECTED && 
           (millis() - start_time) < 5000) {
        delay(100);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        my_ip_ = WiFi.localIP();
        LOG("[STEP1] ✅ Station connected - IP: %s", formatIP(my_ip_).c_str());
        return true;
    } else {
        LOG("[STEP1] ❌ Station connection failed");
        return false;
    }
}

bool ESP32_RC_WIFI::performUDPHandshake() {
    // UDP handshake to exchange final connection details
    static unsigned long handshake_start = 0;
    
    if (handshake_start == 0) {
        handshake_start = millis();
        LOG("[STEP1] Starting UDP handshake...");
    }
    
    // Send handshake message periodically
    sendUDPHandshake();
    
    // Listen for handshake response
    if (listenForUDPHandshake()) {
        LOG("[STEP1] ✅ UDP handshake complete");
        return true;
    }
    
    // Check timeout
    if (millis() - handshake_start >= UDP_HANDSHAKE_TIMEOUT_MS) {
        LOG("[STEP1] UDP handshake timeout");
        return true; // Continue anyway for now
    }
    
    return false;
}

// ========== UDP Handshake Implementation ==========

bool ESP32_RC_WIFI::sendUDPHandshake() {
    if (!udp_.begin(udp_port_)) {
        return false;
    }
    
    // Create handshake message
    RCMessage_t msg = {};
    msg.type = RCMSG_TYPE_IP_DISCOVERY;
    memcpy(msg.from_addr, my_addr_, 6);
    
    // Put IP in payload
    msg.payload[0] = my_ip_[0];
    msg.payload[1] = my_ip_[1];
    msg.payload[2] = my_ip_[2];
    msg.payload[3] = my_ip_[3];
    
    // Broadcast to subnet
    IPAddress broadcast_ip = my_ip_;
    broadcast_ip[3] = 255;
    
    udp_.beginPacket(broadcast_ip, udp_port_);
    size_t written = udp_.write((const uint8_t*)&msg, sizeof(msg));
    bool success = udp_.endPacket();
    
    return (success && written == sizeof(msg));
}

bool ESP32_RC_WIFI::listenForUDPHandshake() {
    int packet_size = udp_.parsePacket();
    if (packet_size != sizeof(RCMessage_t)) {
        return false;
    }
    
    RCMessage_t msg;
    udp_.read((uint8_t*)&msg, sizeof(msg));
    
    if (msg.type == RCMSG_TYPE_IP_DISCOVERY) {
        // Extract peer IP from message
        IPAddress discovered_ip(msg.payload[0], msg.payload[1], 
                               msg.payload[2], msg.payload[3]);
        
        // Ignore our own broadcasts
        if (discovered_ip != my_ip_) {
            peer_ip_ = discovered_ip;
            
            LOG("[STEP1] ✅ Peer handshake - IP: %s", formatIP(peer_ip_).c_str());
            
            // Notify base class
            RCAddress_t peer_addr = {peer_ip_[0], peer_ip_[1], peer_ip_[2], peer_ip_[3], 0, 0};
            String ip_info = formatIP(peer_ip_);
            onPeerDiscovered(peer_addr, ip_info.c_str());
            
            return true;
        }
    }
    
    return false;
}

// ========== Required Base Class Implementations ==========

void ESP32_RC_WIFI::lowLevelSend(const RCMessage_t& msg) {
    if (peer_ip_ == IPAddress(0, 0, 0, 0)) {
        return;
    }
    
    udp_.beginPacket(peer_ip_, udp_port_);
    udp_.write((const uint8_t*)&msg, sizeof(msg));
    udp_.endPacket();
}

RCMessage_t ESP32_RC_WIFI::parseRawData(const uint8_t* data, size_t len) {
    RCMessage_t msg = {};
    
    if (data && len == sizeof(RCMessage_t)) {
        memcpy(&msg, data, sizeof(RCMessage_t));
        
        // Validate message type
        if (msg.type == RCMSG_TYPE_DATA || 
            msg.type == RCMSG_TYPE_HEARTBEAT || 
            msg.type == RCMSG_TYPE_IP_DISCOVERY) {
            return msg;
        }
    }
    
    memset(&msg, 0, sizeof(RCMessage_t));
    return msg;
}

void ESP32_RC_WIFI::checkHeartbeat() {
    ESP32RemoteControl::checkHeartbeat();
}

void ESP32_RC_WIFI::setPeerAddr(const uint8_t* peer_addr) {
    if (peer_addr) {
        memcpy(peer_mac_, peer_addr, 6);
    }
}

// Using base class implementation for setPeerAddr

void ESP32_RC_WIFI::unsetPeerAddr() {
    peer_ip_ = IPAddress(0, 0, 0, 0);
    memset(peer_mac_, 0, 6);
}

void ESP32_RC_WIFI::createBroadcastAddress(RCAddress_t& broadcast_addr) const {
    uint8_t broadcast[RC_ADDR_SIZE] = {255, 255, 255, 255, 0, 0};
    memcpy(broadcast_addr, broadcast, RC_ADDR_SIZE);
}

// ========== Utility Functions ==========

uint8_t ESP32_RC_WIFI::calculatePriority() {
    // Simple priority based on MAC address
    uint32_t priority = 0;
    for (int i = 0; i < 6; i++) {
        priority += my_addr_[i];
    }
    return (uint8_t)(priority % 256);
}

String ESP32_RC_WIFI::generateDynamicSSID() {
    // Generate SSID with proper hex formatting (always 2 digits)
    return String("rc-") + 
           String(my_addr_[3] < 16 ? "0" : "") + String(my_addr_[3], HEX) +
           String(my_addr_[4] < 16 ? "0" : "") + String(my_addr_[4], HEX) +
           String(my_addr_[5] < 16 ? "0" : "") + String(my_addr_[5], HEX);
}

String ESP32_RC_WIFI::formatIP(IPAddress ip) const {
    return String(ip[0]) + "." + String(ip[1]) + "." + 
           String(ip[2]) + "." + String(ip[3]);
}

// ========== Step 3: Continuous Handshake Listening ==========

void ESP32_RC_WIFI::startContinuousHandshakeListening() {
    LOG("[STEP3] Activating continuous handshake listening mode");
    continuous_listening_active_ = true;
    last_peer_discovery_ms_ = millis();
    
    // Keep sniff mode active for continuous monitoring
    bool sniff_enabled = false;
    esp_wifi_get_promiscuous(&sniff_enabled);
    if (!sniff_enabled) {
        setupSniffMode();
    }
    
    // Continue injecting discovery frames periodically to maintain presence
    last_frame_inject_ms_ = millis();
}

void ESP32_RC_WIFI::monitorForRehandshake() {
    if (!continuous_listening_active_) {
        return;
    }
    
    // Continue sending periodic discovery frames to maintain presence
    if (millis() - last_frame_inject_ms_ >= (FRAME_INJECT_INTERVAL_MS * 2)) {  // Slower rate when connected
        injectDiscoveryFrame();
        last_frame_inject_ms_ = millis();
    }
    
    // Check if peer has gone silent (potential reboot)
    if (peer_discovered_ && 
        (millis() - last_peer_discovery_ms_) > peer_silence_timeout_ms_) {
        
        LOG("[STEP3] ⚠️  Peer silence detected - potential reboot!");
        handlePeerRebootDetected();
    }
}

void ESP32_RC_WIFI::handlePeerRebootDetected() {
    LOG("[STEP3] 🔄 Handling peer reboot - restarting handshake process");
    
    // Reset peer discovery state
    peer_discovered_ = false;
    peer_priority_ = 0;
    memset(peer_mac_, 0, 6);
    
    // Reset connection state to trigger re-discovery
    conn_state_ = RCConnectionState_t::CONNECTING;
    discovery_phase_ = PHASE_RAW_DISCOVERY;
    
    // Restart discovery process
    startRawDiscovery();
    
    LOG("[STEP3] 🔄 Re-handshake process initiated");
}