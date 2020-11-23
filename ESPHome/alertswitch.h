#include "esphome.h"

class alertSwitch : public Component, public Switch {
 public:
  unsigned long lastTime = 0;
  int phase = 0;
  float stored_brightness = 0;
  bool color_led_on = false;
  bool white_led_on = false;

  void setup() override {}
  void loop() override {
    if (phase != 0 && (((millis() - lastTime)/500) >= phase)) {
      if (phase == 3 && !(color_led_on || white_led_on)) {
        auto call_off = white_led->turn_off();
        call_off.perform();
        finalize();
      } else {
        light::LightCall call = (color_led_on) ? color_led->make_call() : call = white_led->make_call();
        if (phase == 1) {
          call.set_brightness(0.01);
        } else if (phase == 2) {
          call.set_brightness(1.0);
        } else {// phase is 3
          call.set_brightness(stored_brightness);
        }
        call.set_transition_length(500);
        call.perform();
        if (phase == 3) {
          finalize();
        } else {
          phase++;
        }
      }
    }
  }
  void finalize() {
    phase = 0;
    color_led_on = false;
    white_led_on = false;
    stored_brightness = 0.0f;
    this->publish_state(false);
  }
  void write_state(bool state) override {
    if (state) {
      if (color_led->remote_values.is_on()) {
        stored_brightness = color_led->remote_values.get_brightness();
        color_led_on = true;
      } else {
        white_led_on = white_led->remote_values.is_on();
        stored_brightness = white_led->remote_values.get_brightness();
      }
      light::LightCall call = (color_led_on) ? color_led->turn_on() : call = white_led->turn_on();
      call.set_brightness(1.0);
      call.set_transition_length(500);
      call.perform();
      phase = 1;
      lastTime = millis();
    }
  }
};
