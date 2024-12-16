#pragma once
#include <Arduino.h>
#include <ESP32_RC.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>

/*
 *
 * ESPNOW Class
 * 
 * Bi-directional communication via ESPNOW
 * 
 * Note:
 *  role : this is un-necessary for ESPNOW.
 *  fast_mode = false:  The send process is blocking when send_queue is full. This ensures the message delivery.
 *  fast_mode = true:   The send process is non-blocking when send_queue is full. The first message of send_queue will 
 *                      be removed and then en-queue the new messaage. This is to ensure the quick response of client, not getting blocked.
 *                          
 * 
 * 
 *
 */


class ESP32_RC_ESPNOW : public ESP32RemoteControl {
  public:
    ESP32_RC_ESPNOW(bool fast_mode=false, bool debug_mode=false); 
    ~ESP32_RC_ESPNOW();
    
    void init(void) override;
    void connect(void) override;                // general wrapper to establish the connection
    void send(Message data) override;           // only en-queue the message
    Message recv(void) override;                // general wrapper to receive data

  
  private:
    void run(void* data) override;              // Override the Task class run function
    void send_queue_msg(void) override;         // send msg in send_queue
    bool handshake(void) override;              // Handshake process
    bool op_send(Message msg) override;         // Send operation
    static ESP32_RC_ESPNOW* instance;           // instance pointer


    // ======== ESPNOW specific section ===========
    static uint8_t broadcast_addr[6];
    esp_now_peer_info_t peer;
 
    static void send_timer_callback(TimerHandle_t xTimer) ; 
    static void heartbeat_timer_callback(TimerHandle_t xTimer) ; 

    void pair_peer(const uint8_t *mac_addr);   // ESPNOW - pairing peer
    void unpair_peer(const uint8_t *mac_addr); // ESPNOW - un-pairing peer
    
    static void static_on_datasent(const uint8_t *mac_addr, esp_now_send_status_t status);
    static void static_on_datarecv(const uint8_t *mac_addr, const uint8_t *data, int data_len);
    void on_datasent(const uint8_t *mac_addr, esp_now_send_status_t status) ;
    void on_datarecv(const uint8_t *mac_addr, const uint8_t *data, int data_len);

};


