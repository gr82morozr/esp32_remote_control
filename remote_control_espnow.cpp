#include <Arduino.h>
#include "remote_control_espnow.h"

/*
 * ==============================================================================
 *
 *  Class functions
 * 
 * 
 * 
 * =============================================================================
*/
 
// RemoteControl constructor  
ESPNOWRemoteControl::ESPNOWRemoteControl() {
  Serial.begin(115200);
  Serial.println("asdasdasdsa");



  WiFi.mode(WIFI_MODE_APSTA);

  if (esp_now_init() == ESP_OK) {
    Serial.println("ESPNow Init Success");
  }
  else {
    Serial.println("ESPNow Init Failed");
    ESP.restart();
  }
}


void ESPNOWRemoteControl::ScanPartners() {
  int8_t scanResults = WiFi.scanNetworks();
  //reset slaves
  memset(this->partners, 0, sizeof(this->partners));
  this->partner_count = 0;
  Serial.println("");
  if (scanResults == 0) {
    Serial.println("No WiFi devices in AP Mode found");
  } else {
    Serial.print("Found "); Serial.print(scanResults); Serial.println(" devices ");
    for (int i = 0; i < scanResults; ++i) {
      // Print SSID and RSSI for each device found
      String SSID = WiFi.SSID(i);
      int32_t RSSI = WiFi.RSSI(i);
      String BSSIDstr = WiFi.BSSIDstr(i);

      if (PRINTSCANRESULTS) {
        Serial.print(i + 1); Serial.print(": "); Serial.print(SSID); Serial.print(" ["); Serial.print(BSSIDstr); Serial.print("]"); Serial.print(" ("); Serial.print(RSSI); Serial.print(")"); Serial.println("");
      }
      delay(10);
      // Check if the current device starts with `Slave`
      if (SSID.indexOf("ESPNOW") == 0) {
        // SSID of interest
        Serial.print(i + 1); Serial.print(": "); Serial.print(SSID); Serial.print(" ["); Serial.print(BSSIDstr); Serial.print("]"); Serial.print(" ("); Serial.print(RSSI); Serial.print(")"); Serial.println("");
        // Get BSSID => Mac Address of the Slave
        int mac[6];

        if ( 6 == sscanf(BSSIDstr.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",  &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5] ) ) {
          for (int ii = 0; ii < 6; ++ii ) {
            this->partners[this->partner_count].peer_addr[ii] = (uint8_t) mac[ii];
          }
        }
        this->partners[this->partner_count].channel = CHANNEL_MASTER; // pick a channel
        this->partners[this->partner_count].encrypt = 0; // no encryption
        this->partner_count++;
      }
    }
  }

  if (this->partner_count > 0) {
    Serial.print(this->partner_count); Serial.println(" Slave(s) found, processing..");
  } else {
    Serial.println("No Slave Found, trying again.");
  }

  // clean up ram
  WiFi.scanDelete();
}


void ESPNOWRemoteControl::initController() {
  this->ScanPartners();

}

void ESPNOWRemoteControl::initReceiver() {
  this->ScanPartners();
  
}



// Check if the slave is already paired with the master.
// If not, pair the slave with master
void ESPNOWRemoteControl::managePartners() {
  if (this->partner_count > 0) {
    for (int i = 0; i < this->partner_count; i++) {
      const esp_now_peer_info_t *peer = &this->partners[i];
      const uint8_t *peer_addr = this->partners[i].peer_addr;
      Serial.print("Processing: ");
      for (int ii = 0; ii < 6; ++ii ) {
        Serial.print((uint8_t) this->partners[i].peer_addr[ii], HEX);
        if (ii != 5) Serial.print(":");
      }
      Serial.print(" Status: ");
      // check if the peer exists
      bool exists = esp_now_is_peer_exist(peer_addr);
      if (exists) {
        // Slave already paired.
        Serial.println("Already Paired");
      } else {
        // Slave not paired, attempt pair
        esp_err_t addStatus = esp_now_add_peer(peer);
        if (addStatus == ESP_OK) {
          // Pair success
          Serial.println("Pair success");
        } else if (addStatus == ESP_ERR_ESPNOW_NOT_INIT) {
          // How did we get so far!!
          Serial.println("ESPNOW Not Init");
        } else if (addStatus == ESP_ERR_ESPNOW_ARG) {
          Serial.println("Add Peer - Invalid Argument");
        } else if (addStatus == ESP_ERR_ESPNOW_FULL) {
          Serial.println("Peer list full");
        } else if (addStatus == ESP_ERR_ESPNOW_NO_MEM) {
          Serial.println("Out of memory");
        } else if (addStatus == ESP_ERR_ESPNOW_EXIST) {
          Serial.println("Peer Exists");
        } else {
          Serial.println("Not sure what happened");
        }
        delay(100);
      }
    }
  } else {
    // No slave found to process
    Serial.println("No Slave found to process");
  }
}





