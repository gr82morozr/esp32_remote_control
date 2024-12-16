/*
  Define the message struct, 
  - Below sample contains 24 channels.
  - It can be changed, but make sure it is less than max length. (default = 250)
  - there is one field should be reserved even with customized struct. The reserved field is for ahndshake and haeartbeat system level messages.
  struct Message {
    bool is_set;       // reserved , don't change
    char sys[40];      // reserved , don't change
    char msg1[40];
    char msg2[40];
    char msg3[40];
    float a1;
    float a2;
    float a3;
    float a4;
    float a5;
    float a6;
    float a7;
    float a8;
    float a9;
    float a10;
    float b1;
    float b2;
    float b3;
    float b4;
    float b5;
    float b6;
    float b7;
    float b8;
    float b9;
    float b10;
  };
*/

struct Message {
  bool is_set;       // reserved , don't change
  char sys[40];      // reserved , don't change
  char msg1[40];
  char msg2[40];
  char msg3[40];
  float a1;
  float a2;
  float a3;
  float a4;
  float a5;
  float a6;
  float a7;
  float a8;
  float a9;
  float a10;
  float b1;
  float b2;
  float b3;
  float b4;
  float b5;
  float b6;
  float b7;
  float b8;
  float b9;
  float b10;
};