#include <Arduino.h>


#ifndef ESP32_REMOTE_CONTROL_ESPNOW_H
#define ESP32_REMOTE_CONTROL_ESPNOW_H


#include <esp_now.h>
#include <WiFi.h>

// Common marcos
#define RC_CONTROLLER  1
#define RC_RECEIVER    2

#define DEBUG          1
#define DEBUG_SERIAL_BAUD_RATE 115200


// ESPNOWSpecific marcos
#define ESPNOW_CHANNEL    1
#define WIFI_SSID           "ESP32-RC-WLAN"
#define WIFI_PASSWORD       "123456789"

#define RCVR_NOT_FOUND    0
#define RCVR_FOUND        1
#define RCVR_PAIRED       2




class ESPNOWRemoteControl {
  public:
    ESPNOWRemoteControl(int role);
    esp_now_peer_info_t receiver;
    void init(void);
    bool check_connection(void);
    
    void send_data();
    void recv_data();
    
    


  private:
    int role;
    int receiver_status;
    void init_ESPNow();
    void config_DeviceAP();
    void scan_Receivers();
    void pair_Receiver();
};









#endif