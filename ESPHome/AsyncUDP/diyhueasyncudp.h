#include "esphome.h"
#include <ESPAsyncUDP.h>

class diyhueudp : public Component {
 public:
  int lastUDPmilsec;
  int entertainmentTimeout = 1500;
  AsyncUDP Udp;
  void setup() override {
    if(Udp.listen(2100)) {
        ESP_LOGD("DiyHueUDP", "Listerner Enabled");
        Udp.onPacket([&](AsyncUDPPacket &packet) {entertainment(packet);});
    }
  }
  void loop() override {
    if (entertainment_switch->state) {
      if ((millis() - lastUDPmilsec) >= entertainmentTimeout) {
        entertainment_switch->turn_off();
      }
    }
    //entertainment();
  }
  void entertainment(AsyncUDPPacket &packet)
  {
    ESP_LOGD("DiyHueUDP", "Entertainment packet arrived");
     //turn off white_led when entertainment starts
      auto call = white_led->turn_off();
      call.set_transition_length(0);
      call.perform();
      if (!entertainment_switch->state) {
        entertainment_switch->turn_on();
      }
      lastUDPmilsec = millis(); //reset timeout value
      uint8_t *packetBuffer = packet.data();
      //Udp.read(packetBuffer, packetSize);
      int32 packetSize = packet.length();
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
};