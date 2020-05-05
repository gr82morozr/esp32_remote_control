#include <Arduino.h>


#ifndef ESP32_REMOTE_CONTROL_ESPNOW_H
#define ESP32_REMOTE_CONTROL_ESPNOW_H


#include <esp_now.h>
#include <WiFi.h>

/* 
 * ============================================================================
 *
 *  RC Controller - ESPNOW Server
 *    - Running and listening
 *    - detects client connections
 *    - Once client connected - send messages 
 * 
 * 
 *  RC Receiver   - ESPNOW CLient
 *    
 *  
 *  
 * 
 * 
 * ============================================================================
*/



#define NUM_PARTNERS        1

#define CHANNEL_MASTER      4
#define CHANNEL_SLAVE       1
#define PRINTSCANRESULTS    0
#define DATASIZE            120

class ESPNOWRemoteControl {
  public:
    ESPNOWRemoteControl();
    void ScanPartners();
    esp_now_peer_info_t partners[NUM_PARTNERS] = {};
    int partner_count;
    
    void initController();
    void initReceiver();
    void managePartners();
};









#endif