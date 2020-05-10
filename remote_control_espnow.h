#include <Arduino.h>


#ifndef ESP32_REMOTE_CONTROL_ESPNOW_H
#define ESP32_REMOTE_CONTROL_ESPNOW_H


#include <esp_now.h>
#include <WiFi.h>

// Common marcos
#define RC_CONTROLLER     1
#define RC_RECEIVER       2
#define MESSAGE_SIZE      10


#define DEBUG             0
#define SERIAL_BAUD_RATE  115200


// ESPNOWSpecific marcos
#define ESPNOW_CHANNEL    1
#define WIFI_SSID         "ESP32-RC-WLAN"
#define WIFI_PASSWORD     "vdjfiend#d0%d"

// Peer status enum 
#define PEER_ERROR        -1
#define PEER_NOT_FOUND    0
#define PEER_FOUND        1
#define PEER_PAIRED       2


/* 
 * ========================================================================
 * ESPNow Two Way Comminication Remote Controller/Receiver Libary
 * Updated : 
 *  - ver 1.0 : 2020-05-08 - initial build
 * 
 * 
 * Controller -> ESPNOW Master
 * Receiver   -> ESPNOW Slave 
 *  
 * Notes:  
 *  - Allows two way communications between Controller and Receiver
 *    So it doesn't really matter which ESP32 is Controller or Receover   
 * 
 * Steps:
 *  - Receiver hosts an WIFI network.
 *  - Controller connects to the WIFI network.
 *  - Once connected, Controller sends a 'handshake' message for Receiver
 *    to get the Controller's MAC address.
 *  - Ready for two way communications.
 *  
 * 
 * 
 * ============================================================
*/ 

class ESPNOWRemoteControl {
  public:
    ESPNOWRemoteControl(int role);
    void init(void);
    bool check_connection(void);

    void send_data(uint8_t *data);
    String recv_data();
    static uint8_t message_recv[MESSAGE_SIZE];
    

  private:
    static int role;

    static int peer_status;
    static esp_now_peer_info_t peer;
    
    void init_network();
    void config_ap();
    void scan_network();
    void pair_peer();
    static void println(String message);
    static String mac2str(const uint8_t *mac_addr);
    static bool is_mac_set(const uint8_t *mac_addr);
    static void on_datasent(const uint8_t *mac_addr, esp_now_send_status_t status) ;
    static void on_datarecv(const uint8_t *mac_addr, const uint8_t *data, int data_len);

};









#endif