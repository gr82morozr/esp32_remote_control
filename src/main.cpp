#include <Arduino.h>
#include <ESP32_RC_ESPNOW.h>
#include <ESP32_RC_WIFI.h>
/*
 * ESPNOW bi-directional communication sample
 * Both controllor/executor using the exactly same code as blow.
 *
*/



ESP32_RC_ESPNOW rc_controller(false, true);
//ESP32_RC_WIFI   rc_controller(false, true);
//ESP32_RC_ESPNOW rc_controller(_ROLE_EXECUTOR, false, true);
unsigned long count = 0; 


unsigned long start_time = millis();
unsigned long total_bytes = 0;
Message send_data;
Message recv_data;
int cycle_count = 100;



void setup() {
  pinMode(BUILTIN_LED, OUTPUT);
  rc_controller.init();
  rc_controller.connect();
  Serial.println(sizeof(Message));
}

void loop1() {

}

void loop() {
  count ++;
  String str = String(10000000 + count) + " abcdefghijklmnopers";
  strcpy(send_data.msg1, str.c_str());
  send_data.a1 = millis();
  rc_controller.send(send_data);
  recv_data = rc_controller.recv();
  _DELAY(10);
  if (count % cycle_count == 0) {
    unsigned long time_taken = millis() - start_time;
    Serial.println(String(count) + " : " + String((float)int(time_taken/cycle_count*100)/100) + " : " + String(recv_data.msg1) + " : " + String(recv_data.a1) );
    start_time = millis();
    total_bytes = 0;
  };
}