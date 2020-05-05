#include <Arduino.h>

#ifndef ESP32_REMOTE_CONTROL_BLE_H
#define ESP32_REMOTE_CONTROL_BLE_H

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>



/* 
 * ============================================================================
 *
 *  RC Controller - BLE Server
 *    - Running and listening
 *    - detects client connections
 *    - once client connected - send messages 
 *  RC Receiver   - BLE Client
 *    
 *  
 *  
 * 
 * 
 * ============================================================================
*/


// Controller AP config


#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" 
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define DEVICE_NAME            "ESP32_BLE_CNTLR"
#define MSG_SIZE               120  

#define CNTLR_ROLE       1
#define RECVR_ROLE       2

// All Connection (CXN) types defined here 
#define CXN_TCP          1
#define CXN_BLE          2
#define CXN_ESPNOW       3
#define CXN_NRF          4

#define DEBUG_SERIAL_BAUD_RATE 115200




class BLERemoteControl {
  public:
    BLERemoteControl();
    int Role;

    BLECharacteristic *pCharacteristic;
    BLEClient*  pClient  = BLEDevice::createClient();

    bool connectToServer(BLEAddress pAddress);
    char ReceivedMessage[MSG_SIZE];
    bool initController();
    bool initReceiver();
    bool initBLEController();
    bool initBLEReceiver();
    void sendMessage(String message);
    void recvMessage();

}; 








#endif