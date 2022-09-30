
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <HTTPUpdateServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <NeoPixelBus.h>

IPAddress address ( 192,  168,   0,  95); // choose an unique IP Adress
IPAddress gateway ( 192,  168,   0,   1); // Router IP
IPAddress submask(255, 255, 255,   0);

#define LIGHT_VERSION 4.2
#define LIGHT_NAME_MAX_LENGTH 32 // Longer name will get stripped
#define ENTERTAINMENT_TIMEOUT 1500 // millis
#define ledGPIO 19
#define DATA_PIN 4

// Reference:
// https://andi-siess.de/rgb-to-color-temperature/

// Warm White (3000K)
// #define WHITE_CT_R 255
// #define WHITE_CT_G 180
// #define WHITE_CT_B 107

// Natural White (4300K)
#define WHITE_CT_R 255
#define WHITE_CT_G 215
#define WHITE_CT_B 177

// Cool White (6500K)
// #define WHITE_CT_R 255
// #define WHITE_CT_G 249
// #define WHITE_CT_B 253



struct state {
  uint8_t colors[3];
  float x, y, stepLevel[3], currentColors[3];
};

uint16_t ct = 200, hue;
bool lightState;
uint8_t bri = 100, sat = 254, colorMode = 2;

state lights[10];
bool inTransition, entertainmentRun, mosftetState, useDhcp = true;
byte mac[6], packetBuffer[46];
unsigned long lastEPMillis, lastWiFiCheck;

//settings
char lightName[LIGHT_NAME_MAX_LENGTH] = "DiyHue Light";
uint8_t effect, scene, startup, onPin = 9, offPin = 8 ;
bool hwSwitch = false;
uint8_t rgb_multiplier[] = {100, 100, 100}; // light multiplier in percentage /R, G, B/

uint8_t lightsCount = 3;

uint16_t pixelCount = 44;
uint16_t lightLedsCount = pixelCount / (lightsCount - 1);

WebServer server(80);
WiFiUDP Udp;
HTTPUpdateServer httpUpdateServer;
WiFiManager wm;

RgbColor red = RgbColor(255, 0, 0);
RgbColor green = RgbColor(0, 255, 0);
RgbColor white = RgbColor(255);
RgbColor black = RgbColor(0);

NeoPixelBus<NeoBrgFeature, NeoEsp32I2s1Tm1829Method>* strip = NULL;

void blinkLed(uint8_t count, uint16_t interval = 200) {
  for (uint8_t i = 0; i < count; i++) {
    digitalWrite(ledGPIO, LOW);
    delay(interval);
    digitalWrite(ledGPIO, HIGH);
    delay(interval);
  }
}

const uint8_t kWhiteRedChannel = WHITE_CT_R;
const uint8_t kWhiteGreenChannel = WHITE_CT_G;
const uint8_t kWhiteBlueChannel = WHITE_CT_B;

// The transformation has to be normalized to 255
static_assert(kWhiteRedChannel >= 255 ||
              kWhiteGreenChannel >= 255 ||
              kWhiteBlueChannel >= 255);


void factoryReset() {
  //Serial.println("FRST");
  digitalWrite(ledGPIO, HIGH);
  LittleFS.format();
  WiFi.disconnect(false, true);
  blinkLed(8, 100);
  ESP.restart();
}

void convertHue(uint8_t light) // convert hue / sat values from HUE API to RGB
{
  double      hh, p, q, t, ff, s, v;
  long        i;

  s = sat / 255.0;
  v = bri / 255.0;

  if (s <= 0.0) {      // < is bogus, just shuts up warnings
    lights[light].colors[0] = v;
    lights[light].colors[1] = v;
    lights[light].colors[2] = v;
    return;
  }
  hh = hue;
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
      break;
    case 1:
      lights[light].colors[0] = q * 255.0;
      lights[light].colors[1] = v * 255.0;
      lights[light].colors[2] = p * 255.0;
      break;
    case 2:
      lights[light].colors[0] = p * 255.0;
      lights[light].colors[1] = v * 255.0;
      lights[light].colors[2] = t * 255.0;
      break;

    case 3:
      lights[light].colors[0] = p * 255.0;
      lights[light].colors[1] = q * 255.0;
      lights[light].colors[2] = v * 255.0;
      break;
    case 4:
      lights[light].colors[0] = t * 255.0;
      lights[light].colors[1] = p * 255.0;
      lights[light].colors[2] = v * 255.0;
      break;
    case 5:
    default:
      lights[light].colors[0] = v * 255.0;
      lights[light].colors[1] = p * 255.0;
      lights[light].colors[2] = q * 255.0;
      break;
  }
  convertRgbToRgbw(light);
}

void convertXy(uint8_t light) // convert CIE xy values from HUE API to RGB
{
  uint8_t optimal_bri = bri;
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

  lights[light].colors[0] = (int) (r * optimal_bri); lights[light].colors[1] = (int) (g * optimal_bri); lights[light].colors[2] = (int) (b * optimal_bri);
  convertRgbToRgbw(light);
}

void convertCt(uint8_t light) // convert ct (color temperature) value from HUE API to RGB
{
  int hectemp = 10000 / ct;
  uint8_t r, g, b;
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

  lights[light].colors[0] = r * (bri / 255.0f); lights[light].colors[1] = g * (bri / 255.0f); lights[light].colors[2] = b * (bri / 255.0f);
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

void convertRgbToRgbw(uint8_t light) {
  //Get the maximum between R, G, and B
  uint8_t r = lights[light].colors[0];
  uint8_t g = lights[light].colors[1];
  uint8_t b = lights[light].colors[2];

  // These values are what the 'white' value would need to
  // be to get the corresponding color value.
  float whiteValueForRed = r * 255.0 / kWhiteRedChannel;
  float whiteValueForGreen = g * 255.0 / kWhiteGreenChannel;
  float whiteValueForBlue = b * 255.0 / kWhiteBlueChannel;

  // Set the white value to the highest it can be for the given color
  // (without over saturating any channel - thus the minimum of them).
  float minWhiteValue = min(whiteValueForRed,
                            min(whiteValueForGreen,
                                whiteValueForBlue));

  // The rest of the channels will just be the original value minus the
  // contribution by the white channel.
  uint8_t Ro = (uint8_t)(r - minWhiteValue * kWhiteRedChannel / 255);
  uint8_t Go = (uint8_t)(g - minWhiteValue * kWhiteGreenChannel / 255);
  uint8_t Bo = (uint8_t)(b - minWhiteValue * kWhiteBlueChannel / 255);

  lights[light].colors[0] = Ro;
  lights[light].colors[1] = Go;
  lights[light].colors[2] = Bo;
}

void infoLight(RgbColor color) { // boot animation for leds count and wifi test
  // Flash the strip in the selected color. White = booted, green = WLAN connected, red = WLAN could not connect
  for (uint16_t i = 0; i < pixelCount; i++)
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
      bri = 254; ct = 346; colorMode = 2; convertCt(light);
    } else if ( new_scene == 2) {
      bri = 254; ct = 233; colorMode = 2; convertCt(light);
    }  else if ( new_scene == 3) {
      bri = 254; ct = 156; colorMode = 2; convertCt(light);
    }  else if ( new_scene == 4) {
      bri = 77; ct = 367; colorMode = 2; convertCt(light);
    }  else if ( new_scene == 5) {
      bri = 254; ct = 447; colorMode = 2; convertCt(light);
    }  else if ( new_scene == 6) {
      bri = 1; lights[light].x = 0.561; lights[light].y = 0.4042; colorMode = 1; convertXy(light);
    }  else if ( new_scene == 7) {
      bri = 203; lights[light].x = 0.380328; lights[light].y = 0.39986; colorMode = 1; convertXy(light);
    }  else if ( new_scene == 8) {
      bri = 112; lights[light].x = 0.359168; lights[light].y = 0.28807; colorMode = 1; convertXy(light);
    }  else if ( new_scene == 9) {
      bri = 142; lights[light].x = 0.267102; lights[light].y = 0.23755; colorMode = 1; convertXy(light);
    }  else if ( new_scene == 10) {
      bri = 216; lights[light].x = 0.393209; lights[light].y = 0.29961; colorMode = 1; convertXy(light);
    } else {
      bri = 144; ct = 447; colorMode = 2; convertCt(light);
    }
  }
}

void processLightdata(uint8_t light, float transitiontime = 4) { // calculate the step level of every RGB channel for a smooth transition in requested transition time
  transitiontime *= 17 - (pixelCount / 40); //every extra led add a small delay that need to be counted for transition time match
  if (colorMode == 1 && lightState == true) {
    convertXy(light);
  } else if (colorMode == 2 && lightState == true) {
    convertCt(light);
  } else if (colorMode == 3 && lightState == true) {
    convertHue(light);
  }
  for (uint8_t i = 0; i < 3; i++) {
    if (lightState) {
      lights[light].stepLevel[i] = ((float)lights[light].colors[i] - lights[light].currentColors[i]) / transitiontime;
    } else {
      lights[light].stepLevel[i] = lights[light].currentColors[i] / transitiontime;
    }
  }
  inTransition = true;
}


void candleEffect() {
  for (uint8_t light = 0; light < lightsCount; light++) {
    lights[light].colors[0] = random(170, 254);
    lights[light].colors[1] = random(37, 62);
    lights[light].colors[2] = 0;
    for (uint8_t i = 0; i < 3; i++) {
      lights[light].stepLevel[i] = ((float)lights[light].colors[i] - lights[light].currentColors[i]) / random(5, 15);
    }
  }
  inTransition = true;
}

void firePlaceEffect() {
  for (uint8_t light = 0; light < lightsCount; light++) {
    lights[light].colors[0] = random(100, 254);
    lights[light].colors[1] = random(10, 35);
    lights[light].colors[2] = 0;
    for (uint8_t i = 0; i < 3; i++) {
      lights[light].stepLevel[i] = ((float)lights[light].colors[i] - lights[light].currentColors[i]) / random(5, 15);
    }
  }
  inTransition = true;
}

RgbColor convFloat(float color[3]) { // return RgbColor from float
  return RgbColor((uint8_t)color[0], (uint8_t)color[1], (uint8_t)color[2]);
}

void togglePower() {

}


uint8_t pixelLight (uint8_t pixel) {
  return int(pixel / lightLedsCount);
}

void manageButtons() {
  if (digitalRead(onPin) == LOW) { // on button pressed
    uint8_t i = 0;
    while (digitalRead(onPin) == LOW && i < 30) { // count how log is the button pressed
      delay(20);
      i++;
    }
    // factory reset if onPin also pressed
    if (i == 30 && digitalRead(offPin) == LOW) {
      delay(2000);
      if (digitalRead(onPin) == LOW && digitalRead(offPin) == LOW) {
        factoryReset();
      }
    }
    for (uint8_t light = 0; light < lightsCount; light++) {
      if (i < 30) { // there was a short press
        togglePower();
        lightState = true;
      }
      else { // there was a long press
        if (bri < 210) {
          bri += 30;
        } else {
          bri = 254;
        }
      }
      processLightdata(light);
    }
  } else if (digitalRead(offPin) == LOW) { // off button pressed
    uint8_t i = 0;
    while (digitalRead(offPin) == LOW && i < 30) {
      delay(20);
      i++;
    }
    for (uint8_t light = 0; light < lightsCount; light++) {
      if (i < 30) {
        // there was a short press
        lightState = false;
      }
      else {
        // there was a long press
        if (bri > 32) {
          bri -= 30;
        } else {
          bri = 1;
        }

      }
      processLightdata(light);
    }
  }
}

void lightEngine() {
  if (inTransition) {
    if (lightsCount == 1) {
      strip->ClearTo(convFloat(lights[0].currentColors), 0, pixelCount - 1);
    } else {
      for (uint8_t pixel = 0; pixel < pixelCount; pixel++) {
        uint8_t light = pixelLight(pixel);
        strip->SetPixelColor(pixel, RgbColor::LinearBlend(convFloat(lights[light].currentColors), convFloat(lights[light + 1].currentColors), (float)(pixel - (light * lightLedsCount)) / (float)lightLedsCount));
      }
    }
    strip->Show();
    bool allStatesSet = true;
    for (uint8_t light = 0; light < lightsCount; light++) {
      if (lightState) {
        if (lights[light].colors[0] != lights[light].currentColors[0] || lights[light].colors[1] != lights[light].currentColors[1] || lights[light].colors[2] != lights[light].currentColors[2]) { // if not all RGB channels of the light are at desired level
          for (uint8_t k = 0; k < 3; k++) { // loop with every RGB channel of the light
            if (lights[light].colors[k] != lights[light].currentColors[k]) lights[light].currentColors[k] += lights[light].stepLevel[k]; // move RGB channel on step closer to desired level
            if ((lights[light].stepLevel[k] > 0.0 && lights[light].currentColors[k] > lights[light].colors[k]) || (lights[light].stepLevel[k] < 0.0 && lights[light].currentColors[k] < lights[light].colors[k])) lights[light].currentColors[k] = lights[light].colors[k]; // if the current level go below desired level apply directly the desired level.
          }
          allStatesSet = false;
        }
      } else {
        if (lights[light].currentColors[0] != 0 || lights[light].currentColors[1] != 0 || lights[light].currentColors[2] != 0) { // proceed forward only in case not all RGB channels are zero
          for (uint8_t k = 0; k < 3; k++) { //loop with every RGB channel
            if (lights[light].currentColors[k] != 0) lights[light].currentColors[k] -= lights[light].stepLevel[k]; // remove one step level
            if (lights[light].currentColors[k] < 0) lights[light].currentColors[k] = 0; // save condition, if level go below zero set it to zero
          }
          allStatesSet = false;
        }
      }
    }
    if (allStatesSet) {
      inTransition = false;
    }
    delay(5);
  } else {
    togglePower();
    if (effect == 1) { // candle effect
      candleEffect();
    } else if (effect == 2) { // fireplace effect
      firePlaceEffect();
    }
    if (hwSwitch == true) {
      manageButtons();
    }
  }
}


void saveState() { // save the lights state on LittleFS partition in JSON format
  DynamicJsonDocument json(1024);
  json["on"] = lightState;
  json["bri"] = bri;
  if (colorMode == 2) {
    json["ct"] = ct;
  } else if (colorMode == 3) {
    json["hue"] = hue;
    json["sat"] = sat;
  }
  if (colorMode == 1) {
    JsonArray points = json.createNestedArray("points");
    for (uint8_t i = 0; i < lightsCount; i++) {
      DynamicJsonDocument point(128);
      point["x"] = lights[i].x;
      point["y"] = lights[i].y;
      points.add(point);
    }
  }
  File stateFile = LittleFS.open("/state.json", FILE_WRITE);
  serializeJson(json, stateFile);
}

void restoreState() { // restore the lights state from LittleFS partition
  File stateFile = LittleFS.open("/state.json", FILE_READ);
  if (!stateFile) {
    saveState();
    return;
  }

  DynamicJsonDocument json(1024);
  DeserializationError error = deserializeJson(json, stateFile.readString());
  if (error) {
    //Serial.print(F("deserializeJson() state file failed: "));
    //Serial.println(error.c_str());
    return;
  }
  String output;
  serializeJson(json, output);
  //Serial.println(output);
  lightState = json["on"];
  bri = (uint8_t) json["bri"];
  if (json.containsKey("ct")) {
    ct = (uint16_t) json["ct"];
    colorMode = 2;
  } else if (json.containsKey("hue")) {
    hue = (uint16_t) json["hue"];
    sat = (uint8_t) json["sat"];
    colorMode = 3;
  } else if (json.containsKey("points")) {

    lightsCount = 0;
    for (JsonObject point : json["points"].as<JsonArray>()) {
      lights[lightsCount].x = point["x"];
      lights[lightsCount].y = point["y"];
      lightsCount += 1;
    }
    lightLedsCount = pixelCount / (lightsCount - 1);
    colorMode = 1;
  }
}


bool saveConfig() { // save config in LittleFS partition in JSON file
  DynamicJsonDocument json(512);
  json["name"] = lightName;
  json["startup"] = startup;
  json["scene"] = scene;
  json["on"] = onPin;
  json["off"] = offPin;
  json["hw"] = hwSwitch;
  json["dhcp"] = useDhcp;
  json["pixelCount"] = pixelCount;
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
  File configFile = LittleFS.open("/config.json", FILE_WRITE);
  if (!configFile) {
    //Serial.println("Failed to open config file for writing");
    return false;
  }

  serializeJson(json, configFile);
  return true;
}

bool loadConfig() { // load the configuration from LittleFS partition
  File configFile = LittleFS.open("/config.json", FILE_READ);
  if (!configFile) {
    //Serial.println("Create new file with default values");
    return saveConfig();
  }

  if (configFile.size() > 512) {
    //Serial.println("Config file size is too large");
    return false;
  }

  DynamicJsonDocument json(512);
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
  pixelCount = (uint16_t) json["pixelCount"];
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

void ChangeNeoPixels(uint16_t newCount) // this set the number of leds of the strip based on web configuration
{
  if (strip != NULL) {
    delete strip; // delete the previous dynamically created strip
  }
  strip = new NeoPixelBus<NeoBrgFeature, NeoEsp32I2s1Tm1829Method>(newCount, DATA_PIN); // and recreate with new count
  strip->Begin();
}

void setup() {
  //Serial.begin(115200);
  //Serial.println();
  delay(500);
  pinMode(ledGPIO, OUTPUT);
  blinkLed(2);

  //Serial.println("mounting FS...");

  if (!LittleFS.begin(true)) {
    //Serial.println("Failed to mount file system");
  }

  if (!loadConfig()) {
    //Serial.println("Failed to load config");
  } else {
    //Serial.println("Config loaded");
  }

  ChangeNeoPixels(pixelCount);
  lightLedsCount = pixelCount / (lightsCount - 1);

  if (startup == 1) {
    lightState = true;
  }
  if (startup == 0) {
    restoreState();
  } else {
    apply_scene(scene);
  }
  for (uint8_t i = 0; i < lightsCount; i++) {
    processLightdata(i);
  }
  if (lightState) {
    for (uint8_t i = 0; i < 200; i++) {
      lightEngine();
    }
  }


  if (hwSwitch == true) { // set buttons pins mode in case are used
    pinMode(onPin, INPUT);
    pinMode(offPin, INPUT);

  if (digitalRead(offPin) == LOW) {
    factoryReset();
  }
  }

  WiFi.mode(WIFI_STA);
  wm.setDebugOutput(false);
  wm.setConfigPortalTimeout(120);
  if (!useDhcp) {
    wm.setSTAStaticIPConfig(address, gateway, submask);
  }

  bool res;
  res = wm.autoConnect(lightName);

  if (!res) {
    //Serial.println("RST1");
    ESP.restart();
  }

  if (useDhcp) {
    address = WiFi.localIP();
    gateway = WiFi.gatewayIP();
    submask = WiFi.subnetMask();
  }


  if (! lightState) { // test if light zero (must be at last one light) is not set to ON
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

  server.on("/state", HTTP_PUT, []() { // HTTP PUT request used to set a new light state
    bool stateSave = false;
    DynamicJsonDocument root(1024);
    DeserializationError error = deserializeJson(root, server.arg("plain"));

    if (error) {
      server.send(404, "text/plain", "FAIL. " + server.arg("plain"));
    } else {
      if (root.containsKey("effect")) {
        if (root["effect"] == "no_effect") {
          effect = 0;
        } else if (root["effect"] == "candle") {
          effect = 1;
        } else if (root["effect"] == "fire") {
          effect = 2;
        }
      }
      if (root.containsKey("gradient")) {
        if (root["gradient"].containsKey("points")) {
          lightsCount = root["gradient"]["points"].size();
          lightLedsCount = pixelCount / (lightsCount - 1);
          for (uint8_t light = 0; light < lightsCount; light++) {
            if (root["gradient"]["points"][light]["color"].containsKey("xy")) {
              lights[light].x = root["gradient"]["points"][light]["color"]["xy"]["x"];
              lights[light].y = root["gradient"]["points"][light]["color"]["xy"]["y"];
            }
          }
          colorMode = 1;
        }
      }
      if (root.containsKey("xy")) {
        for (uint8_t light = 0; light < lightsCount; light++) {
          lights[light].x = root["xy"][0];
          lights[light].y = root["xy"][1];
        }
        colorMode = 1;
      } else if (root.containsKey("ct")) {
        ct = root["ct"];
        colorMode = 2;
      } else {
        if (root.containsKey("hue")) {
          hue = root["hue"];
          colorMode = 3;
        }
        if (root.containsKey("sat")) {
          sat = root["sat"];
          colorMode = 3;
        }
      }

      if (root.containsKey("on")) {
        if (root["on"]) {
          lightState = true;
          togglePower();// restore power
        } else {
          lightState = false;
        }
        if (startup == 0) {
          stateSave = true;
        }
      }

      if (root.containsKey("bri")) {
        bri = root["bri"];
      }

      if (root.containsKey("bri_inc")) {
        if (root["bri_inc"] > 0) {
          if (bri + (int) root["bri_inc"] > 254) {
            bri = 254;
          } else {
            bri += (int) root["bri_inc"];
          }
        } else {
          if (bri - (int) root["bri_inc"] < 1) {
            bri = 1;
          } else {
            bri += (int) root["bri_inc"];
          }
        }
      }
      uint16_t transitiontime = 4;
      if (root.containsKey("transitiontime")) {
        transitiontime = root["transitiontime"];
      }
      for (uint8_t light = 0; light < lightsCount; light++) {
        if (root.containsKey("alert") && root["alert"] == "select") {
          if (lightState) {
            lights[light].currentColors[0] = 0; lights[light].currentColors[1] = 0; lights[light].currentColors[2] = 0;
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
    uint8_t light = 0;
    DynamicJsonDocument root(1024);
    root["on"] = lightState;
    root["bri"] = bri;
    JsonArray xy = root.createNestedArray("xy");
    xy.add(lights[0].x);
    xy.add(lights[0].y);
    root["ct"] = ct;
    root["hue"] = hue;
    root["sat"] = sat;
    if (colorMode == 1)
      root["colormode"] = "xy";
    else if (colorMode == 2)
      root["colormode"] = "ct";
    else if (colorMode == 3)
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
    root["protocol"] = "native_single";
    root["modelid"] = "LCX004";
    root["type"] = "tm1829_gradient_lightstrip";
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
    root["hwswitch"] = (int)hwSwitch;
    root["pixelcount"] = pixelCount;
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
      pixelCount = server.arg("pixelcount").toInt();
      rgb_multiplier[0] = server.arg("rpct").toInt();
      rgb_multiplier[1] = server.arg("gpct").toInt();
      rgb_multiplier[2] = server.arg("bpct").toInt();
      hwSwitch = server.hasArg("hwswitch") ? server.arg("hwswitch").toInt() : 0;
      if (server.hasArg("hwswitch")) {
        onPin = server.arg("on").toInt();
        offPin = server.arg("off").toInt();
      }
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
    String htmlContent = "<!DOCTYPE html><html><head> <meta charset=\"UTF-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"> <title>Hue Gradient Lightstrip - DiyHue</title> <link rel=\"icon\" type=\"image/png\" href=\"https://diyhue.org/wp-content/uploads/2019/11/cropped-Zeichenfl%C3%A4che-4-1-32x32.png\" sizes=\"32x32\"> <link href=\"https://fonts.googleapis.com/icon?family=Material+Icons\" rel=\"stylesheet\"> <link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/css/materialize.min.css\"> <link rel=\"stylesheet\" href=\"https://diyhue.org/cdn/nouislider.css\"/></head><body> <div class=\"wrapper\"> <nav class=\"nav-extended row\" style=\"background-color: #26a69a !important;\"> <div class=\"nav-wrapper col s12\"> <a href=\"#\" class=\"brand-logo\">DiyHue</a> <ul id=\"nav-mobile\" class=\"right hide-on-med-and-down\" style=\"position: relative;z-index: 10;\"> <li><a target=\"_blank\" href=\"https://github.com/diyhue\"><i class=\"material-icons left\">language</i>GitHub</a></li><li><a target=\"_blank\" href=\"https://diyhue.readthedocs.io/en/latest/\"><i class=\"material-icons left\">description</i>Documentation</a></li><li><a target=\"_blank\" href=\"https://diyhue.slack.com/\"><i class=\"material-icons left\">question_answer</i>Slack channel</a></li></ul> </div><div class=\"nav-content\"> <ul class=\"tabs tabs-transparent\"> <li class=\"tab\" title=\"#home\"><a class=\"active\" href=\"#home\">Home</a></li><li class=\"tab\" title=\"#preferences\"><a href=\"#preferences\">Preferences</a></li><li class=\"tab\" title=\"#network\"><a href=\"#network\">Network settings</a></li><li class=\"tab\" title=\"/update\"><a href=\"/update\">Updater</a></li></ul> </div></nav> <ul class=\"sidenav\" id=\"mobile-demo\"> <li><a target=\"_blank\" href=\"https://github.com/diyhue\">GitHub</a></li><li><a target=\"_blank\" href=\"https://diyhue.readthedocs.io/en/latest/\">Documentation</a></li><li><a target=\"_blank\" href=\"https://diyhue.slack.com/\">Slack channel</a></li></ul> <div class=\"container\"> <div class=\"section\"> <div id=\"home\" class=\"col s12\"> <form> <input type=\"hidden\" name=\"section\" value=\"1\"> <div class=\"row\"> <div class=\"col s10\"> <label for=\"power\">Power</label> <div id=\"power\" class=\"switch section\"> <label> Off <input type=\"checkbox\" name=\"pow\" id=\"pow\" value=\"1\"> <span class=\"lever\"></span> On </label> </div></div></div><div class=\"row\"> <div class=\"col s12 m10\"> <label for=\"bri\">Brightness</label> <input type=\"text\" id=\"bri\" class=\"js-range-slider\" name=\"bri\" value=\"\"/> </div></div><div class=\"row\"> <div class=\"col s12\"> <label for=\"hue\">Color</label> <div id=\"picker\" width=\"320px\" height=\"320px\"></div></div></div><div class=\"row\"> <div class=\"col s12\"> <label for=\"ct\">Color Temp</label> <div id=\"ct\" width=\"320px\" height=\"50px\"></div></div></div></form> </div><div id=\"preferences\" class=\"col s12\"> <form method=\"POST\" action=\"/\"> <input type=\"hidden\" name=\"section\" value=\"1\"> <div class=\"row\"> <div class=\"col s12\"> <label for=\"name\">Light Name</label> <input type=\"text\" id=\"name\" name=\"name\"> </div></div><div class=\"row\"> <div class=\"col s12 m6\"> <label for=\"startup\">Default Power:</label> <select name=\"startup\" id=\"startup\"> <option value=\"0\">Last State</option> <option value=\"1\">On</option> <option value=\"2\">Off</option> </select> </div></div><div class=\"row\"> <div class=\"col s12 m6\"> <label for=\"scene\">Default Scene:</label> <select name=\"scene\" id=\"scene\"> <option value=\"0\">Relax</option> <option value=\"1\">Read</option> <option value=\"2\">Concentrate</option> <option value=\"3\">Energize</option> <option value=\"4\">Bright</option> <option value=\"5\">Dimmed</option> <option value=\"6\">Nightlight</option> <option value=\"7\">Savanna sunset</option> <option value=\"8\">Tropical twilight</option> <option value=\"9\">Arctic aurora</option> <option value=\"10\">Spring blossom</option> </select> </div></div><div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"pixelcount\" class=\"col-form-label\">Pixel count</label> <input type=\"number\" id=\"pixelcount\" name=\"pixelcount\"> </div></div><div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"rpct\" class=\"form-label\">Red multiplier</label> <input type=\"number\" id=\"rpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"rpct\" value=\"\"/> </div><div class=\"col s4 m3\"> <label for=\"gpct\" class=\"form-label\">Green multiplier</label> <input type=\"number\" id=\"gpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"gpct\" value=\"\"/> </div><div class=\"col s4 m3\"> <label for=\"bpct\" class=\"form-label\">Blue multiplier</label> <input type=\"number\" id=\"bpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"bpct\" value=\"\"/> </div></div><div class=\"row\"> <label class=\"control-label col s10\">HW buttons:</label> <div class=\"col s10\"> <div class=\"switch section\"> <label> Disable <input type=\"checkbox\" name=\"hwswitch\" id=\"hwswitch\" value=\"1\"> <span class=\"lever\"></span> Enable </label> </div></div></div><div class=\"switchable\"> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"on\">On Pin</label> <input type=\"number\" id=\"on\" name=\"on\"> </div><div class=\"col s4 m3\"> <label for=\"off\">Off Pin</label> <input type=\"number\" id=\"off\" name=\"off\"> </div></div></div><div class=\"row\"> <div class=\"col s10\"> <button type=\"submit\" class=\"waves-effect waves-light btn teal\">Save</button> </div></div></form> </div><div id=\"network\" class=\"col s12\"> <form method=\"POST\" action=\"/\"> <input type=\"hidden\" name=\"section\" value=\"2\"> <div class=\"row\"> <div class=\"col s12\"> <label class=\"control-label\">Manual IP assignment:</label> <div class=\"switch section\"> <label> Disable <input type=\"checkbox\" name=\"disdhcp\" id=\"disdhcp\" value=\"0\"> <span class=\"lever\"></span> Enable </label> </div></div></div><div class=\"switchable\"> <div class=\"row\"> <div class=\"col s12 m3\"> <label for=\"addr\">Ip</label> <input type=\"text\" id=\"addr\" name=\"addr\"> </div><div class=\"col s12 m3\"> <label for=\"sm\">Submask</label> <input type=\"text\" id=\"sm\" name=\"sm\"> </div><div class=\"col s12 m3\"> <label for=\"gw\">Gateway</label> <input type=\"text\" id=\"gw\" name=\"gw\"> </div></div></div><div class=\"row\"> <div class=\"col s10\"> <button type=\"submit\" class=\"waves-effect waves-light btn teal\">Save</button> </div></div></form> </div></div></div></div><script src=\"https://cdnjs.cloudflare.com/ajax/libs/jquery/3.5.1/jquery.min.js\"></script> <script src=\"https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/js/materialize.min.js\"></script> <script src=\"https://diyhue.org/cdn/iro.min.js\"></script> <script src=\"https://diyhue.org/cdn/nouislider.js\"></script> <script src=\"https://diyhue.org/cdn/gradient-iro.js\"></script> <script></script></body></html>";
    server.send(200, "text/html", htmlContent);
    if (server.args()) {
      delay(1000); // needs to wait until response is received by browser. If ESP restarts too soon, browser will think there was an error.
      //Serial.println("RST2");
      ESP.restart();
    }

  });

  server.on("/reset", []() { // trigger manual reset
    server.send(200, "text/html", "reset");
    delay(1000);
    //Serial.println("RST3");
    ESP.restart();
  });


  server.on("/factory", []() { // trigger manual reset
    server.send(200, "text/html", "factory reset");
    factoryReset();
  });

  server.onNotFound(handleNotFound);

  server.begin();

  digitalWrite(ledGPIO, LOW); // turn off the led on full init.
}

void entertainment() { // entertainment function
  uint8_t packetSize = Udp.parsePacket(); // check if UDP received some bytes
  if (packetSize) { // if nr of bytes is more than zero
    if (!entertainmentRun) { // announce entertainment is running
      entertainmentRun = true;
    }
    lastEPMillis = millis(); // update variable with last received package timestamp
    Udp.read(packetBuffer, packetSize);
    lightsCount = packetSize / 4;
    lightLedsCount = pixelCount / (lightsCount - 1);
    for (uint8_t i = 0; i < packetSize / 4; i++) { // loop with every light. There are 4 bytes for every light (light number, red, green, blue)
      float r, g, b;
      r = lights[packetBuffer[i * 4]].currentColors[0] = packetBuffer[i * 4 + 1] * rgb_multiplier[0] / 100;
      g = lights[packetBuffer[i * 4]].currentColors[1] = packetBuffer[i * 4 + 2] * rgb_multiplier[1] / 100;
      b = lights[packetBuffer[i * 4]].currentColors[2] = packetBuffer[i * 4 + 3] * rgb_multiplier[2] / 100;

      float whiteValueForRed = r * 255.0 / kWhiteRedChannel;
      float whiteValueForGreen = g * 255.0 / kWhiteGreenChannel;
      float whiteValueForBlue = b * 255.0 / kWhiteBlueChannel;

      float minWhiteValue = min(whiteValueForRed,
                                min(whiteValueForGreen,
                                    whiteValueForBlue));
      lights[packetBuffer[i * 4]].currentColors[0] = r - minWhiteValue * kWhiteRedChannel / 255;
      lights[packetBuffer[i * 4]].currentColors[1] = g - minWhiteValue * kWhiteGreenChannel / 255;
      lights[packetBuffer[i * 4]].currentColors[2] = b - minWhiteValue * kWhiteBlueChannel / 255;
    }
    for (uint8_t light = 0; light < lightsCount; light++) {

      if (light == 0) {
        if (lightsCount == 2) {
          for (uint8_t pixel = 0; pixel < pixelCount; pixel++) {
            strip->SetPixelColor(pixel, RgbColor::LinearBlend(convFloat(lights[0].currentColors), convFloat(lights[1].currentColors),  (float)(pixel) / (float)pixelCount));
          }
        } else {
          for (uint8_t pixel = 0; pixel < lightLedsCount; pixel++) {
            strip->SetPixelColor(pixel, RgbColor::LinearBlend(convFloat(lights[0].currentColors), convFloat(lights[1].currentColors),  (float)(pixel) / (float)lightLedsCount));
          }
        }
      } else if (light != lightsCount - 1) {
        for (uint8_t pixel = 0; pixel < lightLedsCount ; pixel++) {
          strip->SetPixelColor(pixel + lightLedsCount * light, RgbColor::LinearBlend(convFloat(lights[light].currentColors), convFloat(lights[light + 1].currentColors), (float)(pixel) / (float)lightLedsCount));
        }
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
        processLightdata(i); //return to original colors with 0.4 sec transition
      }
    }
  }
  entertainment(); // process entertainment data on UDP server

  // blink the led if signal is lost
  if (millis() >=  lastWiFiCheck + 10000)  {
    if (WiFi.status() != WL_CONNECTED) {
      blinkLed(5);
      digitalWrite(ledGPIO, HIGH);
    } else {
      digitalWrite(ledGPIO, LOW);
    }
    lastWiFiCheck = millis();
  }
}
