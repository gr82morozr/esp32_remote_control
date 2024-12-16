#include <Arduino.h>
#include <ESP32_RC.h>

// Constructor definition
ESP32RemoteControl::ESP32RemoteControl (bool fast_mode, bool debug_mode) 
  : Task("ESP32RemoteControl") {
  
  if (this->is_serial_set == false) {
    Serial.begin(115200);
    this->is_serial_set = true;
  }

  enable_fast(fast_mode);
  enable_debug(debug_mode);
    
}

// Create system message -  like HANDSHAKE, HEARTBEAT ... etc
Message ESP32RemoteControl::create_sys_msg(String data) {
  Message msg;
  strncpy(msg.sys, data.c_str(), sizeof(msg.sys) - 1);
  msg.sys[sizeof(msg.sys) - 1] = '\0';
  msg.is_set = true;
  return msg;
}

// Extract system message 
String ESP32RemoteControl::extract_sys_msg(const Message &msg) {
  return String(msg.sys);
}

// Set value with mutex
void ESP32RemoteControl::set_value(int *in_varible, int value) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  *in_varible = value;
  xSemaphoreGive(mutex);      
};

// Get value with mutex
void ESP32RemoteControl::get_value(int *in_varible, int *out_varible) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  *out_varible = *in_varible;
  xSemaphoreGive(mutex);        
};

// Empty queue
void ESP32RemoteControl::empty_queue(QueueHandle_t queue) {
  Message msg;
  while(de_queue(queue, &msg) == pdPASS){
    // do nothing.
  }
}

// Add Message to queue
bool ESP32RemoteControl::en_queue(QueueHandle_t queue,  Message *pmsg) {
  return (xQueueSend(queue, pmsg, ( TickType_t ) 10) == pdPASS);
};

// Pop Message to queue
bool ESP32RemoteControl::de_queue(QueueHandle_t queue,  Message *pmsg) {
  if (pmsg == nullptr) return false;
  return (xQueueReceive(queue, pmsg, ( TickType_t ) 10) == pdPASS);
};



/*
 =========================================
 *
 * Exception Handler
 * 
 * 
 =========================================
 */

void ESP32RemoteControl::handle_exception(void) {
  if (custom_handler == nullptr) {
    pinMode(BUILTIN_LED, OUTPUT);
    Serial.println(exception_message); 
    digitalWrite(BUILTIN_LED, HIGH);
    while (true) {
      _DELAY_(1000);
    }
  } else {
    custom_handler();
  }
}


void ESP32RemoteControl::enable_debug(bool mode) {
  this->debug_mode=mode;
}

void ESP32RemoteControl::enable_fast(bool mode) {
  this->fast_mode=mode;
}

String ESP32RemoteControl::format_time(unsigned long ms) {
  // Calculate hours, minutes, seconds, and milliseconds
  int hours = ms / 3600000;
  ms %= 3600000;
  int minutes = ms / 60000;
  ms %= 60000;
  int seconds = ms / 1000;
  ms %= 1000;

  // Format the output string
  char temp_buffer[13]; // Temporary buffer for formatting
  snprintf(temp_buffer, sizeof(temp_buffer), "%02lu:%02lu:%02lu_%03lu", hours, minutes, seconds, ms);
  
  // Create a String from the temporary buffer and return it
  return String(temp_buffer);
}

String ESP32RemoteControl::format_time() {
  return this->format_time(millis());
}

void ESP32RemoteControl::raise_error(String func_name, String message) {
  if (this->is_serial_set == false) {
    Serial.begin(115200);
    this->is_serial_set = true;
  }
  this->exception_message = format_time() + " : ERR:" + func_name + " (" + String(xPortGetCoreID()) + ") : " + message;
  this->handle_exception();
};

// print debug logs
void ESP32RemoteControl::debug(String func_name, String message) {
  if (this->debug_mode==true) {
    if (this->is_serial_set == false) {
      Serial.begin(115200);
      this->is_serial_set = true;
    }
    
    if (!message.isEmpty()) {
      Serial.println( this->format_time() + " : " + func_name + " (" + String(xPortGetCoreID()) + ") : " + message);
    } else {
      Serial.println( this->format_time() + " : " );
    }  
  }
};


String ESP32RemoteControl::mac2str(const uint8_t *mac_addr) {
  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  return mac_str;
}

int ESP32RemoteControl::get_queue_depth(QueueHandle_t queue) {
  UBaseType_t uxItemsInQueue = uxQueueMessagesWaiting(queue);
  return (int)uxItemsInQueue; 
}