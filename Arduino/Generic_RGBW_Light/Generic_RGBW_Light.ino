/*
  This can control bulbs with 5 pwm channels (red, gree, blue, warm white and could wihite). Is tested with MiLight colors bulb.
*/
#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>


// Define your white led color temp here (Range 2000-6536K).
// For warm-white led try 2000K, for cold-white try 6000K
#define WHITE_TEMP 2000 // kelvin

//#define USE_STATIC_IP //! uncomment to enable Static IP Adress
#ifdef USE_STATIC_IP
IPAddress strip_ip ( 192,  168,   0,  95); // choose an unique IP Adress
IPAddress gateway_ip ( 192,  168,   0,   1); // Router IP
IPAddress subnet_mask(255, 255, 255,   0);
#endif

#define PWM_CHANNELS 4

//core
uint8_t colors[PWM_CHANNELS], bri, sat, colorMode;
bool lightState, inTransition;
int ct, hue;
float stepLevel[PWM_CHANNELS], currentColors[PWM_CHANNELS], x, y;
byte mac[6];
byte packetBuffer[8];

//settings
char *lightName = "New Hue RGBW light";
uint8_t scene, startup, onPin, offPin, pins[PWM_CHANNELS]; //red, green, blue, could white, warm white
bool hwSwitch;

ESP8266WebServer server(80);
WiFiUDP Udp;
ESP8266HTTPUpdateServer httpUpdateServer;

void convert_hue()
{
  double      hh, p, q, t, ff, s, v;
  long        i;

  colors[3] = 0;
  s = sat / 255.0;
  v = bri / 255.0;

  if (s <= 0.0) {      // < is bogus, just shuts up warnings
    colors[0] = v;
    colors[1] = v;
    colors[2] = v;
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
      colors[0] = v * 255.0;
      colors[1] = t * 255.0;
      colors[2] = p * 255.0;
      break;
    case 1:
      colors[0] = q * 255.0;
      colors[1] = v * 255.0;
      colors[2] = p * 255.0;
      break;
    case 2:
      colors[0] = p * 255.0;
      colors[1] = v * 255.0;
      colors[2] = t * 255.0;
      break;

    case 3:
      colors[0] = p * 255.0;
      colors[1] = q * 255.0;
      colors[2] = v * 255.0;
      break;
    case 4:
      colors[0] = t * 255.0;
      colors[1] = p * 255.0;
      colors[2] = v * 255.0;
      break;
    case 5:
    default:
      colors[0] = v * 255.0;
      colors[1] = p * 255.0;
      colors[2] = q * 255.0;
      break;
  }

}

void convert_xy()
{

  int optimal_bri = int( 10 + bri / 1.04);

  float Y = y;
  float X = x;
  float Z = 1.0f - x - y;

  // sRGB D65 conversion
  float r =  X * 3.2406f - Y * 1.5372f - Z * 0.4986f;
  float g = -X * 0.9689f + Y * 1.8758f + Z * 0.0415f;
  float b =  X * 0.0557f - Y * 0.2040f + Z * 1.0570f;

  // Apply gamma correction
  r = r <= 0.0031308f ? 12.92f * r : (1.0f + 0.055f) * pow(r, (1.0f / 2.4f)) - 0.055f;
  g = g <= 0.0031308f ? 12.92f * g : (1.0f + 0.055f) * pow(g, (1.0f / 2.4f)) - 0.055f;
  b = b <= 0.0031308f ? 12.92f * b : (1.0f + 0.055f) * pow(b, (1.0f / 2.4f)) - 0.055f;

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

  colors[0] = (int) (r * optimal_bri); colors[1] = (int) (g * optimal_bri); colors[2] = (int) (b * optimal_bri); colors[3] = 0;
}

/**
 * Last change by: YannikW, 03.10.18
 * 
 * This converts a CT to a mix of a white led with a color temperature defines in WHITE_TEMP,
 * plus RGB shades to achieve full white spectrum.
 * CT value is in mired: https://en.wikipedia.org/wiki/Mired
 * Range is between 153 (equals 6536K cold-white) and 500 (equals 2000K warm-white)
 * 
 * To shift the white led to warmer or colder white shades we mix a "RGB-white" to the white led.
 * This RGB white is calculated as in old convert_ct methode, with formulars by: http://www.tannerhelland.com/4435/convert-temperature-rgb-algorithm-code/
 * If the desired CT equals the white channel CT, we add 0% RGB-white, the more we shift away we add more RGB-white. 
 * At the lower or higher end we add 100% RGB-white and reduce the led-white down to 50%
 */
void convert_ct() {
  int optimal_bri = int( 10 + bri / 1.04);
  int hectemp = 10000 / ct;
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


  // calculate mix factor
  double mixFactor;
  int temp = hectemp*100;
  if(temp >= WHITE_TEMP) {
    // mix cold-rgb-white to led-white
    mixFactor = (double)(temp-WHITE_TEMP) / (6536.0-WHITE_TEMP);  //0.0 - 1.0
  }
  else {
    // mix warm-rgb-white to led-white
    mixFactor = (double)(WHITE_TEMP-temp) / (WHITE_TEMP-2000.0);  //0.0 - 1.0
  }
  // constrain to 0-1
  mixFactor = mixFactor > 1.0 ? 1.0 : mixFactor;
  mixFactor = mixFactor < 0.0 ? 0.0 : mixFactor;
  
  colors[0] = r * (optimal_bri / 255.0f) * mixFactor; 
  colors[1] = g * (optimal_bri / 255.0f) * mixFactor; 
  colors[2] = b * (optimal_bri / 255.0f) * mixFactor;
  
  // reduce white brightness by 50% on maximum mixFactor 
  colors[3] = optimal_bri * (1.0-(mixFactor*0.5));
}

void apply_scene(uint8_t new_scene) {
  if ( new_scene == 1) {
    bri = 254; ct = 346; colorMode = 2; convert_ct();
  } else if ( new_scene == 2) {
    bri = 254; ct = 233; colorMode = 2; convert_ct();
  }  else if ( new_scene == 3) {
    bri = 254; ct = 156; colorMode = 2; convert_ct();
  }  else if ( new_scene == 4) {
    bri = 77; ct = 367; colorMode = 2; convert_ct();
  }  else if ( new_scene == 5) {
    bri = 254; ct = 447; colorMode = 2; convert_ct();
  }  else if ( new_scene == 6) {
    bri = 1; x = 0.561; y = 0.4042; colorMode = 1; convert_xy();
  }  else if ( new_scene == 7) {
    bri = 203; x = 0.380328; y = 0.39986; colorMode = 1; convert_xy();
  }  else if ( new_scene == 8) {
    bri = 112; x = 0.359168; y = 0.28807; colorMode = 1; convert_xy();
  }  else if ( new_scene == 9) {
    bri = 142; x = 0.267102; y = 0.23755; colorMode = 1; convert_xy();
  }  else if ( new_scene == 10) {
    bri = 216; x = 0.393209; y = 0.29961; colorMode = 1; convert_xy();
  }  else {
    bri = 144; ct = 447; colorMode = 2; convert_ct();
  }
}

void processLightdata(float transitiontime) {
  if (colorMode == 1 && lightState == true) {
    convert_xy();
  } else if (colorMode == 2 && lightState == true) {
    convert_ct();
  } else if (colorMode == 3 && lightState == true) {
    convert_hue();
  }
  transitiontime *= 16;
  for (uint8_t color = 0; color < PWM_CHANNELS; color++) {
    if (lightState) {
      stepLevel[color] = (colors[color] - currentColors[color]) / transitiontime;
    } else {
      stepLevel[color] = currentColors[color] / transitiontime;
    }
  }
}

void lightEngine() {
  for (uint8_t color = 0; color < PWM_CHANNELS; color++) {
    if (lightState) {
      if (colors[color] != currentColors[color] ) {
        inTransition = true;
        currentColors[color] += stepLevel[color];
        if ((stepLevel[color] > 0.0f && currentColors[color] > colors[color]) || (stepLevel[color] < 0.0f && currentColors[color] < colors[color])) currentColors[color] = colors[color];
        analogWrite(pins[color], (int)(currentColors[color] * 4.0));
      }
    } else {
      if (currentColors[color] != 0) {
        inTransition = true;
        currentColors[color] -= stepLevel[color];
        if (currentColors[color] < 0.0f) currentColors[color] = 0;
        analogWrite(pins[color], (int)(currentColors[color] * 4.0));
      }
    }
  }
  if (inTransition) {
    delay(6);
    inTransition = false;
  } else if (hwSwitch == true) {
    if (digitalRead(onPin) == HIGH) {
      int i = 0;
      while (digitalRead(onPin) == HIGH && i < 30) {
        delay(20);
        i++;
      }
      if (i < 30) {
        // there was a short press
        lightState = true;
      }
      else {
        // there was a long press
        bri += 56;
        if (bri > 254) {
          // don't increase the brightness more then maximum value
          bri = 254;
        }
      }
      processLightdata(4);
    } else if (digitalRead(offPin) == HIGH) {
      int i = 0;
      while (digitalRead(offPin) == HIGH && i < 30) {
        delay(20);
        i++;
      }
      if (i < 30) {
        // there was a short press
        lightState = false;
      }
      else {
        // there was a long press
        bri -= 56;
        if (bri < 1) {
          // don't decrease the brightness less than minimum value.
          bri = 1;
        }
      }
      processLightdata(4);
    }
  }
}


void saveState() {
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["r"] = colors[0];
  json["g"] = colors[1];
  json["b"] = colors[2];
  json["w"] = colors[3];
  json["on"] = lightState;

  File stateFile = SPIFFS.open("/state.json", "w");
  json.printTo(stateFile);
}


void restoreState() {
  File stateFile = SPIFFS.open("/state.json", "r");
  if (!stateFile) {
    saveState();
    return;
  }

  size_t size = stateFile.size();

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  stateFile.readBytes(buf.get(), size);

  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());

  if (!json.success()) {
    //Serial.println("Failed to parse config file");
    return;
  }
  colors[0] = (uint8_t) json["r"];
  colors[1] = (uint8_t) json["g"];
  colors[2] = (uint8_t) json["b"];
  colors[3] = (uint8_t) json["w"];
  lightState = json["on"];

}


bool saveConfig() {
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["name"] = lightName;
  json["startup"] = startup;
  json["scene"] = scene;
  json["r"] = pins[0];
  json["g"] = pins[1];
  json["b"] = pins[2];
  json["w"] = pins[3];
  json["on"] = onPin;
  json["off"] = offPin;
  json["hw"] = hwSwitch;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    //Serial.println("Failed to open config file for writing");
    return false;
  }

  json.printTo(configFile);
  return true;
}

bool loadConfig() {
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    //Serial.println("Create new file with default values");
    strcpy(lightName, "New Hue RGBW light");
    pins[0] = 12; pins[1] = 13; pins[2] = 14; pins[3] = 4; onPin = 1; offPin = 3; hwSwitch = false;
    return saveConfig();
    delay(100);
    ESP.reset();
  }

  size_t size = configFile.size();
  if (size > 1024) {
    //Serial.println("Config file size is too large");
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);

  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());

  if (!json.success()) {
    //Serial.println("Failed to parse config file");
    return false;
  }

  strcpy(lightName, json["name"]);
  startup = (uint8_t) json["startup"];
  scene  = (uint8_t) json["scene"];
  pins[0] = (uint8_t) json["r"];
  pins[1] = (uint8_t) json["g"];
  pins[2] = (uint8_t) json["b"];
  pins[3] = (uint8_t) json["w"];
  onPin = (uint8_t) json["on"];
  offPin = (uint8_t) json["off"];
  hwSwitch = json["hw"];
  return true;
}

void handleNotFound() {
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

void setup() {
  //Serial.begin(115200);
  //Serial.println();
  delay(1000);

  //Serial.println("mounting FS...");

  if (!SPIFFS.begin()) {
    //Serial.println("Failed to mount file system");
    return;
  }

  if (!loadConfig()) {
    //Serial.println("Failed to load config");
  } else {
    ////Serial.println("Config loaded");
  }

  for (uint8_t pin = 0; pin < PWM_CHANNELS; pin++) {
    pinMode(pins[pin], OUTPUT);
    analogWrite(pins[pin], 0);
  }

#ifdef USE_STATIC_IP
  WiFi.config(strip_ip, gateway_ip, subnet_mask);
#endif


  if (startup == 0) {
    restoreState();
    processLightdata(4);
  } else {
    apply_scene(scene);
    processLightdata(4);
  }
  if (startup == 1) {
    lightState = true;
  }
  if (lightState) {
    for (uint8_t i = 0; i < 200; i++) {
      lightEngine();
    }
  }
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.autoConnect(lightName);

  if (! lightState)  {
    // Show that we are connected
    analogWrite(pins[1], 100);
    delay(500);
    analogWrite(pins[1], 0);
  }
  WiFi.macAddress(mac);

  httpUpdateServer.setup(&server);

  Udp.begin(2100);

  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off by making the voltage HIGH
  if (hwSwitch == true) {
    pinMode(onPin, INPUT);
    pinMode(offPin, INPUT);
  }


  server.on("/state", []() {
    DynamicJsonBuffer newBuffer;
    JsonObject& root = newBuffer.parseObject(server.arg("plain"));
    if (!root.success()) {
      server.send(404, "text/plain", "FAIL. " + server.arg("plain"));
    } else {
      float transitiontime = 4.0;

      if (root.containsKey("xy")) {
        x = root["xy"][0];
        y = root["xy"][1];
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
        } else {
          lightState = false;
        }
        if (startup == 0) {
          saveState();
        }
      }

      if (root.containsKey("bri")) {
        bri = root["bri"];
      }

      if (root.containsKey("bri_inc")) {
        bri += (int) root["bri_inc"];
        if (bri > 255) bri = 255;
        else if (bri < 1) bri = 1;
      }

      if (root.containsKey("transitiontime")) {
        transitiontime = root["transitiontime"];
      }

      if (root.containsKey("alert") && root["alert"] == "select") {
        if (lightState) {
          currentColors[0] = 0; currentColors[1] = 0; currentColors[2] = 0; currentColors[3] = 0;
        } else {
          currentColors[0] = 255; currentColors[1] = 255; currentColors[2] = 255; currentColors[3] = 255;
        }
      }
      String output;
      root.printTo(output);
      server.send(200, "text/plain", output);
      processLightdata(transitiontime);
    }
  });

  server.on("/get", []() {
    DynamicJsonBuffer newBuffer;
    JsonObject& root = newBuffer.createObject();

    root["on"] = lightState;
    root["bri"] = bri;
    JsonArray& xy = root.createNestedArray("xy");
    xy.add(x);
    xy.add(y);
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
    root.printTo(output);
    server.send(200, "text/plain", output);
  });

  server.on("/detect", []() {
    char macString[32] = {0};
    sprintf(macString,"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    DynamicJsonBuffer newBuffer;
    JsonObject& root = newBuffer.createObject();
    root["name"] = lightName;
    root["protocol"] = "native_single";
    root["modelid"] = "LCT015";
    root["type"] = "rgbw";
    root["mac"] = String(macString);
    root["version"] = 2.0;
    String output;
    root.printTo(output);
    server.send(200, "text/plain", output);
  });

  server.on("/config", []() {
    DynamicJsonBuffer newBuffer;
    JsonObject& root = newBuffer.createObject();
    root["name"] = lightName;
    root["scene"] = scene;
    root["startup"] = startup;
    root["red"] = pins[0];
    root["green"] = pins[1];
    root["blue"] = pins[2];
    root["white"] = pins[3];
    root["hw"] = hwSwitch;
    root["on"] = onPin;
    root["off"] = offPin;
    root["hwswitch"] = (int)hwSwitch;
    String output;
    root.printTo(output);
    server.send(200, "text/plain", output);
  });

  server.on("/", []() {
    if (server.args()) {
      server.arg("name").toCharArray(lightName, server.arg("name").length() + 1);
      startup = server.arg("startup").toInt();
      scene = server.arg("scene").toInt();
      pins[0] = server.arg("red").toInt();
      pins[1] = server.arg("green").toInt();
      pins[2] = server.arg("blue").toInt();
      pins[3] = server.arg("white").toInt();
      hwSwitch = server.arg("hwswitch").toInt();
      onPin = server.arg("on").toInt();
      offPin = server.arg("off").toInt();
      saveConfig();
    }

    const char * htmlContent = "<!DOCTYPE html><html><head> <meta charset=\"UTF-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"> <title>Hue Light</title> <link rel=\"stylesheet\" href=\"https://stackpath.bootstrapcdn.com/bootstrap/4.2.1/css/bootstrap.min.css\"> <link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/ion-rangeslider/2.3.0/css/ion.rangeSlider.min.css\" /> <script src=\"https://code.jquery.com/jquery-3.3.1.min.js\"></script> <script src=\"https://stackpath.bootstrapcdn.com/bootstrap/4.2.1/js/bootstrap.min.js\"></script> <script src=\"https://cdnjs.cloudflare.com/ajax/libs/ion-rangeslider/2.3.0/js/ion.rangeSlider.min.js\"></script></head><body> <nav class=\"navbar navbar-expand-lg navbar-light bg-light rounded\"> <button class=\"navbar-toggler\" type=\"button\" data-toggle=\"collapse\" data-target=\"#navbarToggler\" aria-controls=\"navbarToggler\" aria-expanded=\"false\" aria-label=\"Toggle navigation\"> <span class=\"navbar-toggler-icon\"></span> </button> <h2></h2> <div class=\"collapse navbar-collapse justify-content-md-center\" id=\"navbarToggler\"> <ul class=\"nav nav-pills\"> <li class=\"nav-item\"> <a class=\"nav-link active\" data-toggle=\"pill\" href=\"#home\">Home</a> </li> <li class=\"nav-item\"> <a class=\"nav-link\" data-toggle=\"pill\" href=\"#menu1\">Settings</a> </li> <li class=\"nav-item\"> <a class=\"nav-link\" data-toggle=\"pill\" href=\"#\" disabled> </a> </li> <li class=\"nav-item\"> <a class=\"nav-link\" data-toggle=\"pill\" href=\"#\" disabled> </a> </li> </ul> </div> </nav> <!-- Tab panes --> <div class=\"tab-content\"> <div class=\"tab-pane container active\" id=\"home\"> <br><br> <form> <div class=\"form-group row\"> <label for=\"power\" class=\"col-sm-2 col-form-label\">Power</label> <div class=\"col-sm-10\"> <div id=\"power\" class=\"btn-group\" role=\"group\"> <button type=\"button\" class=\"btn btn-default border\" id=\"power-on\">On</button> <button type=\"button\" class=\"btn btn-default border\" id=\"power-off\">Off</button> </div> </div> </div> <div class=\"form-group row\"> <label for=\"bri\" class=\"col-sm-2 col-form-label\">Brightness</label> <div class=\"col-sm-10\"> <input type=\"text\" id=\"bri\" class=\"js-range-slider\" name=\"bri\" value=\"\" /> </div> </div> <div class=\"form-group row\"> <label for=\"hue\" class=\"col-sm-2 col-form-label\">Color</label> <div class=\"col-sm-10\"> <canvas id=\"hue\" width=\"320px\" height=\"320px\" style=\"border:1px solid #d3d3d3;\"></canvas> </div> </div> <div class=\"form-group row\"> <label for=\"color\" class=\"col-sm-2 col-form-label\">Color Temp</label> <div class=\"col-sm-10\"> <canvas id=\"ct\" width=\"320px\" height=\"50px\" style=\"border:1px solid #d3d3d3;\"></canvas> </div> </div> </form> </div> <div class=\"tab-pane container fade\" id=\"menu1\"> <br> <form method=\"POST\" action=\"/\"> <div class=\"form-group row\"> <label for=\"name\" class=\"col-sm-2 col-form-label\">Light Name</label> <div class=\"col-sm-6\"> <input type=\"text\" class=\"form-control\" id=\"name\" name=\"name\"> </div> </div> <div class=\"form-group row\"> <label class=\"control-label col-sm-2\" for=\"startup\">Default Power:</label> <div class=\"col-sm-4\"> <select class=\"form-control\" name=\"startup\" id=\"startup\"> <option value=\"0\">Last State</option> <option value=\"1\">On</option> <option value=\"2\">Off</option> </select> </div> </div> <div class=\"form-group row\"> <label class=\"control-label col-sm-2\" for=\"scene\">Default Scene:</label> <div class=\"col-sm-4\"> <select class=\"form-control\" name=\"scene\" id=\"scene\"> <<option value=\"0\">Relax</option> <option value=\"1\">Read</option> <option value=\"2\">Concentrate</option> <option value=\"3\">Energize</option> <option value=\"4\">Bright</option> <option value=\"5\">Dimmed</option> <option value=\"6\">Nightlight</option> <option value=\"7\">Savanna sunset</option> <option value=\"8\">Tropical twilight</option> <option value=\"9\">Arctic aurora</option> <option value=\"10\">Spring blossom</option> </select> </div> </div> <div class=\"form-group row\"> <label for=\"red\" class=\"col-sm-2 col-form-label\">Red Pin</label> <div class=\"col-sm-3\"> <input type=\"number\" class=\"form-control\" id=\"red\" name=\"red\" placeholder=\"\"> </div> </div> <div class=\"form-group row\"> <label for=\"green\" class=\"col-sm-2 col-form-label\">Green Pin</label> <div class=\"col-sm-3\"> <input type=\"number\" class=\"form-control\" id=\"green\" name=\"green\" placeholder=\"\"> </div> </div> <div class=\"form-group row\"> <label for=\"blue\" class=\"col-sm-2 col-form-label\">Blue Pin</label> <div class=\"col-sm-3\"> <input type=\"number\" class=\"form-control\" id=\"blue\" name=\"blue\" placeholder=\"\"> </div> </div> <div class=\"form-group row\"> <label for=\"white\" class=\"col-sm-2 col-form-label\">White Pin</label> <div class=\"col-sm-3\"> <input type=\"number\" class=\"form-control\" id=\"white\" name=\"white\" placeholder=\"\"> </div> </div> <div class=\"form-group row\"> <label class=\"control-label col-sm-2\" for=\"hwswitch\">HW Switch:</label> <div class=\"col-sm-2\"> <select class=\"form-control\" name=\"hwswitch\" id=\"hwswitch\"> <option value=\"1\">Yes</option> <option value=\"0\">No</option> </select> </div> </div> <div class=\"form-group row\"> <label for=\"on\" class=\"col-sm-2 col-form-label\">On Pin</label> <div class=\"col-sm-3\"> <input type=\"number\" class=\"form-control\" id=\"on\" name=\"on\" placeholder=\"\"> </div> </div> <div class=\"form-group row\"> <label for=\"off\" class=\"col-sm-2 col-form-label\">Off Pin</label> <div class=\"col-sm-3\"> <input type=\"number\" class=\"form-control\" id=\"off\" name=\"off\" placeholder=\"\"> </div> </div> <div class=\"form-group row\"> <div class=\"col-sm-10\"> <button type=\"submit\" class=\"btn btn-primary\">Save</button> </div> </div> </form> </div> </div> <script src=\"https://diyhue.org/cdn/color.js\"></script></body></html>";
    server.send(200, "text/html", htmlContent);
  });

  server.on("/reset", []() {
    server.send(200, "text/html", "reset");
    ESP.reset();
  });

  server.onNotFound(handleNotFound);

  server.begin();
}

void entertainment() {
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    Udp.read(packetBuffer, packetSize);
    for (uint8_t color = 0; color < 3; color++) {
      analogWrite(pins[color - 1], (int)(packetBuffer[color] * 4));
    }
  }
}

void loop() {
  server.handleClient();
  lightEngine();
  entertainment();
}
