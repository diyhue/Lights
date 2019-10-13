#include "esphome.h"
#include <ESPAsyncUDP.h>
class diyhueudp : public Component {
 public:
  int lastUDPmilsec;
  int entertainmentTimeout = 1500;
  float maxColor = 255;
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
    auto call = white_led->turn_off(); //turn off white_led when entertainment starts
    call.set_transition_length(0);
    call.perform();
    if (!entertainment_switch->state) {
      entertainment_switch->turn_on();
    }
    lastUDPmilsec = millis(); //reset timeout value
    uint8_t *packetBuffer = packet.data();
    int32 packetSize = packet.length();
    call = color_led->turn_on();
    if (((packetBuffer[1])+(packetBuffer[2])+(packetBuffer[3])) == 0) {
      call.set_rgb(0,0,0);
      call.set_brightness(0);
      call.set_transition_length(0);
      call.perform();
    } else {
      call.set_rgb(packetBuffer[1]/maxColor, packetBuffer[2]/maxColor, packetBuffer[3]/maxColor);
      call.set_transition_length(0);
      call.set_brightness(packetBuffer[4]/maxColor);
      call.perform();
    }
  }
};