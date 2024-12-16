#include <ESP32_RC_Message.h>


/*
  ESP32 Remote Control Wrapper Library
  - Description  
*/
#define _FUNC_NAME              String(__func__) 
#define _DEBUG_(msg)            debug(_FUNC_NAME, (msg))
#define _ERROR_(msg)            raise_error(_FUNC_NAME, (msg))
#define _DELAY_(x)              vTaskDelay(pdMS_TO_TICKS(x))

/* 
  Remote Control Roles 
  Only support two nodes (in pair), not multi-executors
*/

#define _ROLE_CONTROLLER        1           // RC Controller Role
#define _ROLE_EXECUTOR          2           // RC Executor Role


#define _MAX_MSG_LEN            250         // max length of each message

/* 
  ESP32 supported Wireless protocols 
*/
#define _ESP32_NOW              1
#define _ESP32_BLE              2
#define _ESP32_WLAN             3
#define _ESP32_NRF24            4

/* 
  Status Code for possible scenarios 
  - Different Wireless devices may have different status
  - Mainly classified by 3 : [Success], [in Progress], [Error]

*/

#define _STATUS_CONN_OK           2
#define _STATUS_CONN_IN_PROG      200
#define _STATUS_CONN_ERR          -2

#define _STATUS_SEND_READY        4
#define _STATUS_SEND_DONE         5
#define _STATUS_SEND_IN_PROG      400
#define _STATUS_SEND_ERR          -4



#define _HANDSHAKE_MSG            "ESP32_RC_HANDSHAKE_HELLO"
#define _HANDSHAKE_ACK_MSG        "ESP32_RC_HANDSHAKE_ACK"

#define _HEARTBEAT_MSG            "ESP32_RC_HEARTBEAT_HELLO"
#define _HEARTBEAT_ACK_MSG        "ESP32_RC_HEARTBEAT_ACK"


#define _ESP32_RC_DATA_RATE       100                         // X messages/second , better <=100
#define ESP32_RC_HEARTBEAT_RATE   0.5                         // X messages/second

/* =========   ESPNOW  Settings ========= */
#define _ESPNOW_CHANNEL           2
#define _ESPNOW_OUTPUT_POWER      82                          // [0, 82] representing [0, 20.5]dBm


#define _RC_QUEUE_DEPTH           int(_ESP32_RC_DATA_RATE/2)    // keep messages queue for max 0.5s only, if overflow, drop the older ones


/* =========   BLE  Settings ========= */
#define _BLE_SVR_DEVICE_NAME      "ESP32_RC_SERVER"
#define _BLE_CLT_DEVICE_NAME      "ESP32_RC_CLIENT"
#define _BLE_SERVICE_UUID         "4fafc201-1fb5-459e-8fac-c5c9c331914b"
#define _BLE_TX_CHARAC_UUID       "beb5483e-36e1-4688-b4f5-ea07361b26a8"
#define _BLE_RX_CHARAC_UUID       "6d753264-c8fc-4ea7-bcee-4c450e67b02f"                               




/* =========   ESP32 - NRF24 Specific Settings ========= */
// For ESP32, needs to define the SPI pins
#define HSPI_MISO             12  // MISO pin
#define HSPI_MOSI             13  // MOSI pin
#define HSPI_SCLK             14  // Clock Pin
#define HSPI_CS               15  // Chip Selection Pin
#define NRF24_CE              4   // Chip Enable Pin
