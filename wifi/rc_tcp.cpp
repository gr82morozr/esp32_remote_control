#include <WiFi.h>
#include "rc_tcp.h"




boolean TCPRemoteControl::initReceiver(int connection) {
  this->Role = RECVR_ROLE;
  this->MessageCount = 0;
  switch (connection) {
    case CXN_TCP: 
      return TCPRemoteControl::initTCPReceiver();
      break;
    case CXN_BLE: 
      return false;
      break;
    case CXN_ESPNOW: 
      return false;
      break;
    case CXN_NRF: 
      return false;
      break;
    default: 
      return false;
  }
}


boolean TCPRemoteControl::initController(int connection) {
  this->Role = CNTLR_ROLE;
  this->MessageCount = 0;
  switch (connection) {
    case CXN_TCP: 
      return TCPRemoteControl::initTCPController();
      break;
    case CXN_BLE: 
      return false;
      break;
    case CXN_ESPNOW: 
      return false;
      break;
    case CXN_NRF: 
      break;
    default: 
      return false;
  }
}


/* 
 * ============================================================================
 *
 * 
 * 
 *  
 * 
 * ============================================================================
*/

boolean TCPRemoteControl::initTCPController() {
  Serial.begin(DEBUG_SERIAL_BAUD_RATE);
  WiFi.begin(RECVR_SSID, RECVR_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("...");
  }

  Serial.print("WiFi connected with IP: ");
  Serial.println(WiFi.localIP());
  return true;
}


boolean TCPRemoteControl::initTCPReceiver() {
  boolean config_success = false;
  this->Server = WiFiServer(RECVR_PORT);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(RECVR_SSID, RECVR_PASSWORD);
  delay(DELAY_MS);
  config_success = WiFi.softAPConfig(RECVR_IP, RECVR_IP_GTWY, RECVR_IP_MASK);
  Serial.println(WiFi.softAPIP());
  this->Server.begin();
  delay(DELAY_MS);
  this->Client = this->Server.available();
  return config_success;
}






boolean TCPRemoteControl::checkClientConnections() {
  if (this->Server.hasClient())   {
    if (this->Client.connected())   {
      Serial.println("Connection rejected");
      this->Server.available().stop();
      return false;
    } else {
      Serial.print("Connection accepted: ");
      Serial.println(this->Client.remoteIP());
      this->Server.available().stop();
      return true;
    }
  } else {
    this->Client = this->Server.available();
    return false;
  }
}

void TCPRemoteControl::sendMessage(String message) {
  switch (this->Role)  {
    case RECVR_ROLE:
      if (this->Client) {
        this->Client.println(message);
      }
      break;
    case CNTLR_ROLE:
      if (this->Client.connect(RECVR_IP, RECVR_PORT)) {
        this->MessageCount ++;
        this->Client.println(message);
      } else {
        Serial.println("Connection to host failed");
      }
      break;
    default:
      break;
  }
}


void TCPRemoteControl::recvMessage() {
  switch (this->Role)  {
    case RECVR_ROLE:
      if (this->Client.connected() && this->Client.available()) {
        this->MessageCount ++;
        this->ReceivedMessage = this->Client.readStringUntil('\n');
        this->Client.flush();
        Serial.println("Receiver - Received : " +  this->ReceivedMessage);
      } else {
        this->Client = this->Server.available();
      }
      break;
    case CNTLR_ROLE:
      if (this->Client.connected()) {
        this->ReceivedMessage = this->Client.readStringUntil('\n');
        this->Client.flush();
        //Serial.println("Controller - Received : " +  line);
      }
      break;

    default:
      break;
  }

}