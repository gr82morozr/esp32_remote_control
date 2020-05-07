#include <Arduino.h>
#include "remote_control_espnow.h"







void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print("Last Packet Sent to: "); Serial.println(macStr);
  Serial.print("Last Packet Send Status: "); Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print("Last Packet Recv from: "); Serial.println(macStr);
  Serial.print("Last Packet Recv Data: "); Serial.println(*data);
  Serial.println("");
}

ESPNOWRemoteControl::ESPNOWRemoteControl(int role) {
  Serial.begin(DEBUG_SERIAL_BAUD_RATE);

  this->role = role;
  this->receiver_status = RCVR_NOT_FOUND;
  Serial.println("RC Role: " + String(this->role));
}




void ESPNOWRemoteControl::init(void) {
  this->init_ESPNow();


}


bool ESPNOWRemoteControl::check_connection(void) {
  while (this->receiver_status != RCVR_PAIRED) {
    Serial.println("Checking... (status=" + String(receiver_status) + ")" );
    switch (this->receiver_status) {
      case RCVR_NOT_FOUND:
        this->scan_Receivers();
        break;
      case RCVR_FOUND:
        this->pair_Receiver();
        break;
      case RCVR_PAIRED:
        break;
      default:
        this->receiver_status = RCVR_NOT_FOUND;
        break;
    };
  };
  return this->receiver_status;  
}



// Init ESP Now with fallback
void ESPNOWRemoteControl::init_ESPNow() {

  switch (this->role) {
    case RC_CONTROLLER:
      WiFi.mode(WIFI_STA);
      break;
    case RC_RECEIVER:
      WiFi.mode(WIFI_AP);
      this->config_DeviceAP();

      break;
    default:
      Serial.println("Error: No Role is defined.");
      delay(1000);
      ESP.restart();
      break;
  }

  WiFi.disconnect();

  if (esp_now_init() == ESP_OK) {
    Serial.println("ESPNow Init Success");
  } else {
    Serial.println("ESPNow Init Failed");
    delay(1000);
    ESP.restart();
  }
  
  
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  

}


void ESPNOWRemoteControl::config_DeviceAP(void) {
  bool result = WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, ESPNOW_CHANNEL, 0);
  if (!result) {
    Serial.println("AP Config failed.");
  } else {
    Serial.println("AP Config Success. Broadcasting with AP: " + String(WIFI_SSID));
  }
}


void ESPNOWRemoteControl::scan_Receivers() {
  int8_t scanResults = WiFi.scanNetworks();
  this->receiver_status = RCVR_NOT_FOUND;
  memset(&this->receiver, 0, sizeof(this->receiver));
  if (scanResults > 0) {
    Serial.println("Found " + String(scanResults) + " devices ");

    for (int i = 0; i < scanResults; ++i) {
      // Print SSID and RSSI for each device found
      String SSID = WiFi.SSID(i);
      int32_t RSSI = WiFi.RSSI(i);
      String BSSIDstr = WiFi.BSSIDstr(i);

      if (DEBUG) {
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.print(SSID);
        Serial.print(" (");
        Serial.print(RSSI);
        Serial.print(")");
        Serial.println("");
      }
      delay(10);
      // Check if the current device starts with `Slave`
      if (SSID.indexOf(WIFI_SSID) == 0) {
        Serial.print("Found a receiver:");
        Serial.print(i + 1); Serial.print(": "); Serial.print(SSID); Serial.print(" ["); Serial.print(BSSIDstr); Serial.print("]"); Serial.print(" ("); Serial.print(RSSI); Serial.print(")"); Serial.println("");
        // Get BSSID => Mac Address of the Receiver
        int mac[6];
        if ( 6 == sscanf(BSSIDstr.c_str(), "%x:%x:%x:%x:%x:%x",  &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5] ) ) {
          for (int ii = 0; ii < 6; ++ii ) {
            this->receiver.peer_addr[ii] = (uint8_t) mac[ii];
          }
        }

        this->receiver.channel = ESPNOW_CHANNEL; // pick a channel
        this->receiver.encrypt = 0; // no encryption
        this->receiver_status = RCVR_FOUND;
        break;
      }
    }
  } else {
    Serial.println("No WiFi devices in AP Mode found");
    delay(1000);
    ESP.restart();
  }
  
  // clean up ram
  WiFi.scanDelete();
}

void ESPNOWRemoteControl::pair_Receiver() {
  this->receiver_status = esp_now_is_peer_exist(this->receiver.peer_addr);

  if ( this->receiver_status != RCVR_PAIRED) {
    esp_err_t pair_Status = esp_now_add_peer(&this->receiver);
    switch (pair_Status) {
      case ESP_OK :
        Serial.println("Pair Receiver Success.");
        this->receiver_status = RCVR_PAIRED;
        break;
      default:
        Serial.println("Pair Receiver Failed - " + String());
        this->receiver_status = RCVR_NOT_FOUND;
        break;
    }
  } 
}



void ESPNOWRemoteControl::send_data() {
  uint8_t data = millis();
  const uint8_t *peer_addr = this->receiver.peer_addr;
  Serial.print("Sending: "); Serial.println(data);
  esp_err_t result = esp_now_send(peer_addr, &data, sizeof(data));
  Serial.print("Send Status: ");
  if (result == ESP_OK) {
    Serial.println("Success");
  } else if (result == ESP_ERR_ESPNOW_NOT_INIT) {
    // How did we get so far!!
    Serial.println("ESPNOW not Init.");
  } else if (result == ESP_ERR_ESPNOW_ARG) {
    Serial.println("Invalid Argument");
  } else if (result == ESP_ERR_ESPNOW_INTERNAL) {
    Serial.println("Internal Error");
  } else if (result == ESP_ERR_ESPNOW_NO_MEM) {
    Serial.println("ESP_ERR_ESPNOW_NO_MEM");
  } else if (result == ESP_ERR_ESPNOW_NOT_FOUND) {
    Serial.println("Peer not found.");
  } else {
    Serial.println("Not sure what happened");
  }
}