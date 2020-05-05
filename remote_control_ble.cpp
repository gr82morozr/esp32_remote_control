#include <Arduino.h>
#include "remote_control_ble.h"


/* define global varibles used by multiple class here */
bool doConnect = false;
bool connected = false;
static BLEAddress *pServerAddress;

static BLEUUID  serviceUUID(SERVICE_UUID);
static BLEUUID  charUUID(CHARACTERISTIC_UUID_TX);
static BLERemoteCharacteristic* pRemoteCharacteristic;

// notify call back
static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic,  uint8_t* pData, size_t length, bool isNotify) {

}


class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    connected = true;
  };

  void onDisconnect(BLEServer* pServer) {
    connected = false;
  }
};




class ScanAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      Serial.print("BLE Advertised Devices found: ");
      Serial.println(advertisedDevice.toString().c_str());
      Serial.println(advertisedDevice.getName().c_str());

      if (advertisedDevice.getName() == DEVICE_NAME) { 
        Serial.println(advertisedDevice.getAddress().toString().c_str());
        advertisedDevice.getScan()->stop();
        pServerAddress = new BLEAddress(advertisedDevice.getAddress());
        doConnect = true;
      }
    }
};


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
BLERemoteControl::BLERemoteControl() {
  //doConnect = false;


}







bool BLERemoteControl::connectToServer(BLEAddress pAddress) {
  Serial.print("Forming a connection to ");
  Serial.println(pAddress.toString().c_str());

  this->pClient->connect(pAddress);
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  Serial.print(" - Connected to server :");
  Serial.println(serviceUUID.toString().c_str());
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    return false;
  }
  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  Serial.print(" - Found our characteristic UUID: ");
  Serial.println(charUUID.toString().c_str());
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    return false;
  }
  Serial.println(" - Found our characteristic");
  pRemoteCharacteristic->registerForNotify(notifyCallback);
}

bool BLERemoteControl::initReceiver() {
  this->Role = CNTLR_ROLE;
  return BLERemoteControl::initBLEReceiver();
}

bool BLERemoteControl::initController() {
  this->Role = RECVR_ROLE;
  return BLERemoteControl::initBLEController();
  
}



/* 
  ================================================================
  

  ================================================================ 
*/
bool BLERemoteControl::initBLEReceiver() {  
  Serial.begin(115200);
  BLEDevice::init(DEVICE_NAME);
  BLEScan* pBLEScan = BLEDevice::getScan();
  Serial.println("Scanning BLE Devices ... ");
  pBLEScan->setAdvertisedDeviceCallbacks(new ScanAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->start(10);
  
  
  if (connected == false) {
    if (this->connectToServer(*pServerAddress)) {
      Serial.println("We are now connected to the BLE Server.");
      connected = true;
    } else {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
      connected = false;
    }
  }
  return false;
}



bool BLERemoteControl::initBLEController() {
  Serial.begin(DEBUG_SERIAL_BAUD_RATE);
  BLEDevice::init(DEVICE_NAME);
  BLEServer *pServer = BLEDevice::createServer();
  
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID_TX,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );
                      
  pCharacteristic->addDescriptor(new BLE2902());
  //pCharacteristic->setCallbacks(new BLECallbacks());

  // Start the service
  pService->start();

  // Start advertising
  pServer->getAdvertising()->start();
  Serial.println("Waiting a client connection to notify...");

  // Create the BLE Device
  Serial.println("aa");
  //Serial.println(this->test[0] );
  return false;
}



/* Send Messages */


void BLERemoteControl::sendMessage(String message) {
  char ble_message[120];
  if (connected) {
      message.toCharArray(ble_message, MSG_SIZE);
      pCharacteristic->setValue(ble_message);
      pCharacteristic->notify(); // Send the value to the app!
      Serial.println("*** Sent Value: " +  message);
  }

  delay(100);
}



/* Receive Messages */


void BLERemoteControl::recvMessage() {
  if (connected) {
    std::string value = pRemoteCharacteristic->readValue();
    Serial.println(  value.c_str() );
  }
  
  delay(2);

}