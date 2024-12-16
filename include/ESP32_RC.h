#pragma once
#include <Arduino.h>
#include <ESP32_RC_Common.h>
#include <Task.h>
#include <freertos/timers.h>
#include <queue>


/*
 *
 * Remote Control Library
 * 
 * ESP32RemoteControl : Abstract Class
 * Support below protocols :
 * - ESPNOW
 * - Wifi 
 * 
 * - BLE (to do)
 * - Bluetooth Serial (to do)
 * - NRD24 (to do)
 *
*/


class ESP32RemoteControl : public Task {
  public:
    // pointer function 
    typedef void (*funcPtrType)(void);                    // a function pointer type
        
    // constructor
    ESP32RemoteControl(bool fast_mode, bool debug_mode);  
    
    // common functions
    virtual void init(void)               = 0;            // general wrapper to init the RC configuration
    virtual void connect(void)            = 0;            // general wrapper to establish the connection
    virtual void send(Message data)       = 0;            // general wrapper to send data
    virtual Message recv(void)            = 0;            // general wrapper to receive data
    
    void enable_fast(bool mode);                          // fast mode enabled, non-blocking
    void enable_debug(bool mode);                         // debug mode enabled, output debug info

    funcPtrType custom_handler            = nullptr;      // A Custom Exception Handler.

  protected:

    // common settings
    struct Metric {
      unsigned long in_count;
      unsigned long out_count;
      unsigned long err_count;
    };

    Metric send_metric  = {0, 0, 0};
    Metric recv_metric  = {0, 0, 0};

    int connection_status;                                // connection status
    int send_status;                                      // send status

    bool fast_mode      = false;                          // enable or disable quick mode
    bool debug_mode     = false;                          // enable or disable debug mode

    SemaphoreHandle_t mutex;                              // for access varible locking
      
    TimerHandle_t send_timer;                             // Timer task to send message
    TimerHandle_t recv_timer;                             // Timer task to recieve message
    TimerHandle_t heartbeat_timer;                        // Timer task to send heartbeat message
    
    QueueHandle_t send_queue;                             // send message queue
	  QueueHandle_t recv_queue;                             // recv message queue        

    Message create_sys_msg(String data);                  // convert gaven String to Message
    String extract_sys_msg(const Message &msg);           // extract Message.sys data to String

    void set_value(int *in_varible, int value);           // thread-safe to set varible 
    void get_value(int *in_varible, int *out_varible);    // thread-safe to get varible 

    void empty_queue(QueueHandle_t queue);                // clean up all messages in queue
    bool en_queue(QueueHandle_t queue, Message *pmsg);    // Push to queue
    bool de_queue(QueueHandle_t queue, Message *pmsg);    // Pop from queue
    int get_queue_depth(QueueHandle_t queue);             // get queue depth

    virtual bool handshake(void)          = 0;
    virtual void send_queue_msg(void)     = 0;
    virtual bool op_send(Message msg)     = 0;

    // ******************************************************************************* //
    // internal functions
    // ******************************************************************************* //

    void debug(String func_name, String message) ;        // Output debug info
    void raise_error(String func_name, String message) ;  // Common method to raise error.
    String mac2str(const uint8_t *mac_addr) ;             // Convert MAC address to String


  private :
    String exception_message;
    void handle_exception();                              // Exception handler
    String format_time(unsigned long ms);
    String format_time();
    bool is_serial_set = false;

};


