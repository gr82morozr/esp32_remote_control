#include <Arduino.h>
#include "rc_ble.h"


int BLERemoteControl::role;
int BLERemoteControl::status;
uint8_t BLERemoteControl::data_sent[MAX_MSG_SIZE];
uint8_t BLERemoteControl::data_recv[MAX_MSG_SIZE];
String BLERemoteControl::data_sent_str;
String BLERemoteControl::data_recv_str;

// for server
BLEServer* BLERemoteControl::p_server;
BLEService* BLERemoteControl::p_service;
BLECharacteristic* BLERemoteControl::p_rx_charcs;
BLECharacteristic* BLERemoteControl::p_tx_charcs;
BLEAdvertising* BLERemoteControl::p_advertising;

// for client
BLEClient*  BLERemoteControl::p_client;
BLEAdvertisedDevice* BLERemoteControl::server_device;
BLEScan* BLERemoteControl::p_scanner;
BLEUUID BLERemoteControl::service_uuid;
BLEUUID BLERemoteControl::rx_charcs_uuid;
BLEUUID BLERemoteControl::tx_charcs_uuid;
BLERemoteService* BLERemoteControl::p_peer_service;
BLERemoteCharacteristic* BLERemoteControl::p_peer_tx_charcs;
BLERemoteCharacteristic* BLERemoteControl::p_peer_rx_charcs;

/*
 * ================================================================
 *  External Callback functions 
 * 
 * ================================================================
 */
class Server_Callbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      BLERemoteControl::status = SERVER_CONNECTED;
      BLERemoteControl::debug("Server is connected");
    };

    void onDisconnect(BLEServer* pServer) {
      BLERemoteControl::status = SERVER_NOT_CONNECTED;
      BLERemoteControl::debug("Server is not connected");
    }
};




class Server_CharcsCallback: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
      std::string rxValue = pCharacteristic->getValue();

      if (rxValue.length() > 0) {
        Serial.println("*********");
        Serial.print("Received Value: ");
        for (int i = 0; i < rxValue.length(); i++)
          Serial.print(rxValue[i]);

        Serial.println();
        Serial.println("*********");
      }
    }

};


/*
 * ================================================================
 *  External Callback functions - For client
 * 
 * ================================================================
 */

class Client_CallBack : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    BLERemoteControl::debug("onConnect");
  }

  void onDisconnect(BLEClient* pclient) {
    BLERemoteControl::debug("onDisconnect");
  }
};


class Advertise_DeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    //BLERemoteControl::debug("BLE Found: " + advertisedDevice.toString());
    BLERemoteControl::debug(advertisedDevice.toString().c_str() );
    
    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(BLERemoteControl::service_uuid)) {
      BLEDevice::getScan()->stop();
      BLERemoteControl::server_device = new BLEAdvertisedDevice(advertisedDevice);
      BLERemoteControl::debug(BLERemoteControl::server_device->getAddress().toString().c_str());
    } // Found our server
    
  } 
}; 




/*
 * ================================================================
 *  Constructor
 * 
 * ================================================================
 */


BLERemoteControl::BLERemoteControl(int r) {
  Serial.begin(SERIAL_BAUD_RATE);
  role  = r;
  service_uuid = BLEUUID(SERVICE_UUID);
  rx_charcs_uuid = BLEUUID(CHARC_RX_UUID);
  tx_charcs_uuid = BLEUUID(CHARC_TX_UUID);
  memset(data_sent, 0, MAX_MSG_SIZE);
  memset(data_recv, 0, MAX_MSG_SIZE);
  data_sent_str = "";
  data_recv_str = "";  
};

/*
 * ================================================================
 *  All public functions
 * 
 * ================================================================
 */


void BLERemoteControl::init() {
  switch (role) {
    case RC_CONTROLLER: 
      init_server();
      do_advertising();
      break;
    case RC_RECEIVER:
      do_scan();
      init_client();
      connect_server();
      //read_server();
      break;
    default:
      break;
  }
}


bool BLERemoteControl::check_connection() {
  switch (role) {
    case RC_CONTROLLER: 
      return (status == SERVER_CONNECTED );      
      break;
    case RC_RECEIVER:

      break;
    default:
      break;
  }
  return false;
}

void BLERemoteControl::send_data(String message) {

  int data_length = message.length();


  switch (role) {
    case RC_CONTROLLER:
      do_notify(message);
      break;
    case RC_RECEIVER:
      do_write();
      break;
    default:
      break;
  }

}


String BLERemoteControl::recv_data() {


    return "";
}

/*
 * ================================================================
 *  Server functions
 * 
 * ================================================================
 */


void BLERemoteControl::init_server() {
  status = SERVER_INIT;
  BLEDevice::init(DEVICE_NAME);
  p_server = BLEDevice::createServer();
  p_server->setCallbacks(new Server_Callbacks());

  p_service = p_server->createService(SERVICE_UUID);
  p_tx_charcs = p_service->createCharacteristic(
              CHARC_TX_UUID,
              BLECharacteristic::PROPERTY_READ   |
              BLECharacteristic::PROPERTY_WRITE  |
              BLECharacteristic::PROPERTY_NOTIFY |
              BLECharacteristic::PROPERTY_INDICATE
            );

  p_tx_charcs->setValue(SERVER_HANDSHAKE_MSG); 
  p_tx_charcs->addDescriptor(new BLE2902());

  p_rx_charcs = p_service->createCharacteristic(
							CHARC_RX_UUID,
              BLECharacteristic::PROPERTY_READ   |
              BLECharacteristic::PROPERTY_WRITE  |
              BLECharacteristic::PROPERTY_NOTIFY |
              BLECharacteristic::PROPERTY_INDICATE
						);

  p_rx_charcs->setCallbacks(new Server_CharcsCallback());
  
  // Start the service
  p_service->start();
  
}

void BLERemoteControl::do_advertising() {

  // Start advertising
  p_advertising = BLEDevice::getAdvertising();
  p_advertising->addServiceUUID(SERVICE_UUID);
  p_advertising->setScanResponse(true);
  p_advertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  p_advertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  // Ready to be connected
  status = SERVER_NOT_CONNECTED;
  debug("Server is not connected");
}


void BLERemoteControl::do_notify(String message) {
  int data_length = message.length();
  debug(message);
  // set max message length
  if (data_length > MAX_MSG_SIZE) {
    data_length = MAX_MSG_SIZE;
  };

  // convert to uint8_t type.
  for (int i = 0; i < data_length; i++) {
    data_sent[i] = (uint8_t)message[i];
  };

  p_tx_charcs->setValue(data_sent, data_length);
  p_tx_charcs->notify();
}


/*
 * ================================================================
 *  Client functions
 * 
 * ================================================================
 */

// Client receive notification from server a adn then invoke this call back.
void BLERemoteControl::client_notify_callback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  Serial.print("Notify callback for characteristic ");
  Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
  Serial.print(" of data length ");
  Serial.println(length);
  Serial.print("data: ");
  Serial.println((char*)pData);
}

void BLERemoteControl::do_scan() {
  BLEDevice::init("");
  p_scanner = BLEDevice::getScan();
  p_scanner->setAdvertisedDeviceCallbacks(new Advertise_DeviceCallbacks());
  p_scanner->setInterval(1349);
  p_scanner->setWindow(449);
  p_scanner->setActiveScan(true);
  p_scanner->start(5, false);
}

void BLERemoteControl::init_client() {
  p_client  = BLEDevice::createClient();
  p_client->setClientCallbacks(new Client_CallBack());
}

void BLERemoteControl::connect_server() {
  p_client->connect(server_device); 
  p_peer_service = p_client->getService(service_uuid);
  if (p_peer_service == nullptr) {
    debug(service_uuid.toString().c_str());
    p_client->disconnect();
    return;
  }

  p_peer_tx_charcs = p_peer_service->getCharacteristic(tx_charcs_uuid);
  if (p_peer_tx_charcs == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(tx_charcs_uuid.toString().c_str());
    p_client->disconnect();
    return;
  }

  p_peer_rx_charcs = p_peer_service->getCharacteristic(rx_charcs_uuid);
  if (p_peer_rx_charcs == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(rx_charcs_uuid.toString().c_str());
    p_client->disconnect();
    return;
  }

  if (p_peer_tx_charcs->canNotify()) {
    p_peer_tx_charcs->registerForNotify(client_notify_callback);
  }

}


String BLERemoteControl::read_server() {
  if (p_peer_tx_charcs->canRead()) {
      std::string value = p_peer_tx_charcs->readValue();
      Serial.print("The characteristic value was: ");
      Serial.println(value.c_str());
  }
  return "aa";
}


void BLERemoteControl::do_write() {
  String newValue = "Time since boot: " + String(millis()/1000);
  p_peer_rx_charcs->writeValue(newValue.c_str(), newValue.length());
}


/* 
 * ========================================================
 *   Common Utility Functions
 *   
 * 
 * 
 * ==========================================================
 */

void BLERemoteControl::debug(String msg) {
  if (DEBUG) {
    Serial.println(String(millis()) + " : " + msg);
  }
}