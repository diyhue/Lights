#include "esphome.h"
#include <WiFiUdp.h>
#include <WiFi.h>
class diyhueudp : public Component {
 public:
  WiFiUDP Udp;
  int lastUDPmilsec;
  int entertainmentTimeout = 1500;
  int initialized = 0;

  //TODO: Can this be removed?
  void setup() override {
    
  }
  void loop() override {
    if (entertainment_switch->state) {
      if ((millis() - lastUDPmilsec) >= entertainmentTimeout) {
        entertainment_switch->turn_off();
      }
    }
    if (!initialized && WiFi.status() == WL_CONNECTED)
    {
    	initialized = 1;
    	Udp.begin(2100);
    }
    if (initialized)
    {
    	entertainment();
    }
  }
  void entertainment() {
    int packetSize = Udp.parsePacket(); //begin parsing udp packet
    if (packetSize) {
      //turn off white_led when entertainment starts
      auto call = white_led->turn_off();
      call.set_transition_length(0);
      call.perform();
      if (!entertainment_switch->state) {
        entertainment_switch->turn_on();
      }
      lastUDPmilsec = millis(); //reset timeout value
      byte packetBuffer[8];
      Udp.read(packetBuffer, packetSize);
      call = color_led->turn_on();
      if (((packetBuffer[1])+(packetBuffer[2])+(packetBuffer[3])) == 0) {
        call.set_rgb(0,0,0);
        call.set_brightness(0);
        call.set_transition_length(0);
        call.perform();
      } else {
        call.set_rgb((float)((packetBuffer[1])/(float)255), (float)((packetBuffer[2])/(float)255), (float)((packetBuffer[3])/(float)255));
        call.set_transition_length(0);
        call.set_brightness((float)((packetBuffer[4])/(float)255));
        call.perform();
      }
    }
  }
};