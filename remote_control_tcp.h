#ifndef ESP32_REMOTE_CONTROL_TCP_H
#define ESP32_REMOTE_CONTROL_TCP_H

#include <WiFi.h>


/* 
 * ============================================================================
 *
 *  RC Controller - TCP Client
 *  RC Receiver   - TCP Server
 *  
 * 
 * ============================================================================
*/


// Controller AP config


#define RECVR_SSID       "ESP32-RC-RECEIVER"
#define RECVR_PASSWORD   "123-456-789"
#define RECVR_PORT       18089



#define RECVR_IP         IPAddress(10, 10, 10, 1)
#define RECVR_IP_GTWY    IPAddress(10, 10, 10, 1)
#define RECVR_IP_MASK    IPAddress(255, 255, 255, 0)

// All Connection (CXN) types defined here 
#define CXN_TCP          1
#define CXN_BLE          2
#define CXN_ESPNOW       3
#define CXN_NRF          4

#define CNTLR_ROLE       1
#define RECVR_ROLE       2


#define MSG_SIZE         128        // pre-defined to be 128 to cover most of the usages

#define MESSAGE_CYCLE_MS 5        // 200 times per senond to publish message
#define DELAY_MS         500
#define DEBUG            1
#define DEBUG_SERIAL_BAUD_RATE 115200



class TCPRemoteControl {
  public:
    int Role;
    long MessageCount;
    WiFiServer Server;
    WiFiClient Client;
    
    String SentMessage;
    String ReceivedMessage;
    
    // Wrapper functions
    boolean initController(int connection);
    boolean initReceiver(int connection);

    boolean initTCPController();
    boolean initTCPReceiver();

    boolean checkClientConnections();
    void sendMessage(String message);
    void recvMessage();

    //boolean initBLEController();
    //boolean initBLEReceiver();

    //boolean initESPNowController();
    //boolean initESPNowReceiver();
  


}; 









#endif