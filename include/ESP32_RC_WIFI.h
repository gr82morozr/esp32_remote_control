#pragma once
#include <Arduino.h>
#include <ESP32_RC.h>
#include <WiFi.h>

#define ESP32_RC_TCP_PORT  8888
#define ESP32_RC_SSID      "ESP32_RC_LAN"
#define ESP32_RC_PASSWORD  "today123"



class ESP32_RC_WIFI : public ESP32RemoteControl {
  public:
    // Constructor
    ESP32_RC_WIFI(bool fast_mode, bool debug_mode);
    ~ESP32_RC_WIFI();

    // Implement virtual functions
    void init(void) override;         // Initialize WiFi configuration
    void connect(void) override;      // Establish WiFi connection (auto-switch role)
    void send(Message data) override; // Send data over WiFi
    Message recv(void) override;      // Receive data over WiFi

  private:
    static ESP32_RC_WIFI* instance;
    WiFiServer server;                // WiFi Server (AP mode)
    WiFiClient client;                // WiFi Client (STA mode)
    bool is_ap = false;               // Tracks if the ESP32 is in AP mode
    void run(void* data) override;    // Override the Task class run function
    void send_queue_msg(void) override;       // send msg in send_queue

    // Timers Tasks
    static void send_timer_callback(TimerHandle_t xTimer) ;
    static void heartbeat_timer_callback(TimerHandle_t xTimer) ; 

};


