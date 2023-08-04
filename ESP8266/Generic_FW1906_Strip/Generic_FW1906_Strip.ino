// Creates a single length diyHue Strip Light which can be divided into several sections.
// Each section will be exposed as a single "light" entity in DIYhue dashboard.
// Default 3 sections, can be set to less or more. Can be set to 1 section.
// Each section does NOT have to be same number of pixels.  (section 1 can be 10 pixels, section 2 can be 99 pixeles ect)
// Changing number of sections will require deleting/adding the light(s) in DIYhue dashboard. super easy!
//
// After flashing first time, or after erasing flash, connect to DIY hue wifi Access Point, go to webpage 192.168.4.1 to configure wifi.
// enter device IP address into browser to configure LED sections, LED pixel count (save & reboot), and set Static IP (save & reboot)
// then add device to DIYhue dashboard via STATIC IP address.

#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <NeoPixelBus.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>

IPAddress address ( 192,  168,   0,  95); // choose an unique IP Adress
IPAddress gateway ( 192,  168,   0,   1); // Router IP
IPAddress submask(255, 255, 255,   0);

#define LIGHT_VERSION 3.2
#define LIGHT_NAME_MAX_LENGTH 32 // Longer name will get stripped
#define ENTERTAINMENT_TIMEOUT 1500 // millis
#define POWER_MOSFET_PIN 13 // By installing a MOSFET it will cut the power to the leds when lights ore off.

// Warm White (2000K)
const uint8_t WWHITE_CT_R = 255;
const uint8_t  WWHITE_CT_G = 138;
const uint8_t  WWHITE_CT_B = 18;

// Cool White (6500K)
const uint8_t  CWHITE_CT_R = 255;
const uint8_t  CWHITE_CT_G = 249;
const uint8_t  CWHITE_CT_B = 253;

float covertedColor[5];

struct state {
  uint8_t colors[5], bri = 100, sat = 254, colorMode = 2;
  bool lightState;
  int ct = 200, hue;
  float stepLevel[5], currentColors[5], x, y;
};

state lights[10];
bool inTransition, entertainmentRun, mosftetState, useDhcp = true;
byte mac[6], packetBuffer[46];
unsigned long lastEPMillis;

//settings
char lightName[LIGHT_NAME_MAX_LENGTH] = "Hue fw1906 strip";
uint8_t scene, startup, onPin = 4, offPin = 5; 

int RgbAdditionalPct = 0;
int CtBlendRgbPctW = 0;
int CtBlendRgbPctC = 0;
bool hwSwitch = false;
uint8_t rgb_multiplier[] = {100, 100, 100}; // light multiplier in percentage /R, G, B/
bool rgbctswitch = false;
bool ctblendswitch = false;

uint8_t lightsCount = 3;
uint16_t dividedLightsArray[30];

uint16_t pixelCount = 60;
uint8_t transitionLeds = 6; // pixelCount must be divisible by this value

ESP8266WebServer server(80);
WiFiUDP Udp;
ESP8266HTTPUpdateServer httpUpdateServer;

RgbwwColor red = RgbwwColor(255, 0, 0, 0, 0);
RgbwwColor green = RgbwwColor(0, 255, 0, 0, 0);
RgbwwColor white = RgbwwColor(0,0,0,127,127);
RgbwwColor black = RgbwwColor(0);

//NeoPixelBus<NeoGrbcwxFeature, Neo800KbpsMethod>* strip = NULL;
NeoPixelBus<NeoGrbcwxFeature, NeoWs2812xMethod>* strip = NULL;

void convertHue(uint8_t light) // convert hue / sat values from HUE API to RGB
{
  double      hh, p, q, t, ff, s, v;
  long        i;

  s = lights[light].sat / 255.0;
  v = lights[light].bri / 255.0;

  if (s <= 0.0) {      // < is bogus, just shuts up warnings
    lights[light].colors[0] = v;
    lights[light].colors[1] = v;
    lights[light].colors[2] = v;
    return;
  }
  hh = lights[light].hue;
  if (hh >= 65535.0) hh = 0.0;
  hh /= 11850, 0;
  i = (long)hh;
  ff = hh - i;
  p = v * (1.0 - s);
  q = v * (1.0 - (s * ff));
  t = v * (1.0 - (s * (1.0 - ff)));

  switch (i) {
    case 0:
      lights[light].colors[0] = v * 255.0;
      lights[light].colors[1] = t * 255.0;
      lights[light].colors[2] = p * 255.0;
      lights[light].colors[3] = 0;
      lights[light].colors[4] = 0;
      break;
    case 1:
      lights[light].colors[0] = q * 255.0;
      lights[light].colors[1] = v * 255.0;
      lights[light].colors[2] = p * 255.0;
      lights[light].colors[3] = 0;
      lights[light].colors[4] = 0;
      break;
    case 2:
      lights[light].colors[0] = p * 255.0;
      lights[light].colors[1] = v * 255.0;
      lights[light].colors[2] = t * 255.0;
      lights[light].colors[3] = 0;
      lights[light].colors[4] = 0;
      break;

    case 3:
      lights[light].colors[0] = p * 255.0;
      lights[light].colors[1] = q * 255.0;
      lights[light].colors[2] = v * 255.0;
      lights[light].colors[3] = 0;
      lights[light].colors[4] = 0;
      break;
    case 4:
      lights[light].colors[0] = t * 255.0;
      lights[light].colors[1] = p * 255.0;
      lights[light].colors[2] = v * 255.0;
      lights[light].colors[3] = 0;
      lights[light].colors[4] = 0;
      break;
    case 5:
    default:
      lights[light].colors[0] = v * 255.0;
      lights[light].colors[1] = p * 255.0;
      lights[light].colors[2] = q * 255.0;
      lights[light].colors[3] = 0;
      lights[light].colors[4] = 0;
      break;
  }
  convertRgbToRgbwc(light);
}

void convertXy(uint8_t light) // convert CIE xy values from HUE API to RGB
{
  int optimal_bri = lights[light].bri;
  if (optimal_bri < 5) {
    optimal_bri = 5;
  }
  float Y = lights[light].y;
  float X = lights[light].x;
  float Z = 1.0f - lights[light].x - lights[light].y;

  // sRGB D65 conversion
  float r =  X * 3.2406f - Y * 1.5372f - Z * 0.4986f;
  float g = -X * 0.9689f + Y * 1.8758f + Z * 0.0415f;
  float b =  X * 0.0557f - Y * 0.2040f + Z * 1.0570f;


  // Apply gamma correction
  r = r <= 0.0031308f ? 12.92f * r : (1.0f + 0.055f) * pow(r, (1.0f / 2.4f)) - 0.055f;
  g = g <= 0.0031308f ? 12.92f * g : (1.0f + 0.055f) * pow(g, (1.0f / 2.4f)) - 0.055f;
  b = b <= 0.0031308f ? 12.92f * b : (1.0f + 0.055f) * pow(b, (1.0f / 2.4f)) - 0.055f;

  // Apply multiplier for white correction
  r = r * rgb_multiplier[0] / 100;
  g = g * rgb_multiplier[1] / 100;
  b = b * rgb_multiplier[2] / 100;

  if (r > b && r > g) {
    // red is biggest
    if (r > 1.0f) {
      g = g / r;
      b = b / r;
      r = 1.0f;
    }
  }
  else if (g > b && g > r) {
    // green is biggest
    if (g > 1.0f) {
      r = r / g;
      b = b / g;
      g = 1.0f;
    }
  }
  else if (b > r && b > g) {
    // blue is biggest
    if (b > 1.0f) {
      r = r / b;
      g = g / b;
      b = 1.0f;
    }
    
  }

  r = r < 0 ? 0 : r;
  g = g < 0 ? 0 : g;
  b = b < 0 ? 0 : b;

  lights[light].colors[0] = (int) (r * optimal_bri); lights[light].colors[1] = (int) (g * optimal_bri); lights[light].colors[2] = (int) (b * optimal_bri); lights[light].colors[3] = 0; lights[light].colors[4] = 0;
  convertRgbToRgbwc(light);
}

void convertCt(uint8_t light) // convert ct (color temperature) value from HUE API to RGB
{
  int hectemp = 10000 / lights[light].ct;
  
  int optimal_bri = int(3 + lights[light].bri / 1.01);
  int optimal_bri_rgb = int(optimal_bri * RgbAdditionalPct / 100);
  
  int r, g, b;
  if (hectemp <= 66) {
    r = 255;
    g = 99.4708025861 * log(hectemp) - 161.1195681661;
    b = hectemp <= 19 ? 0 : (138.5177312231 * log(hectemp - 10) - 305.0447927307);
  } else {
    r = 329.698727446 * pow(hectemp - 60, -0.1332047592);
    g = 288.1221695283 * pow(hectemp - 60, -0.0755148492);
    b = 255;
  }

  r = r > 255 ? 255 : r;
  g = g > 255 ? 255 : g;
  b = b > 255 ? 255 : b;

  // Apply multiplier for white correction
  r = r * rgb_multiplier[0] / 100;
  g = g * rgb_multiplier[1] / 100;
  b = b * rgb_multiplier[2] / 100;

  lights[light].colors[0] = r * (optimal_bri_rgb / 255.0f); lights[light].colors[1] = g * (optimal_bri_rgb / 255.0f); lights[light].colors[2] = b * (optimal_bri_rgb / 255.0f);

  uint8 percent_warm = ((lights[light].ct - 150) * 100) / 350;

  lights[light].colors[3] = (optimal_bri * percent_warm) / 100;
  lights[light].colors[4] =  (optimal_bri * (100 - percent_warm)) / 100;
}

void convertRgbToRgbwc(uint8_t light) {
  uint8_t r = lights[light].colors[0];
  uint8_t g = lights[light].colors[1];
  uint8_t b = lights[light].colors[2];

  convertColorRgbToRgbwc(r, g, b);
  
  for (uint8_t k = 0; k < 5; k++) { //loop with every RGB channel
    lights[light].colors[k] = covertedColor[k];
  }
}

void convertColorRgbToRgbwc(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t r0 = r;
  uint8_t g0 = g;
  uint8_t b0 = b;

  // get highest  replacement by warm white
  float wwhiteValueForRed = r0 * 255.0 / WWHITE_CT_R;
  float wwhiteValueForGreen = g0 * 255.0 / WWHITE_CT_G;
  float wwhiteValueForBlue = b0 * 255.0 / WWHITE_CT_B;
  float minWWhiteValue = min(wwhiteValueForRed,
                             min(wwhiteValueForGreen,
                                 wwhiteValueForBlue)) * CtBlendRgbPctW / 100;

  // get highest  replacement by cold white
  float cwhiteValueForRed = r0 * 255.0 / CWHITE_CT_R;
  float cwhiteValueForGreen = g0 * 255.0 / CWHITE_CT_G;
  float cwhiteValueForBlue = b0 * 255.0 / CWHITE_CT_B;
  float minCWhiteValue = min(cwhiteValueForRed,
                             min(cwhiteValueForGreen,
                                 cwhiteValueForBlue))  * CtBlendRgbPctC / 100;

  // balance warm and cold white replacement
  float Wtot = (minCWhiteValue + minWWhiteValue);
  float CWpct = (Wtot != 0 ? minCWhiteValue / Wtot : 0);
  float WWpct = (Wtot != 0 ? minWWhiteValue / Wtot : 0);
  minCWhiteValue = minCWhiteValue * CWpct;
  minWWhiteValue = minWWhiteValue * WWpct;
  uint8_t CW = (minCWhiteValue <= 255 ? (uint8_t) minCWhiteValue : 255);
  uint8_t WW = (minWWhiteValue <= 255 ? (uint8_t) minWWhiteValue : 255);
  
  //subtract warm white on rgb channels
  uint8_t r1 = (uint8_t)(r0 - minWWhiteValue * WWHITE_CT_R / 255);
  uint8_t g1 = (uint8_t)(g0 - minWWhiteValue * WWHITE_CT_G / 255);
  uint8_t b1 = (uint8_t)(b0 - minWWhiteValue * WWHITE_CT_B / 255);
  //subtract cold white on rgb channels
  uint8_t r2 = (uint8_t)(r1- minCWhiteValue * WWHITE_CT_R / 255);
  uint8_t g2 = (uint8_t)(g1 - minCWhiteValue * WWHITE_CT_G / 255);
  uint8_t b2 = (uint8_t)(b1 - minCWhiteValue * WWHITE_CT_B / 255);

  //result
  covertedColor[0] = r2;
  covertedColor[1] = g2;
  covertedColor[2] = b2;
  covertedColor[3] = WW;
  covertedColor[4] = CW;
}

void handleNotFound() { // default webserver response for unknow requests
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void infoLight(RgbwwColor color) { // boot animation for leds count and wifi test
  // Flash the strip in the selected color. White = booted, green = WLAN connected, red = WLAN could not connect
  for (int i = 0; i < pixelCount; i++)
  {
    strip->SetPixelColor(i, color);
    strip->Show();
    delay(10);
    strip->SetPixelColor(i, black);
    strip->Show();
  }
}

void apply_scene(uint8_t new_scene) { // these are internal scenes store in light firmware that can be applied on boot and manually from light web interface
  for (uint8_t light = 0; light < lightsCount; light++) {
    if ( new_scene == 1) {
      lights[light].bri = 254; lights[light].ct = 346; lights[light].colorMode = 2; convertCt(light);
    } else if ( new_scene == 2) {
      lights[light].bri = 254; lights[light].ct = 233; lights[light].colorMode = 2; convertCt(light);
    }  else if ( new_scene == 3) {
      lights[light].bri = 254; lights[light].ct = 156; lights[light].colorMode = 2; convertCt(light);
    }  else if ( new_scene == 4) {
      lights[light].bri = 77; lights[light].ct = 367; lights[light].colorMode = 2; convertCt(light);
    }  else if ( new_scene == 5) {
      lights[light].bri = 254; lights[light].ct = 447; lights[light].colorMode = 2; convertCt(light);
    }  else if ( new_scene == 6) {
      lights[light].bri = 1; lights[light].x = 0.561; lights[light].y = 0.4042; lights[light].colorMode = 1; convertXy(light);
    }  else if ( new_scene == 7) {
      lights[light].bri = 203; lights[light].x = 0.380328; lights[light].y = 0.39986; lights[light].colorMode = 1; convertXy(light);
    }  else if ( new_scene == 8) {
      lights[light].bri = 112; lights[light].x = 0.359168; lights[light].y = 0.28807; lights[light].colorMode = 1; convertXy(light);
    }  else if ( new_scene == 9) {
      lights[light].bri = 142; lights[light].x = 0.267102; lights[light].y = 0.23755; lights[light].colorMode = 1; convertXy(light);
    }  else if ( new_scene == 10) {
      lights[light].bri = 216; lights[light].x = 0.393209; lights[light].y = 0.29961; lights[light].colorMode = 1; convertXy(light);
    } else {
      lights[light].bri = 144; lights[light].ct = 447; lights[light].colorMode = 2; convertCt(light);
    }
  }
}

void processLightdata(uint8_t light, float transitiontime) { // calculate the step level of every RGB channel for a smooth transition in requested transition time
  transitiontime *= 17 - (pixelCount / 40); //every extra led add a small delay that need to be counted for transition time match 
  if (lights[light].colorMode == 1 && lights[light].lightState == true) {
    convertXy(light);
  } else if (lights[light].colorMode == 2 && lights[light].lightState == true) {
    convertCt(light);
  } else if (lights[light].colorMode == 3 && lights[light].lightState == true) {
    convertHue(light);
  }
  for (uint8_t i = 0; i < 5; i++) {
    if (lights[light].lightState) {
      lights[light].stepLevel[i] = ((float)lights[light].colors[i] - lights[light].currentColors[i]) / transitiontime;
    } else {
      lights[light].stepLevel[i] = lights[light].currentColors[i] / transitiontime;
    }
  }
}

RgbwwColor blending(float left[5], float right[5], uint8_t pixel) { // return RgbwwColor based on neighbour leds
  uint8_t result[5];
  for (uint8_t i = 0; i < 5; i++) {
    float percent = (float) pixel / (float) (transitionLeds + 1);
    result[i] = (left[i] * (1.0f - percent) + right[i] * percent);
  }
  return RgbwwColor((uint8_t)result[0], (uint8_t)result[1], (uint8_t)result[2], (uint8_t)result[3], (uint8_t)result[4]);
}

RgbwwColor convFloat(float color[5]) { // return RgbwwColor from float
  return RgbwwColor((uint8_t)color[0], (uint8_t)color[1], (uint8_t)color[2], (uint8_t)color[3], (uint8_t)color[4]);
}

void cutPower() {
  bool any_on = false;
  
  for (int light = 0; light < lightsCount; light++) {
    if (lights[light].lightState) {
      any_on = true;
    }
  }
  if (!any_on && !inTransition && mosftetState) {
    digitalWrite(POWER_MOSFET_PIN, LOW);
    mosftetState = false;
  } else if (any_on && !mosftetState){;
    digitalWrite(POWER_MOSFET_PIN, HIGH);
    mosftetState = true;
  }

}





void lightEngine() {  // core function executed in loop()
  for (int light = 0; light < lightsCount; light++) { // loop with every virtual light
    if (((lights[light].lightState) && (lights[light].colors[0] != lights[light].currentColors[0] || lights[light].colors[1] != lights[light].currentColors[1] || lights[light].colors[2] != lights[light].currentColors[2] || lights[light].colors[3] != lights[light].currentColors[3] || lights[light].colors[4] != lights[light].currentColors[4])) || (!(lights[light].lightState) && (lights[light].currentColors[0] != 0 || lights[light].currentColors[1] != 0 || lights[light].currentColors[2] != 0 || lights[light].currentColors[3] != 0 || lights[light].currentColors[4] != 0))) { // if not all RGB channels of the light are at desired level
      inTransition = true;
      for (uint8_t k = 0; k < 5; k++) { // loop with every RGB channel of the light
        if (lights[light].lightState) {
          if (lights[light].colors[k] != lights[light].currentColors[k]) lights[light].currentColors[k] += lights[light].stepLevel[k]; // move RGB channel on step closer to desired level
          if ((lights[light].stepLevel[k] > 0.0 && lights[light].currentColors[k] > lights[light].colors[k]) || (lights[light].stepLevel[k] < 0.0 && lights[light].currentColors[k] < lights[light].colors[k])) lights[light].currentColors[k] = lights[light].colors[k]; // if the current level go below desired level apply directly the desired level.
        }
        else {
          if (lights[light].currentColors[k] != 0) lights[light].currentColors[k] -= lights[light].stepLevel[k]; // remove one step level
          if (lights[light].currentColors[k] < 0) lights[light].currentColors[k] = 0; // save condition, if level go below zero set it to zero
        }
      }
      if (lightsCount > 1) { // if are more then 1 virtual light we need to apply transition leds (set in the web interface)
        long pixelSum = 0;
        for (int value = 0; value < light; value++) {pixelSum += dividedLightsArray[value];}
        if (light == 0) {pixelSum = 0;}
        for (int pixel = 0; pixel < dividedLightsArray[light]; pixel++){ // loop with all leds of the light 
          if (pixel < transitionLeds / 2) { // beginning transition leds
            if (light == 0) { //no transition in front of first light
              strip->SetPixelColor(pixel + pixelSum, convFloat(lights[light].currentColors));
            }
            else {
              strip->SetPixelColor(pixel + pixelSum, blending( lights[light - 1].currentColors, lights[light].currentColors, pixel + 1 + transitionLeds / 2));
              strip->SetPixelColor(pixel + pixelSum - transitionLeds / 2, blending( lights[light - 1].currentColors, lights[light].currentColors, pixel + 1)); //set transition on previous light
            }
          }
          else if (pixel > dividedLightsArray[light] - transitionLeds / 2 - 1) {  // end of transition leds
            if (light == lightsCount - 1) { //no transition on end of last light
              strip->SetPixelColor(pixel + pixelSum , convFloat(lights[light].currentColors));
            }
            else {
              strip->SetPixelColor(pixel + pixelSum, blending( lights[light].currentColors, lights[light + 1].currentColors, pixel + transitionLeds / 2 - dividedLightsArray[light] +1 ));
              strip->SetPixelColor(pixel + pixelSum + transitionLeds / 2, blending( lights[light].currentColors, lights[light + 1].currentColors, pixel + transitionLeds - dividedLightsArray[light] +1 )); //set transition on next light
            }
          }
          else  { // outside transition leds (apply raw color)
            strip->SetPixelColor(pixel + pixelSum, convFloat(lights[light].currentColors));
          }
        }
      } else { // strip has only one virtual light so apply raw color to entire strip
        strip->ClearTo(convFloat(lights[light].currentColors), 0, pixelCount - 1);
      }
      strip->Show(); //show what was calculated previously 
    }
  }
  cutPower(); // if all lights are off GPIO12 can cut the power to the strip using a powerful P-Channel MOSFET
  if (inTransition) { // wait 6ms for a nice transition effect
    delay(6);
    inTransition = false; // set inTransition bash to false (will be set bach to true on next level execution if desired state is not reached)
  } else if (hwSwitch == true) { // if you want to use some GPIO's for on/off and brightness controll
    if (digitalRead(onPin) == HIGH) { // on button pressed
      int i = 0;
      while (digitalRead(onPin) == HIGH && i < 30) { // count how log is the button pressed
        delay(20);
        i++;
      }
      for (int light = 0; light < lightsCount; light++) {
        if (i < 30) { // there was a short press
          lights[light].lightState = true;
        }
        else { // there was a long press
          lights[light].bri += 56;
          if (lights[light].bri > 255) {
            // don't increase the brightness more then maximum value
            lights[light].bri = 255;
          }
        }
      }
    } else if (digitalRead(offPin) == HIGH) { // off button pressed
      int i = 0;
      while (digitalRead(offPin) == HIGH && i < 30) {
        delay(20);
        i++;
      }
      for (int light = 0; light < lightsCount; light++) {
        if (i < 30) {
          // there was a short press
          lights[light].lightState = false;
        }
        else {
          // there was a long press
          lights[light].bri -= 56;
          if (lights[light].bri < 1) {
            // don't decrease the brightness less than minimum value.
            lights[light].bri = 1;
          }
        }
      }
    }
  }
}

void saveState() { // save the lights state on SPIFFS partition in JSON format
  DynamicJsonDocument json(1024);
  for (uint8_t i = 0; i < lightsCount; i++) {
    JsonObject light = json.createNestedObject((String)i);
    light["on"] = lights[i].lightState;
    light["bri"] = lights[i].bri;
    if (lights[i].colorMode == 1) {
      light["x"] = lights[i].x;
      light["y"] = lights[i].y;
    } else if (lights[i].colorMode == 2) {
      light["ct"] = lights[i].ct;
    } else if (lights[i].colorMode == 3) {
      light["hue"] = lights[i].hue;
      light["sat"] = lights[i].sat;
    }
  }
  File stateFile = SPIFFS.open("/state.json", "w");
  serializeJson(json, stateFile);

}

void restoreState() { // restore the lights state from SPIFFS partition
  File stateFile = SPIFFS.open("/state.json", "r");
  if (!stateFile) {
    saveState();
    return;
  }

  DynamicJsonDocument json(1024);
  DeserializationError error = deserializeJson(json, stateFile.readString());
  if (error) {
    //Serial.println("Failed to parse config file");
    return;
  }
  for (JsonPair state : json.as<JsonObject>()) {
    const char* key = state.key().c_str();
    int lightId = atoi(key);
    JsonObject values = state.value();
    lights[lightId].lightState = values["on"];
    lights[lightId].bri = (uint8_t)values["bri"];
    if (values.containsKey("x")) {
      lights[lightId].x = values["x"];
      lights[lightId].y = values["y"];
      lights[lightId].colorMode = 1;
    } else if (values.containsKey("ct")) {
      lights[lightId].ct = values["ct"];
      lights[lightId].colorMode = 2;
    } else {
      if (values.containsKey("hue")) {
        lights[lightId].hue = values["hue"];
        lights[lightId].colorMode = 3;
      }
      if (values.containsKey("sat")) {
        lights[lightId].sat = (uint8_t) values["sat"];
        lights[lightId].colorMode = 3;
      }
    }
  }
}

bool saveConfig() { // save config in SPIFFS partition in JSON file
  DynamicJsonDocument json(1024);
  json["name"] = lightName;
  json["startup"] = startup;
  json["scene"] = scene;
  json["on"] = onPin;
  json["off"] = offPin;
  json["hw"] = hwSwitch;
  json["dhcp"] = useDhcp;
  json["lightsCount"] = lightsCount;
  json["rgbct"] = rgbctswitch;
  json["ctblend"] = ctblendswitch;
  json["rgbctpct"] = RgbAdditionalPct;
  json["ctblendpctc"] = CtBlendRgbPctC;
  json["ctblendpctw"] = CtBlendRgbPctW;
  for (uint16_t i = 0; i < lightsCount; i++) {
    json["dividedLight_" + String(i)] = dividedLightsArray[i];
  }
  json["pixelCount"] = pixelCount;
  json["transLeds"] = transitionLeds;
  json["rpct"] = rgb_multiplier[0];
  json["gpct"] = rgb_multiplier[1];
  json["bpct"] = rgb_multiplier[2];
  JsonArray addr = json.createNestedArray("addr");
  addr.add(address[0]);
  addr.add(address[1]);
  addr.add(address[2]);
  addr.add(address[3]);
  JsonArray gw = json.createNestedArray("gw");
  gw.add(gateway[0]);
  gw.add(gateway[1]);
  gw.add(gateway[2]);
  gw.add(gateway[3]);
  JsonArray mask = json.createNestedArray("mask");
  mask.add(submask[0]);
  mask.add(submask[1]);
  mask.add(submask[2]);
  mask.add(submask[3]);
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    //Serial.println("Failed to open config file for writing");
    return false;
  }

  serializeJson(json, configFile);
  return true;
}

bool loadConfig() { // load the configuration from SPIFFS partition
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    //Serial.println("Create new file with default values");
    return saveConfig();
  }

  size_t size = configFile.size();
  if (size > 1024) {
    //Serial.println("Config file size is too large");
    return false;
  }

  if (configFile.size() > 1024) {
    //Serial.println("Config file size is too large");
    return false;
  }

  DynamicJsonDocument json(1024);
  DeserializationError error = deserializeJson(json, configFile.readString());
  if (error) {
    //Serial.println("Failed to parse config file");
    return false;
  }

  strcpy(lightName, json["name"]);
  startup = (uint8_t) json["startup"];
  scene  = (uint8_t) json["scene"];
  onPin = (uint8_t) json["on"];
  offPin = (uint8_t) json["off"];
  hwSwitch = json["hw"];
  lightsCount = (uint16_t) json["lightsCount"];
  rgbctswitch = json["rgbct"];
  RgbAdditionalPct = json["rgbctpct"];
  ctblendswitch = json["ctblend"];
  CtBlendRgbPctC = json["ctblendpctc"];
  CtBlendRgbPctW = json["ctblendpctw"];
  for (uint16_t i = 0; i < lightsCount; i++) {
    dividedLightsArray[i] = (uint16_t) json["dividedLight_" + String(i)];
  }
  pixelCount = (uint16_t) json["pixelCount"];
  transitionLeds = (uint8_t) json["transLeds"];
  if (json.containsKey("rpct")) {
    rgb_multiplier[0] = (uint8_t) json["rpct"];
    rgb_multiplier[1] = (uint8_t) json["gpct"];
    rgb_multiplier[2] = (uint8_t) json["bpct"];
  }
  useDhcp = json["dhcp"];
  address = {json["addr"][0], json["addr"][1], json["addr"][2], json["addr"][3]};
  submask = {json["mask"][0], json["mask"][1], json["mask"][2], json["mask"][3]};
  gateway = {json["gw"][0], json["gw"][1], json["gw"][2], json["gw"][3]};
  return true;
}

void ChangeNeoPixels(uint16_t newCount) {// this set the number of leds of the strip based on web configuration
  if (strip != NULL) {
    delete strip; // delete the previous dynamically created strip
  }
  //strip = new NeoPixelBus<NeoGrbcwxFeature, Neo800KbpsMethod>(newCount); // and recreate with new count
  strip = new NeoPixelBus<NeoGrbcwxFeature, NeoWs2812xMethod>(newCount); // and recreate with new count
  strip->Begin();
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  delay(1000);

  pinMode(POWER_MOSFET_PIN, OUTPUT);
  digitalWrite(POWER_MOSFET_PIN, HIGH); mosftetState = true; // reuired if HIGH logic power the strip, otherwise must be commented.

  Serial.println("mounting FS...");

  if (!SPIFFS.begin()) {
    //Serial.println("Failed to mount file system");
    return;
  }

  if (!loadConfig()) {
    //Serial.println("Failed to load config");
  } else {
    ////Serial.println("Config loaded");
  }

  dividedLightsArray[lightsCount];


  ChangeNeoPixels(pixelCount);

  if (startup == 1) {
    for (uint8_t i = 0; i < lightsCount; i++) {
      lights[i].lightState = true;
    }
  }
  if (startup == 0) {
    restoreState();
  } else {
    apply_scene(scene);
  }
  for (uint8_t i = 0; i < lightsCount; i++) {
    processLightdata(i, 4);
  }
  if (lights[0].lightState) {
    for (uint8_t i = 0; i < 200; i++) {
      lightEngine();
    }
  }
  WiFi.mode(WIFI_STA);
  WiFiManager wifiManager; // wifimanager will start the configuration SSID if wifi connection is not succesfully 

  if (!useDhcp) {
    wifiManager.setSTAStaticIPConfig(address, gateway, submask);
  }

  if (!wifiManager.autoConnect(lightName)) { // light was not connected to wifi and not configured so reset.
    delay(3000);
    ESP.reset();
    delay(5000);
  }

  if (useDhcp) {
    address = WiFi.localIP();
    gateway = WiFi.gatewayIP();
    submask = WiFi.subnetMask();
  }


  if (! lights[0].lightState) { // test if light zero (must be at last one light) is not set to ON
    infoLight(white); // play white anymation
    while (WiFi.status() != WL_CONNECTED) { // connection to wifi still not ready
      infoLight(red); // play red animation
      delay(500);
    }
    // Show that we are connected
    infoLight(green); // connected, play green animation

  }

  String hostname = lightName;
  hostname.replace(" ", "-");

  WiFi.hostname("hue-" + hostname);
  WiFi.macAddress(mac);

  httpUpdateServer.setup(&server); // start http server

  Udp.begin(2100); // start entertainment UDP server

  if (hwSwitch == true) { // set buttons pins mode in case are used
    pinMode(onPin, INPUT);
    pinMode(offPin, INPUT);
  }
  
  server.on("/state", HTTP_PUT, []() { // HTTP PUT request used to set a new light state
    bool stateSave = false;
    DynamicJsonDocument root(1024);
    DeserializationError error = deserializeJson(root, server.arg("plain"));

    if (error) {
      server.send(404, "text/plain", "FAIL. " + server.arg("plain"));
    } else {
      for (JsonPair state : root.as<JsonObject>()) {
        const char* key = state.key().c_str();
        int light = atoi(key) - 1;
        JsonObject values = state.value();
        int transitiontime = 4;

        if (values.containsKey("xy")) {
          lights[light].x = values["xy"][0];
          lights[light].y = values["xy"][1];
          lights[light].colorMode = 1;
        } else if (values.containsKey("ct")) {
          lights[light].ct = values["ct"];
          lights[light].colorMode = 2;
        } else {
          if (values.containsKey("hue")) {
            lights[light].hue = values["hue"];
            lights[light].colorMode = 3;
          }
          if (values.containsKey("sat")) {
            lights[light].sat = values["sat"];
            lights[light].colorMode = 3;
          }
        }

        if (values.containsKey("on")) {
          if (values["on"]) {
            lights[light].lightState = true;
          } else {
            lights[light].lightState = false;
          }
          if (startup == 0) {
            stateSave = true;
          }
        }

        if (values.containsKey("bri")) {
          lights[light].bri = values["bri"];
        }

        if (values.containsKey("bri_inc")) {
          lights[light].bri += (int) values["bri_inc"];
          if (lights[light].bri > 255) lights[light].bri = 255;
          else if (lights[light].bri < 1) lights[light].bri = 1;
        }

        if (values.containsKey("transitiontime")) {
          transitiontime = values["transitiontime"];
        }

        if (values.containsKey("alert") && values["alert"] == "select") {
          if (lights[light].lightState) {
            lights[light].currentColors[0] = 0; lights[light].currentColors[1] = 0; lights[light].currentColors[2] = 0; lights[light].currentColors[3] = 0; lights[light].currentColors[4] = 0;
          } else {
            lights[light].currentColors[1] = 126; lights[light].currentColors[2] = 126;
          }
        }
        processLightdata(light, transitiontime);
      }
      String output;
      serializeJson(root, output);
      server.send(200, "text/plain", output);
      if (stateSave) {
        saveState();
      }
    }
  });

  server.on("/state", HTTP_GET, []() { // HTTP GET request used to fetch current light state
    uint8_t light = server.arg("light").toInt() - 1;
    DynamicJsonDocument root(1024);
    root["on"] = lights[light].lightState;
    root["bri"] = lights[light].bri;
    JsonArray xy = root.createNestedArray("xy");
    xy.add(lights[light].x);
    xy.add(lights[light].y);
    root["ct"] = lights[light].ct;
    root["hue"] = lights[light].hue;
    root["sat"] = lights[light].sat;
    if (lights[light].colorMode == 1)
      root["colormode"] = "xy";
    else if (lights[light].colorMode == 2)
      root["colormode"] = "ct";
    else if (lights[light].colorMode == 3)
      root["colormode"] = "hs";
    String output;
    serializeJson(root, output);
    server.send(200, "text/plain", output);
  });

  server.on("/detect", []() { // HTTP GET request used to discover the light type
    char macString[32] = {0};
    sprintf(macString, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    DynamicJsonDocument root(1024);
    root["name"] = lightName;
    root["lights"] = lightsCount;
    root["protocol"] = "native_multi";
    root["modelid"] = "LST002";
    root["type"] = "fw1906_strip";
    root["mac"] = String(macString);
    root["version"] = LIGHT_VERSION;
    String output;
    serializeJson(root, output);
    server.send(200, "text/plain", output);
  });

  server.on("/config", []() { // used by light web interface to get current configuration 
    DynamicJsonDocument root(1024);
    root["name"] = lightName;
    root["scene"] = scene;
    root["startup"] = startup;
    root["hw"] = hwSwitch;
    root["on"] = onPin;
    root["off"] = offPin;
    root["rgbct"] = rgbctswitch;
    root["ctblend"] = ctblendswitch;
    root["hwswitch"] = (int)hwSwitch;
    root["rgbctswitch"] = (int)rgbctswitch;
    root["ctblendswitch"] = (int)ctblendswitch;
    root["lightscount"] = lightsCount;
    root["rgbctpct"] = RgbAdditionalPct;
    root["ctblendpctc"] = CtBlendRgbPctC;
    root["ctblendpctw"] = CtBlendRgbPctW;
    for (uint8_t i = 0; i < lightsCount; i++) {
      root["dividedLight_" + String(i)] = (int)dividedLightsArray[i];
    }
    root["pixelcount"] = pixelCount;
    root["transitionleds"] = transitionLeds;
    root["rpct"] = rgb_multiplier[0];
    root["gpct"] = rgb_multiplier[1];
    root["bpct"] = rgb_multiplier[2];
    root["disdhcp"] = (int)!useDhcp;
    root["addr"] = (String)address[0] + "." + (String)address[1] + "." + (String)address[2] + "." + (String)address[3];
    root["gw"] = (String)gateway[0] + "." + (String)gateway[1] + "." + (String)gateway[2] + "." + (String)gateway[3];
    root["sm"] = (String)submask[0] + "." + (String)submask[1] + "." + (String)submask[2] + "." + (String)submask[3];
    String output;
    serializeJson(root, output);
    server.send(200, "text/plain", output);
  });

  server.on("/", []() { // light http web interface
    if (server.arg("section").toInt() == 1) {
      server.arg("name").toCharArray(lightName, LIGHT_NAME_MAX_LENGTH);
      startup = server.arg("startup").toInt();
      scene = server.arg("scene").toInt();
      lightsCount = server.arg("lightscount").toInt();
      pixelCount = server.arg("pixelcount").toInt();
      transitionLeds = server.arg("transitionleds").toInt();
      rgb_multiplier[0] = server.arg("rpct").toInt();
      rgb_multiplier[1] = server.arg("gpct").toInt();
      rgb_multiplier[2] = server.arg("bpct").toInt();
      for (uint16_t i = 0; i < lightsCount; i++) {
        dividedLightsArray[i] = server.arg("dividedLight_" + String(i)).toInt();
      }
      hwSwitch = server.hasArg("hwswitch") ? server.arg("hwswitch").toInt() : 0;
      if (server.hasArg("hwswitch")) {
        onPin = server.arg("on").toInt();
        offPin = server.arg("off").toInt();
      }
      rgbctswitch = server.hasArg("rgbctswitch") ? server.arg("rgbctswitch").toInt() : 0;
      if (server.hasArg("rgbctswitch")) {
        RgbAdditionalPct = server.arg("rgbctpct").toInt();
      } else {
        RgbAdditionalPct = 0;
      }
      if (RgbAdditionalPct < 0) {RgbAdditionalPct = 0;} else if (RgbAdditionalPct > 100) {RgbAdditionalPct = 100;}
      ctblendswitch = server.hasArg("ctblendswitch") ? server.arg("ctblendswitch").toInt() : 0;
      if (server.hasArg("ctblendswitch")) {
        CtBlendRgbPctC = server.arg("ctblendpctc").toInt();
        CtBlendRgbPctW = server.arg("ctblendpctw").toInt();
      } else {
        CtBlendRgbPctC = 0;
        CtBlendRgbPctW = 0;
      }
      if (CtBlendRgbPctC < 0) {CtBlendRgbPctC = 0;} else if (CtBlendRgbPctC > 100) {CtBlendRgbPctC = 100;}
      if (CtBlendRgbPctW < 0) {CtBlendRgbPctW = 0;} else if (CtBlendRgbPctW > 100) {CtBlendRgbPctW = 100;}
      saveConfig();
    } else if (server.arg("section").toInt() == 2) {
      useDhcp = (!server.hasArg("disdhcp")) ? 1 : server.arg("disdhcp").toInt();
      if (server.hasArg("disdhcp")) {
        address.fromString(server.arg("addr"));
        gateway.fromString(server.arg("gw"));
        submask.fromString(server.arg("sm"));
      }
      saveConfig();
    }
	  
    String htmlContent = "<!DOCTYPE html> <html> <head> <meta charset=\"UTF-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"> <title>" + String(lightName) + " - DiyHue</title> <link rel=\"icon\" type=\"image/png\" href=\"https://diyhue.org/wp-content/uploads/2019/11/cropped-Zeichenfl%C3%A4che-4-1-32x32.png\" sizes=\"32x32\"> <link href=\"https://fonts.googleapis.com/icon?family=Material+Icons\" rel=\"stylesheet\"> <link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/css/materialize.min.css\"> <link rel=\"stylesheet\" href=\"https://diyhue.org/cdn/nouislider.css\" /> </head> <body> <div class=\"wrapper\"> <nav class=\"nav-extended row\" style=\"background-color: #26a69a !important;\"> <div class=\"nav-wrapper col s12\"> <a href=\"#\" class=\"brand-logo\">DiyHue</a> <ul id=\"nav-mobile\" class=\"right hide-on-med-and-down\" style=\"position: relative;z-index: 10;\"> <li><a target=\"_blank\" href=\"https://github.com/diyhue\"><i class=\"material-icons left\">language</i>GitHub</a></li> <li><a target=\"_blank\" href=\"https://diyhue.readthedocs.io/en/latest/\"><i class=\"material-icons left\">description</i>Documentation</a></li> <li><a target=\"_blank\" href=\"https://diyhue.slack.com/\"><i class=\"material-icons left\">question_answer</i>Slack channel</a></li> </ul> </div> <div class=\"nav-content\"> <ul class=\"tabs tabs-transparent\"> <li class=\"tab\" title=\"#home\"><a class=\"active\" href=\"#home\">Home</a></li> <li class=\"tab\" title=\"#preferences\"><a href=\"#preferences\">Preferences</a></li> <li class=\"tab\" title=\"#network\"><a href=\"#network\">Network settings</a></li> <li class=\"tab\" title=\"/update\"><a href=\"/update\">Updater</a></li> </ul> </div> </nav> <ul class=\"sidenav\" id=\"mobile-demo\"> <li><a target=\"_blank\" href=\"https://github.com/diyhue\">GitHub</a></li> <li><a target=\"_blank\" href=\"https://diyhue.readthedocs.io/en/latest/\">Documentation</a></li> <li><a target=\"_blank\" href=\"https://diyhue.slack.com/\">Slack channel</a></li> </ul> <div class=\"container\"> <div class=\"section\"> <div id=\"home\" class=\"col s12\"> <form> <input type=\"hidden\" name=\"section\" value=\"1\"> <div class=\"row\"> <div class=\"col s10\"> <label for=\"power\">Power</label> <div id=\"power\" class=\"switch section\"> <label> Off <input type=\"checkbox\" name=\"pow\" id=\"pow\" value=\"1\"> <span class=\"lever\"></span> On </label> </div> </div> </div> <div class=\"row\"> <div class=\"col s12 m10\"> <label for=\"bri\">Brightness</label> <input type=\"text\" id=\"bri\" class=\"js-range-slider\" name=\"bri\" value=\"\" /> </div> </div> <div class=\"row\"> <div class=\"col s12\"> <label for=\"hue\">Color</label> <div> <canvas id=\"hue\" width=\"320px\" height=\"320px\" style=\"border:1px solid #d3d3d3;\"></canvas> </div> </div> </div> <div class=\"row\"> <div class=\"col s12\"> <label for=\"ct\">Color Temp</label> <div> <canvas id=\"ct\" width=\"320px\" height=\"50px\" style=\"border:1px solid #d3d3d3;\"></canvas> </div> </div> </div> </form> </div> <div id=\"preferences\" class=\"col s12\"> <form method=\"POST\" action=\"/\"> <input type=\"hidden\" name=\"section\" value=\"1\"> <div class=\"row\"> <div class=\"col s12\"> <label for=\"name\">Light Name</label> <input type=\"text\" id=\"name\" name=\"name\"> </div> </div> <div class=\"row\"> <div class=\"col s12 m6\"> <label for=\"startup\">Default Power:</label> <select name=\"startup\" id=\"startup\"> <option value=\"0\">Last State</option> <option value=\"1\">On</option> <option value=\"2\">Off</option> </select> </div> </div> <div class=\"row\"> <div class=\"col s12 m6\"> <label for=\"scene\">Default Scene:</label> <select name=\"scene\" id=\"scene\"> <option value=\"0\">Relax</option> <option value=\"1\">Read</option> <option value=\"2\">Concentrate</option> <option value=\"3\">Energize</option> <option value=\"4\">Bright</option> <option value=\"5\">Dimmed</option> <option value=\"6\">Nightlight</option> <option value=\"7\">Savanna sunset</option> <option value=\"8\">Tropical twilight</option> <option value=\"9\">Arctic aurora</option> <option value=\"10\">Spring blossom</option> </select> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"pixelcount\" class=\"col-form-label\">Pixel count</label> <input type=\"number\" id=\"pixelcount\" name=\"pixelcount\"> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"lightscount\" class=\"col-form-label\">Lights count</label> <input type=\"number\" id=\"lightscount\" name=\"lightscount\"> </div> </div> <label class=\"form-label\">Light division</label> </br> <label>Available Pixels:</label> <label class=\"availablepixels\"><b>null</b></label> <div class=\"row dividedLights\"> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"transitionleds\">Transition leds:</label> <select name=\"transitionleds\" id=\"transitionleds\"> <option value=\"0\">0</option> <option value=\"2\">2</option> <option value=\"4\">4</option> <option value=\"6\">6</option> <option value=\"8\">8</option> <option value=\"10\">10</option> </select> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"rpct\" class=\"form-label\">Red multiplier</label> <input type=\"number\" id=\"rpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"rpct\" value=\"\" /> </div> <div class=\"col s4 m3\"> <label for=\"gpct\" class=\"form-label\">Green multiplier</label> <input type=\"number\" id=\"gpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"gpct\" value=\"\" /> </div> <div class=\"col s4 m3\"> <label for=\"bpct\" class=\"form-label\">Blue multiplier</label> <input type=\"number\" id=\"bpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"bpct\" value=\"\" /> </div> </div> <div class=\"row\"> <label class=\"control-label col s10\">HW buttons:</label> <div class=\"col s10\"> <div class=\"switch section\"> <label> Disable <input type=\"checkbox\" name=\"hwswitch\" id=\"hwswitch\" value=\"1\"> <span class=\"lever\"></span> Enable </label> </div> </div> </div> <div class=\"switchable\"> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"on\">On Pin</label> <input type=\"number\" id=\"on\" name=\"on\"> </div> <div class=\"col s4 m3\"> <label for=\"off\">Off Pin</label> <input type=\"number\" id=\"off\" name=\"off\"> </div> </div> </div> <div class=\"row\"> <label class=\"control-label col s10\">RGB addition on CT:</label> <div class=\"col s10\"> <div class=\"switch section\"> <label> Disable <input type=\"checkbox\" name=\"rgbctswitch\" id=\"rgbctswitch\" value=\"1\"> <span class=\"lever\"></span> Enable </label> </div> </div> </div> <div class=\"switchable\"><div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"rgbctpct\" class=\"col-form-label\">Additional color from RGB on CT in pct</label> <input type=\"number\" id=\"rgbctpct\" name=\"rgbctpct\"> </div> </div> </div> <div class=\"row\"> <label class=\"control-label col s10\">CT blend on RGB:</label> <div class=\"col s10\"> <div class=\"switch section\"> <label> Disable <input type=\"checkbox\" name=\"ctblendswitch\" id=\"ctblendswitch\" value=\"1\"> <span class=\"lever\"></span> Enable </label> </div> </div> </div> <div class=\"switchable\"><div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"ctblendpctc\" class=\"col-form-label\">Blend upto pct on cold white</label> <input type=\"number\" id=\"ctblendpctc\" name=\"ctblendpctc\"> </div><div class=\"col s4 m3\"> <label for=\"ctblendpctw\" class=\"col-form-label\">Blend upto pct on warm white</label> <input type=\"number\" id=\"ctblendpctw\" name=\"ctblendpctw\"> </div> </div> </div> <div class=\"row\"> <div class=\"col s10\"> <button type=\"submit\" class=\"waves-effect waves-light btn teal\">Save</button> <!--<button type=\"submit\" name=\"reboot\" class=\"waves-effect waves-light btn grey lighten-1\">Reboot</button>--> </div> </div> </form> </div> <div id=\"network\" class=\"col s12\"> <form method=\"POST\" action=\"/\"> <input type=\"hidden\" name=\"section\" value=\"2\"> <div class=\"row\"> <div class=\"col s12\"> <label class=\"control-label\">Manual IP assignment:</label> <div class=\"switch section\"> <label> Disable <input type=\"checkbox\" name=\"disdhcp\" id=\"disdhcp\" value=\"0\"> <span class=\"lever\"></span> Enable </label> </div> </div> </div> <div class=\"switchable\"> <div class=\"row\"> <div class=\"col s12 m3\"> <label for=\"addr\">Ip</label> <input type=\"text\" id=\"addr\" name=\"addr\"> </div> <div class=\"col s12 m3\"> <label for=\"sm\">Submask</label> <input type=\"text\" id=\"sm\" name=\"sm\"> </div> <div class=\"col s12 m3\"> <label for=\"gw\">Gateway</label> <input type=\"text\" id=\"gw\" name=\"gw\"> </div> </div> </div> <div class=\"row\"> <div class=\"col s10\"> <button type=\"submit\" class=\"waves-effect waves-light btn teal\">Save</button> <!--<button type=\"submit\" name=\"reboot\" class=\"waves-effect waves-light btn grey lighten-1\">Reboot</button>--> <!--<button type=\"submit\" name=\"reboot\" class=\"waves-effect waves-light btn grey lighten-1\">Reboot</button>--> </div> </div> </form> </div> </div> </div> </div> <script src=\"https://cdnjs.cloudflare.com/ajax/libs/jquery/3.5.1/jquery.min.js\"></script> <script src=\"https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/js/materialize.min.js\"></script> <script src=\"https://diyhue.org/cdn/nouislider.js\"></script> <script src=\"https://diyhue.org/cdn/diyhue.js\"></script> </body> </html>";
    server.send(200, "text/html", htmlContent);
    if (server.args()) {
      delay(1000); // needs to wait until response is received by browser. If ESP restarts too soon, browser will think there was an error.
      ESP.restart();
    }

  });

  server.on("/reset", []() { // trigger manual reset
    server.send(200, "text/html", "reset");
    delay(1000);
    ESP.restart();
  });

  server.onNotFound(handleNotFound);

  server.begin();
}

void entertainment() { // entertainment function
  uint8_t packetSize = Udp.parsePacket(); // check if UDP received some bytes
  if (packetSize) { // if nr of bytes is more than zero
    if (!entertainmentRun) { // announce entertainment is running
      entertainmentRun = true;
    }
    lastEPMillis = millis(); // update variable with last received package timestamp
    Udp.read(packetBuffer, packetSize);
    for (uint8_t i = 0; i < packetSize / 4; i++) { // loop with every light. There are 4 bytes for every light (light number, red, green, blue)
      uint8_t r,g,b;
      r = packetBuffer[i * 4 + 1] * rgb_multiplier[0] / 100;
      g = packetBuffer[i * 4 + 2] * rgb_multiplier[1] / 100;
      b = packetBuffer[i * 4 + 3] * rgb_multiplier[2] / 100;

      convertColorRgbToRgbwc(r, g, b);
      
      for (uint8_t k = 0; k < 5; k++) { //loop with every RGB channel
        lights[packetBuffer[i * 4]].currentColors[k] = covertedColor[k];
      }

    }
    for (uint8_t light = 0; light < lightsCount; light++) { 
      if (lightsCount > 1) {
        long pixelSum = 0;
        for (int value = 0; value < light; value++) {pixelSum += dividedLightsArray[value];}
        if (light == 0) {pixelSum = 0;}
        for (int pixel = 0; pixel < dividedLightsArray[light]; pixel++){ // loop with all leds of the light 
          if (pixel < transitionLeds / 2) { // beginning transition leds
            if (light == 0) { //no transition in front of first light
              strip->SetPixelColor(pixel + pixelSum, convFloat(lights[light].currentColors));
            }
            else {
              strip->SetPixelColor(pixel + pixelSum, blending( lights[light - 1].currentColors, lights[light].currentColors, pixel + 1 + transitionLeds / 2));
              strip->SetPixelColor(pixel + pixelSum - transitionLeds / 2, blending( lights[light - 1].currentColors, lights[light].currentColors, pixel + 1)); //set transition on previous light
            }
          }
          else if (pixel > dividedLightsArray[light] - transitionLeds / 2 - 1) {  // end of transition leds
            if (light == lightsCount - 1) { //no transition on end of last light
              strip->SetPixelColor(pixel + pixelSum , convFloat(lights[light].currentColors));
            }
            else {
              strip->SetPixelColor(pixel + pixelSum, blending( lights[light].currentColors, lights[light + 1].currentColors, pixel + transitionLeds / 2 - dividedLightsArray[light] +1 ));
              strip->SetPixelColor(pixel + pixelSum + transitionLeds / 2, blending( lights[light].currentColors, lights[light + 1].currentColors, pixel + transitionLeds - dividedLightsArray[light] +1 )); //set transition on next light
            }
          }
          else  { // outside transition leds (apply raw color)
            strip->SetPixelColor(pixel + pixelSum, convFloat(lights[light].currentColors));
          }
        }
      } else {
        strip->ClearTo(convFloat(lights[light].currentColors), 0, pixelCount - 1);
      }
    }
    strip->Show();
  }
}

void loop() {
  server.handleClient();
  if (!entertainmentRun) {
    lightEngine(); // process lights data set on http server
  } else {
    if ((millis() - lastEPMillis) >= ENTERTAINMENT_TIMEOUT) { // entertainment stream stop (timeout)
      entertainmentRun = false;
      for (uint8_t i = 0; i < lightsCount; i++) {
        processLightdata(i, 4); //return to original colors with 0.4 sec transition
      }
    }
  }
  entertainment(); // process entertainment data on UDP server
}
