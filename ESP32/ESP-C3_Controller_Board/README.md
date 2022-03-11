 This is a board specially made for DiyHue and WS2812/WS2815 strips based on the new ESP32-C3 controller (RISC-V CPU). 
 
 ![3D Rencer](https://raw.githubusercontent.com/diyhue/Lights/master/ESP32/ESP-C3_Controller_Board/3D_render.png)
## Features
 - P-Channel mosfet that cut the power to the strip when all leds are soft off in order to save the energy (WS2812 leds draw ~1mA / led while off).
 - Two buttons that allow to control the power (short press) , brightness (long press), factory reset (both buttons pressed) and to enter FLASH MODE at boot.
 - Led indicator for WiFi connectivity (the led turn off once wifi connection is established and will blink when connection is lost).
 - 2.4GHz Chip Antenna.
 
This work is licensed under a Creative Commons Attribution-NonCommercial 3.0 Unported (CC BY-NC 3.0) International License.
