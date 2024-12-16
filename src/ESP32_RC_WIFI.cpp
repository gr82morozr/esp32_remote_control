#include <ESP32_RC_WIFI.h>

ESP32_RC_WIFI* ESP32_RC_WIFI::instance = nullptr;


ESP32_RC_WIFI::ESP32_RC_WIFI(bool fast_mode, bool debug_mode)
    : ESP32RemoteControl(fast_mode, debug_mode), server(ESP32_RC_TCP_PORT) {

  ESP32_RC_WIFI::instance = this;
}

// Initialize WiFi configuration
void ESP32_RC_WIFI::init(void) {
  _DEBUG_("Initializing private WiFi...");
  WiFi.mode(WIFI_AP_STA);  // Enable both AP and STA modes
  _DEBUG_("WiFi initialized.");

  // Create queues
  send_queue = xQueueCreate(_RC_QUEUE_DEPTH, sizeof(Message));
  recv_queue = xQueueCreate(_RC_QUEUE_DEPTH, sizeof(Message));
  if (send_queue == NULL || recv_queue == NULL) {
    _ERROR_("Failed to create queues.");
  }

  // Create the mutex
  mutex = xSemaphoreCreateMutex();

  // Create Timer
  // For example : _ESP32_RC_DATA_RATE = 100 (times/sec)
  //              => int(1000/_ESP32_RC_DATA_RATE) = 10 (ms),  delay 10ms for each timer event
  send_timer = xTimerCreate("Timer", pdMS_TO_TICKS( int(1000/_ESP32_RC_DATA_RATE) ), pdTRUE, (void*)0, send_timer_callback);
  
  // heartbeat_timer triggers every 1 sec
  heartbeat_timer = xTimerCreate("Timer", pdMS_TO_TICKS( int(1000/ESP32_RC_HEARTBEAT_RATE) ), pdTRUE, (void*)0, heartbeat_timer_callback);

  if (send_timer == NULL || heartbeat_timer == NULL) {
    _ERROR_("Failed to create timer");
  }  
}

void ESP32_RC_WIFI::connect(void) {
  _DEBUG_("Setting up private WiFi...");

  // Try connecting as STA
  WiFi.begin(ESP32_RC_SSID, ESP32_RC_PASSWORD);
  int retry = 10;  // Retry limit
  while (WiFi.status() != WL_CONNECTED && retry > 0) {
    _DELAY(1000);
    _DEBUG_("Retrying STA mode...");
    retry--;
  }

  if (WiFi.status() == WL_CONNECTED) {
    _DEBUG_("Connected to AP as STA.");
    is_ap = false;
  } else {
    // Fall back to AP mode
    _DEBUG_("Switching to AP mode...");
    WiFi.softAP(ESP32_RC_SSID, ESP32_RC_PASSWORD);
    server.begin();
    is_ap = true;
    _DEBUG_("AP mode active.");
  }
}

void ESP32_RC_WIFI::send(Message data) {
  /*
  if (is_ap) {
    WiFiClient new_client = server.available();
    if (new_client) {
      new_client.write((uint8_t*)&data, sizeof(Message));
      _DEBUG_("Message sent to client.");
    } else {
      _ERROR_("No client connected.");
    }
  } else {
    if (client.connected()) {
      client.write((uint8_t*)&data, sizeof(Message));
      _DEBUG_("Message sent to AP.");
    } else {
      _ERROR_("Not connected to AP.");
    }
  }
  */
}

// Receive data over private WiFi
Message ESP32_RC_WIFI::recv(void) {
  
  Message data = {0};
  /*
  if (is_ap) {
    WiFiClient new_client = server.available();
    if (new_client && new_client.available()) {
      new_client.read((uint8_t*)&data, sizeof(Message));
      _DEBUG_("Message received from client.");
    } else {
      _DEBUG_("No data available.");
    }
  } else {
    if (client.connected() && client.available()) {
      client.read((uint8_t*)&data, sizeof(Message));
      _DEBUG_("Message received from AP.");
    } else {
      _DEBUG_("No data available.");
    }
  }
  */
  return data;
  
};

ESP32_RC_WIFI::~ESP32_RC_WIFI() {
  if (send_queue) vQueueDelete(send_queue);
  if (recv_queue) vQueueDelete(recv_queue);
  if (send_timer) {
    xTimerStop(send_timer, 0);
    xTimerDelete(send_timer, 0);
  }
};




void ESP32_RC_WIFI::send_timer_callback(TimerHandle_t xTimer) {
  //ESP32_RC_ESPNOW* this_instance = static_cast<ESP32_RC_ESPNOW*>(pvTimerGetTimerID(xTimer));
  instance->send_queue_msg();
}

void ESP32_RC_WIFI::send_queue_msg() {


}


void ESP32_RC_WIFI::heartbeat_timer_callback(TimerHandle_t xTimer) {
  instance->send_queue_msg();
  digitalWrite(BUILTIN_LED, HIGH);
}


/*
 * ========================================================
 * run - Override
 * ========================================================
 */

void ESP32_RC_WIFI::run(void* data) {
  // Example: Add your task logic here
  while (true) {
    _DEBUG_("Running WiFi task...");
    _DELAY(1000);  // Simulate some work
  }
}