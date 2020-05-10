#include <Arduino.h>
#include "remote_control_espnow.h"


// Declare all static varibles
int ESPNOWRemoteControl::role;
int ESPNOWRemoteControl::peer_status;
uint8_t ESPNOWRemoteControl::message_recv[MESSAGE_SIZE];
esp_now_peer_info_t ESPNOWRemoteControl::peer;
  

/*
 * =================================================================
 * Contructor
 *  - Controllor  
 *  - Receiver
 * =================================================================
*/
ESPNOWRemoteControl::ESPNOWRemoteControl(int r) {
  Serial.begin(SERIAL_BAUD_RATE);
  role = r;
  memset(ESPNOWRemoteControl::peer.peer_addr, 0, 6);
  println("This Device RC Role = " + String(role) );
}


/*
 * =================================================================
 * Init Controller and Receiver 
 *   
 * =================================================================
*/
void ESPNOWRemoteControl::init(void) {
  this->init_network();
  peer_status = PEER_NOT_FOUND;
}

/*
 * =================================================================
 * Detect Network Connection Status
 *  If error detected, needs to re-connect 
 * =================================================================
*/
bool ESPNOWRemoteControl::check_connection(void) {
  int retry = 0;
  switch (role) {
    // ===== For Controller =====
    case RC_CONTROLLER : 
      while (peer_status != PEER_PAIRED && retry <=3) {
        println("Controller : Checking... (peer_status=" + String(peer_status) + ")" );
        retry ++;
        switch (peer_status) {
          case PEER_NOT_FOUND:
            this->scan_network(); // look for receiver, if found, set the peer object
            break;
          case PEER_FOUND:
            this->pair_peer();
            break;
          case PEER_PAIRED:
            break;
          case PEER_ERROR:
            peer_status = PEER_NOT_FOUND; // retry from scan_network.
            break;
          default:
            peer_status = PEER_NOT_FOUND; // retry from scan_network.
            break;
        };
      };
      println("Controller : Checked (peer_status=" + String(peer_status) + ") after retry=" + String(retry) );
      break;


    // ====== For receiver =====
    case RC_RECEIVER:
      switch (peer_status) {
        case PEER_NOT_FOUND:
          println("Peer Mac has not been set, waiting for the 1st message coming ....");
          // do nothing , just wait...
          break;
        case PEER_FOUND:
          println("Peer Mac has been set, but not paired");
          this->pair_peer(); 
          break;
        case PEER_PAIRED:
          println("Already paired successfully.");
          break;
        case PEER_ERROR:    
          peer_status = PEER_NOT_FOUND; // retry from re-connect
          break;
        default:
          peer_status = PEER_NOT_FOUND; // retry from re-connect
          break;
      }
  }
  return (peer_status==PEER_PAIRED); 
}


// Init ESP Now with fallback
void ESPNOWRemoteControl::init_network() {
  switch (role) {
    case RC_CONTROLLER:
      /* Master connects to Receiver  */
      WiFi.mode(WIFI_STA);
      break;
    case RC_RECEIVER:
      /* Receiver hosts an WIFI network as AP */
      WiFi.mode(WIFI_AP);
      this->config_ap();
      break;
    default:
      println("Error: No Role is defined.");
      delay(1000);
      ESP.restart();
      break;
  }

  WiFi.disconnect();

  if (esp_now_init() == ESP_OK) {
    println("ESPNow Init Success.");
  } else {
    println("ESPNow Init Failed, wait for 1s and restart.");
    delay(1000);
    ESP.restart();
  }

  esp_now_register_send_cb(ESPNOWRemoteControl::on_datasent);
  esp_now_register_recv_cb(ESPNOWRemoteControl::on_datarecv);

}


void ESPNOWRemoteControl::config_ap(void) {
  bool result = WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, ESPNOW_CHANNEL, 0);
  if (!result) {
    println("AP Config failed.");
  } else {
    println("AP Config Success. Broadcasting with AP: " + String(WIFI_SSID));
    println(WiFi.macAddress());
  }
}


void ESPNOWRemoteControl::scan_network() {
/* 
 * ================================================================== 
 *  Only invoked from Controller (Master)
 *  to scan for the possible receivers
 * 
 * 
 * 
 * 
 * ==================================================================
 */
   
  int8_t scanResults = WiFi.scanNetworks();
  peer_status = PEER_NOT_FOUND;
  memset(&peer, 0, sizeof(peer));
  if (scanResults > 0) {
    println("Found " + String(scanResults) + " devices.");
    for (int i = 0; i < scanResults; ++i) {
      // Print SSID and RSSI for each device found
      String SSID = WiFi.SSID(i);
      int32_t RSSI = WiFi.RSSI(i);
      String BSSIDstr = WiFi.BSSIDstr(i);
      delay(10);
      // Check if the current device matches
      if (SSID.indexOf(WIFI_SSID) == 0) {
        if ( 6 == sscanf(BSSIDstr.c_str(), "%x:%x:%x:%x:%x:%x",  &peer.peer_addr[0], &peer.peer_addr[1], &peer.peer_addr[2], &peer.peer_addr[3], &peer.peer_addr[4], &peer.peer_addr[5] ) ) {
          peer_status = PEER_FOUND;
        }          
        break;
      }
    }
  } else {
    println("Error : No receiver found");
    peer_status = PEER_NOT_FOUND;
    //delay(1000);
    //ESP.restart();
  }
  
  // clean up ram
  WiFi.scanDelete();
}



/* 
 * ========================================================
 * Pairing ESP Now Controller and Receiver
 * 
 * 
 * ========================================================
 */
void ESPNOWRemoteControl::pair_peer() {
  if ( ! esp_now_is_peer_exist(peer.peer_addr) || peer_status != PEER_PAIRED ) { // if not exists
    
    //clean existing pairing
    if (esp_now_del_peer(peer.peer_addr) == ESP_OK ) {
		  println("Pair cleaned - Success");
    }

    // prepare for pairing - RC receiver needs to set ifidx
    peer.channel = ESPNOW_CHANNEL;  // pick a channel
    peer.encrypt = 0;               // no encryption
    if (role ==RC_RECEIVER ) { 
      peer.ifidx = ESP_IF_WIFI_AP;
    }

    // do pairing
    switch (esp_now_add_peer(&peer)) {
      case ESP_OK :
        println("Paired Success.");
        peer_status = PEER_PAIRED;
        break;
      default:
        println("Paird Failed - " + String());
        peer_status = PEER_NOT_FOUND;
        break;
    }
  } 
}



void ESPNOWRemoteControl::send_data(uint8_t *message) {
  esp_err_t result;
  result = esp_now_send(peer.peer_addr, message, MESSAGE_SIZE);
  if (result == ESP_OK) {
    println("Sending success.");
  } else {
    peer_status = PEER_ERROR;
    println("Cannot send data.");
  }
}

String ESPNOWRemoteControl::recv_data() {
  String str = (char*)message_recv;
  return str;
}


void ESPNOWRemoteControl::on_datasent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  println("Last Packet Sent to: " + mac2str(mac_addr));
  println("Last Packet Send Status: " + String(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail"));
  if (status != ESP_NOW_SEND_SUCCESS) {
    peer_status = PEER_ERROR;
    println("Error of sending data. (ack)");
  }

}

void ESPNOWRemoteControl::on_datarecv(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  if (!is_mac_set(peer.peer_addr) || peer_status == PEER_NOT_FOUND ) {
    memcpy( &peer.peer_addr, mac_addr, 6 );
    peer_status = PEER_FOUND;
  };
  println(mac2str(peer.peer_addr));
  println("Last Packet Recv from: " + mac2str(peer.peer_addr));
  println("Last Packet Recv Data Length: " + String(data_len));
  memcpy(message_recv,data,data_len);
}


/* 
 * ========================================================
 *   Common Utility Functions
 *   
 * 
 * 
 * ==========================================================
 */
void ESPNOWRemoteControl::println(String message) {
  if (DEBUG) {
    Serial.println("debug: " + message);
  }
}

String ESPNOWRemoteControl::mac2str(const uint8_t *mac_addr) {
  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  return mac_str;
}


bool ESPNOWRemoteControl::is_mac_set(const uint8_t *mac_addr) {
  // Check the MAC has been set
  int n = 6;
  while(--n>0 && mac_addr[n]==mac_addr[0]);
    return n!=0;
}