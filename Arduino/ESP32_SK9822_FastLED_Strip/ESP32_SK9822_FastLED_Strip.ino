#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <MQTT.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#include <FastLED.h>
#include <Arduino.h>

// physical length of led-strip
#define NUM_LEDS 277

// FastLED settings, data and clock pin for spi communication
// Note that the protocol for SM16716 is the same for the SM16726
#define DATA_PIN 22
#define CLOCK_PIN 19
#define COLOR_ORDER BGR
#define LED_TYPE SK9822
#define CORRECTION TypicalSMD5050

// Use Correction from fastLED library or not
#define USE_F_LED_CC true
 
#define LIGHT_VERSION 3.1
#define ENTERTAINMENT_TIMEOUT 1500    // millis
#define RGB_R 100      // light multiplier in percentage /R, G, B/
#define RGB_G 100
#define RGB_B 100
#define LEDS_NUM 277  //physical number of leds in the stripe

// May get messed up with SPI CLOCK_PIN with this particular bulb
#define use_hardware_switch false // To control on/off state and brightness using GPIO/Pushbutton, set this value to true.
//For GPIO based on/off and brightness control, it is mandatory to connect the following GPIO pins to ground using 10k resistor
#define onPin 4 // on and brightness up
#define offPin 5 // off and brightness down

// !!!!!!!! Experimental !!!!!!!!!!
// True - add cold white LEDs according to luminance/ whiteness in xy color selector
// False - Don't
#define W_ON_XY true

// Set up array for use by FastLED
CRGBArray<NUM_LEDS> leds;

// define details of virtual hue-lights -- adapt to your needs!
String HUE_Name = "Hue SK9822 FastLED strip";   //max 32 characters!!!
int HUE_LightsCount = 4;                     //number of emulated hue-lights
int HUE_PixelPerLight = 57;                  //number of leds forming one emulated hue-light
int HUE_TransitionLeds = 12;                 //number of 'space'-leds inbetween the emulated hue-lights; pixelCount must be divisible by this value
String HUE_UserName = "Z0lt8pVCK4YqzwzO8U3HzzxNy1-oxyvL7jAb5YwF";     //hue-username (needs to be configured in the diyHue-bridge
int HUE_FirstHueLightNr = 18;                //first free number for the first hue-light (look in diyHue config.json)
int HUE_ColorCorrectionRGB[3] = {100, 100, 100};  // light multiplier in percentage /R, G, B/

IPAddress address ( 192,  168,   2,  95);     // choose an unique IP Adress
IPAddress gateway ( 192,  168,   2,   1);     // Router IP
IPAddress submask(255, 255, 255,   0);
byte mac[6]; // to hold  the wifi mac address

const char* ConfKeys_Zero = "0";              //no idea what's the purpose...!!??
const char* ConfKeys_Startup = "startup";
const char* ConfKeys_Scene = "scene";

uint8_t scene;
uint8_t startup;
bool inTransition;
bool useDhcp = true;
bool hwSwitch = false;

WebServer websrv(80);
Preferences Conf;
MQTTClient MQTT(1024);
WiFiClient net;

HueApi objHue = HueApi(leds, mac, HUE_Name, HUE_LightsCount, HUE_PixelPerLight, HUE_TransitionLeds, HUE_UserName, HUE_FirstHueLightNr);
  
void setup() {

  Serial.begin(115200);
  Serial.println();
  delay(1000);
  
  Conf.begin("HueLED", false);

  if (USE_F_LED_CC == true) {
    FastLED.addLeds<LED_TYPE, DATA_PIN, CLOCK_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( CORRECTION );
  } else {
    FastLED.addLeds<LED_TYPE, DATA_PIN, CLOCK_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  }
   
  //loadConfig();
  //restoreState();
  //apply_scene(scene);

  WiFiManager wifiManager;
  if (!useDhcp) {
    wifiManager.setSTAStaticIPConfig(address, gateway, submask);
  }
  wifiManager.autoConnect("New Hue Light");

  if (useDhcp) {
    address = WiFi.localIP();
    gateway = WiFi.gatewayIP();
    submask = WiFi.subnetMask();
  }

  infoLight(CRGB::White);
  while (WiFi.status() != WL_CONNECTED) {
    infoLight(CRGB::Red);
    delay(500);
  }
  // Show that we are connected
  infoLight(CRGB::Green);

  WiFi.macAddress(mac);         //gets the mac-address

  // ArduinoOTA.setPort(8266);                      // Port defaults to 8266
  // ArduinoOTA.setHostname("myesp8266");           // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setPassword((const char *)"123");   // No authentication by default
  ArduinoOTA.begin();

  if (use_hardware_switch == true) {
    pinMode(onPin, INPUT);
    pinMode(offPin, INPUT);
  }

  ConnectMQTT();
  
  websrv.on("/state", HTTP_PUT, websrvStatePut); 
  websrv.on("/state", HTTP_GET, websrvStateGet);
  websrv.on("/detect", websrvDetect);
  
  websrv.on("/config", websrvConfig);
  websrv.on("/", websrvRoot);
  websrv.on("/reset", websrvReset);
  websrv.onNotFound(websrvNotFound);

  websrv.on("/text_sensor/light_id", []() {
    websrv.send(200, "text/plain", "1");
  });
  
  websrv.begin();

  Log("Up and running.");
}

void loop() {
  ArduinoOTA.handle();
  websrv.handleClient();
  objHue.lightEngine();
  FastLED.show();

  EVERY_N_MILLISECONDS(200) {
    ArduinoOTA.handle();
    MQTT.loop();
  }
}


void ConnectMQTT(){

  Log("Connecting to MQTT-Broker: ");
  
  MQTT.begin("192.168.2.3", 1883, net);
  MQTT.setWill("iot/ledcontroller/log", "Off");
  
  while (!MQTT.connect("ledcontroller", "", "")) {
    Serial.print(".");
    delay(500);
  }

//  MQTT.onMessage(messageReceived);
  
  MQTT.subscribe("iot/ledcontroller/log");
/*  MQTT.subscribe(Conf.getString("ctrl.cmdtopic"));
  MQTT.subscribe(Conf.getString("ctrl.pcttopic"));
  MQTT.subscribe(Conf.getString("ctrl.rgbtopic"));
  MQTT.subscribe(Conf.getString("ctrl.scenetopic"));
  MQTT.subscribe(Conf.getString("ctrl.cfgtopic"));
*/
  Log("MQTT connected.\r\n");
}

void Log(String msg) {

  Serial.println(msg);

  if (MQTT.connected()) {
    MQTT.publish("iot/ledcontroller/log", msg);
  }
}

String WebLog(String message) {
  
  message += "URI: " + websrv.uri();
  message += "\r\n Method: " + (websrv.method() == HTTP_GET) ? "GET" : "POST";
  message += "\r\n Arguments: " + websrv.args(); + "\r\n";
  for (uint8_t i = 0; i < websrv.args(); i++) {
    message += " " + websrv.argName(i) + ": " + websrv.arg(i) + " \r\n";
  }
  
  Log(message);
  return message;
}

void infoLight(CRGB color) {
  
  // Flash the strip in the selected color. White = booted, green = WLAN connected, red = WLAN could not connect
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = color;
    FastLED.show();
    leds.fadeToBlackBy(10);
  }
  leds = CRGB(CRGB::Black);
  FastLED.show();
}

void saveState() {
  
  String Output;
  DynamicJsonDocument json(1024);
  JsonObject light;
  
  for (uint8_t i = 0; i < HUE_LightsCount; i++) {
    light = json.createNestedObject((String)i);
    light["on"] = objHue.lights[i].lightState;
    light["bri"] = objHue.lights[i].bri;
    if (objHue.lights[i].colorMode == 1) {
      light["x"] = objHue.lights[i].x;
      light["y"] = objHue.lights[i].y;
    } else if (objHue.lights[i].colorMode == 2) {
      light["ct"] = objHue.lights[i].ct;
    } else if (objHue.lights[i].colorMode == 3) {
      light["hue"] = objHue.lights[i].hue;
      light["sat"] = objHue.lights[i].sat;
    }
  }
  
  serializeJson(json, Output);
  Conf.putString("StateJson", Output);
}

void restoreState() {
  
  String Input;
  DynamicJsonDocument json(1024);
  DeserializationError error;
  JsonObject values;
  const char* key;
  int lightId;
  
  Conf.getString("StateJson", Input);

  error = deserializeJson(json, Input);
  if (error) {
    //Serial.println("Failed to parse config file");
    return;
  }
  for (JsonPair state : json.as<JsonObject>()) {
    key = state.key().c_str();
    lightId = atoi(key);
    values = state.value();
    objHue.lights[lightId].lightState = values["on"];
    objHue.lights[lightId].bri = (uint8_t)values["bri"];
    if (values.containsKey("x")) {
      objHue.lights[lightId].x = values["x"];
      objHue.lights[lightId].y = values["y"];
      objHue.lights[lightId].colorMode = 1;
    } else if (values.containsKey("ct")) {
      objHue.lights[lightId].ct = values["ct"];
      objHue.lights[lightId].colorMode = 2;
    } else {
      if (values.containsKey("hue")) {
        objHue.lights[lightId].hue = values["hue"];
        objHue.lights[lightId].colorMode = 3;
      }
      if (values.containsKey("sat")) {
        objHue.lights[lightId].sat = (uint8_t) values["sat"];
        objHue.lights[lightId].colorMode = 3;
      }
      objHue.lights[lightId].color = CHSV(objHue.lights[lightId].hue, objHue.lights[lightId].sat, objHue.lights[lightId].bri);
    }
    objHue.processLightdata(lightId, 40);
  }
}

void saveConfig() {
  
  String Output;
  DynamicJsonDocument json(1024);
  JsonArray addr, gw, mask;
  
  json["name"] = HUE_Name;
  json["startup"] = startup;
  json["scene"] = scene;
  json["on"] = onPin;
  json["off"] = offPin;
  json["hw"] = hwSwitch;
  json["dhcp"] = useDhcp;
  json["lightsCount"] = HUE_LightsCount;
  json["pixelCount"] = HUE_PixelPerLight;
  json["transLeds"] = HUE_TransitionLeds;
  json["rpct"] = HUE_ColorCorrectionRGB[0];
  json["gpct"] = HUE_ColorCorrectionRGB[1];
  json["bpct"] = HUE_ColorCorrectionRGB[2];
  addr = json.createNestedArray("addr");
  addr.add(address[0]);
  addr.add(address[1]);
  addr.add(address[2]);
  addr.add(address[3]);
  gw = json.createNestedArray("gw");
  gw.add(gateway[0]);
  gw.add(gateway[1]);
  gw.add(gateway[2]);
  gw.add(gateway[3]);
  mask = json.createNestedArray("mask");
  mask.add(submask[0]);
  mask.add(submask[1]);
  mask.add(submask[2]);
  mask.add(submask[3]);

  serializeJson(json, Output);
  Conf.putString("ConfJson", Output);
}

bool loadConfig() {
  
  String Input;
  DynamicJsonDocument json(1024);
  DeserializationError error;
  
  Conf.getString("ConfJson", Input);
  
  error = deserializeJson(json, Input);
  if (error) {
    //Serial.println("Failed to parse config file");
    return false;
  }

  HUE_Name = json["name"].as<String>();
  startup = (uint8_t) json["startup"];
  scene  = (uint8_t) json["scene"];
  HUE_LightsCount = (uint16_t) json["lightsCount"];
  HUE_PixelPerLight = (uint16_t) json["pixelCount"];
  HUE_TransitionLeds = (uint8_t) json["transLeds"];
  if (json.containsKey("rpct")) {
    HUE_ColorCorrectionRGB[0] = (uint8_t) json["rpct"];
    HUE_ColorCorrectionRGB[1] = (uint8_t) json["gpct"];
    HUE_ColorCorrectionRGB[2] = (uint8_t) json["bpct"];
  }
  useDhcp = json["dhcp"];
  address = {json["addr"][0], json["addr"][1], json["addr"][2], json["addr"][3]};
  submask = {json["mask"][0], json["mask"][1], json["mask"][2], json["mask"][3]};
  gateway = {json["gw"][0], json["gw"][1], json["gw"][2], json["gw"][3]};
  return true;
}

void websrvDetect() {
  String output = objHue.Detect();
  websrv.send(200, "text/plain", output);    
  Log(output);
}

void websrvStateGet() {
  String output = objHue.StateGet(websrv.arg("light"));
  websrv.send(200, "text/plain", output);
}

void websrvStatePut() {
  String output = objHue.StatePut(websrv.arg("plain"));
  if (output.substring(0, 4) == "FAIL") {
    websrv.send(404, "text/plain", "FAIL. " + websrv.arg("plain"));
  }
  websrv.send(200, "text/plain", output);
}

void websrvReset() {
  websrv.send(200, "text/html", "reset");
  delay(1000);
  esp_restart();
}

void websrvRoot() {

  Log("StateRoot: " + websrv.uri());
  
  if (websrv.arg("section").toInt() == 1) {
    HUE_Name = websrv.arg("name");
    startup = websrv.arg("startup").toInt();
    scene = websrv.arg("scene").toInt();
    HUE_LightsCount = websrv.arg("lightscount").toInt();
    HUE_PixelPerLight = websrv.arg("pixelcount").toInt();
    HUE_TransitionLeds = websrv.arg("transitionleds").toInt();
    HUE_ColorCorrectionRGB[0] = websrv.arg("rpct").toInt();
    HUE_ColorCorrectionRGB[1] = websrv.arg("gpct").toInt();
    HUE_ColorCorrectionRGB[2] = websrv.arg("bpct").toInt();
    hwSwitch = websrv.hasArg("hwswitch") ? websrv.arg("hwswitch").toInt() : 0;
    
    saveConfig();
  } else if (websrv.arg("section").toInt() == 2) {
    useDhcp = (!websrv.hasArg("disdhcp")) ? 1 : websrv.arg("disdhcp").toInt();
    if (websrv.hasArg("disdhcp")) {
      address.fromString(websrv.arg("addr"));
      gateway.fromString(websrv.arg("gw"));
      submask.fromString(websrv.arg("sm"));
    }
    saveConfig();
  }

  String htmlContent = "<!DOCTYPE html> <html> <head> <meta charset=\"UTF-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"> <title>Hue Light</title> <link href=\"https://fonts.googleapis.com/icon?family=Material+Icons\" rel=\"stylesheet\"> <link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/css/materialize.min.css\"> <link rel=\"stylesheet\" href=\"https://cerny.in/nouislider.css\"/> </head> <body> <div class=\"wrapper\"> <nav class=\"nav-extended row deep-purple\"> <div class=\"nav-wrapper col s12\"> <a href=\"#\" class=\"brand-logo\">DiyHue</a> <ul id=\"nav-mobile\" class=\"right hide-on-med-and-down\" style=\"position: relative;z-index: 10;\"> <li><a target=\"_blank\" href=\"https://github.com/diyhue\"><i class=\"material-icons left\">language</i>GitHub</a></li> <li><a target=\"_blank\" href=\"https://diyhue.readthedocs.io/en/latest/\"><i class=\"material-icons left\">description</i>Documentation</a></li> <li><a target=\"_blank\" href=\"https://diyhue.slack.com/\" ><i class=\"material-icons left\">question_answer</i>Slack channel</a></li> </ul> </div> <div class=\"nav-content\"> <ul class=\"tabs tabs-transparent\"> <li class=\"tab\"><a class=\"active\" href=\"#test1\">Home</a></li> <li class=\"tab\"><a href=\"#test2\">Preferences</a></li> <li class=\"tab\"><a href=\"#test3\">Network settings</a></li> </ul> </div> </nav> <ul class=\"sidenav\" id=\"mobile-demo\"> <li><a target=\"_blank\" href=\"https://github.com/diyhue\">GitHub</a></li> <li><a target=\"_blank\" href=\"https://diyhue.readthedocs.io/en/latest/\">Documentation</a></li> <li><a target=\"_blank\" href=\"https://diyhue.slack.com/\" >Slack channel</a></li> </ul> <div class=\"container\"> <div class=\"section\"> <div id=\"test1\" class=\"col s12\"> <form> <input type=\"hidden\" name=\"section\" value=\"1\"> <div class=\"row\"> <div class=\"col s10\"> <label for=\"power\">Power</label> <div id=\"power\" class=\"switch section\"> <label> Off <input type=\"checkbox\" name=\"pow\" id=\"pow\" value=\"1\"> <span class=\"lever\"></span> On </label> </div> </div> </div> <div class=\"row\"> <div class=\"col s12 m10\"> <label for=\"bri\">Brightness</label> <input type=\"text\" id=\"bri\" class=\"js-range-slider\" name=\"bri\" value=\"\"/> </div> </div> <div class=\"row\"> <div class=\"col s12\"> <label for=\"hue\">Color</label> <div> <canvas id=\"hue\" width=\"320px\" height=\"320px\" style=\"border:1px solid #d3d3d3;\"></canvas> </div> </div> </div> <div class=\"row\"> <div class=\"col s12\"> <label for=\"ct\">Color Temp</label> <div> <canvas id=\"ct\" width=\"320px\" height=\"50px\" style=\"border:1px solid #d3d3d3;\"></canvas> </div> </div> </div> </form> </div> <div id=\"test2\" class=\"col s12\"> <form method=\"POST\" action=\"/\"> <input type=\"hidden\" name=\"section\" value=\"1\"> <div class=\"row\"> <div class=\"col s12\"> <label for=\"name\">Light Name</label> <input type=\"text\" id=\"name\" name=\"name\"> </div> </div> <div class=\"row\"> <div class=\"col s12 m6\"> <label for=\"startup\">Default Power:</label> <select name=\"startup\" id=\"startup\"> <option value=\"0\">Last State</option> <option value=\"1\">On</option> <option value=\"2\">Off</option> </select> </div> </div> <div class=\"row\"> <div class=\"col s12 m6\"> <label for=\"scene\">Default Scene:</label> <select name=\"scene\" id=\"scene\"> <option value=\"0\">Relax</option> <option value=\"1\">Read</option> <option value=\"2\">Concentrate</option> <option value=\"3\">Energize</option> <option value=\"4\">Bright</option> <option value=\"5\">Dimmed</option> <option value=\"6\">Nightlight</option> <option value=\"7\">Savanna sunset</option> <option value=\"8\">Tropical twilight</option> <option value=\"9\">Arctic aurora</option> <option value=\"10\">Spring blossom</option> </select> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"pixelcount\" class=\"col-form-label\">Pixel count</label> <input type=\"number\" id=\"pixelcount\" name=\"pixelcount\"> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"lightscount\" class=\"col-form-label\">Lights count</label> <input type=\"number\" id=\"lightscount\" name=\"lightscount\"> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"transitionleds\">Transition leds:</label> <select name=\"transitionleds\" id=\"transitionleds\"> <option value=\"0\">0</option> <option value=\"2\">2</option> <option value=\"4\">4</option> <option value=\"6\">6</option> <option value=\"8\">8</option> <option value=\"10\">10</option> </select> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"rpct\" class=\"form-label\">Red multiplier</label> <input type=\"number\" id=\"rpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"rpct\" value=\"\"/> </div> <div class=\"col s4 m3\"> <label for=\"gpct\" class=\"form-label\">Green multiplier</label> <input type=\"number\" id=\"gpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"gpct\" value=\"\"/> </div> <div class=\"col s4 m3\"> <label for=\"bpct\" class=\"form-label\">Blue multiplier</label> <input type=\"number\" id=\"bpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"bpct\" value=\"\"/> </div> </div> <div class=\"row\"><label class=\"control-label col s10\">HW buttons:</label> <div class=\"col s10\"> <div class=\"switch section\"> <label> Disable <input type=\"checkbox\" name=\"hwswitch\" id=\"hwswitch\" value=\"1\"> <span class=\"lever\"></span> Enable </label> </div> </div> </div> <div class=\"switchable\"> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"on\">On Pin</label> <input type=\"number\" id=\"on\" name=\"on\"> </div> <div class=\"col s4 m3\"> <label for=\"off\">Off Pin</label> <input type=\"number\" id=\"off\" name=\"off\"> </div> </div> </div> <div class=\"row\"> <div class=\"col s10\"> <button type=\"submit\" class=\"waves-effect waves-light btn teal\">Save</button> <!--<button type=\"submit\" name=\"reboot\" class=\"waves-effect waves-light btn grey lighten-1\">Reboot</button>--> </div> </div> </form> </div> <div id=\"test3\" class=\"col s12\"> <form method=\"POST\" action=\"/\"> <input type=\"hidden\" name=\"section\" value=\"2\"> <div class=\"row\"> <div class=\"col s12\"> <label class=\"control-label\">Manual IP assignment:</label> <div class=\"switch section\"> <label> Disable <input type=\"checkbox\" name=\"disdhcp\" id=\"disdhcp\" value=\"0\"> <span class=\"lever\"></span> Enable </label> </div> </div> </div> <div class=\"switchable\"> <div class=\"row\"> <div class=\"col s12 m3\"> <label for=\"addr\">Ip</label> <input type=\"text\" id=\"addr\" name=\"addr\"> </div> <div class=\"col s12 m3\"> <label for=\"sm\">Submask</label> <input type=\"text\" id=\"sm\" name=\"sm\"> </div> <div class=\"col s12 m3\"> <label for=\"gw\">Gateway</label> <input type=\"text\" id=\"gw\" name=\"gw\"> </div> </div> </div> <div class=\"row\"> <div class=\"col s10\"> <button type=\"submit\" class=\"waves-effect waves-light btn teal\">Save</button> <!--<button type=\"submit\" name=\"reboot\" class=\"waves-effect waves-light btn grey lighten-1\">Reboot</button>--> <!--<button type=\"submit\" name=\"reboot\" class=\"waves-effect waves-light btn grey lighten-1\">Reboot</button>--> </div> </div> </form> </div> </div> </div> </div> <script src=\"https://cdnjs.cloudflare.com/ajax/libs/jquery/3.4.1/jquery.min.js\"></script> <script src=\"https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/js/materialize.min.js\"></script> <script src=\"https://cerny.in/nouislider.js\"></script> <script src=\"https://cerny.in/diyhue.js\"></script> </body> </html>";

  websrv.send(200, "text/html", htmlContent);
  if (websrv.args()) {
    delay(1000);    // needs to wait until response is received by browser. If ESP restarts too soon, browser will think there was an error.
    esp_restart();
  }
}

void websrvConfig() {
  
  DynamicJsonDocument root(1024);
  String output;
  HueApi::state tmpState;
  
  root["name"] = HUE_Name;
  root["scene"] = scene;
  root["startup"] = startup;
  root["hw"] = hwSwitch;
  root["on"] = onPin;
  root["off"] = offPin;
  root["hwswitch"] = (int)hwSwitch;
  root["lightscount"] = HUE_LightsCount;
  root["pixelcount"] = HUE_PixelPerLight;
  root["transitionleds"] = HUE_TransitionLeds;
  root["rpct"] = RGB_R;
  root["gpct"] = RGB_G;
  root["bpct"] = RGB_B;
  root["disdhcp"] = (int)!useDhcp;
  root["addr"] = (String)address[0] + "." + (String)address[1] + "." + (String)address[2] + "." + (String)address[3];
  root["gw"] = (String)gateway[0] + "." + (String)gateway[1] + "." + (String)gateway[2] + "." + (String)gateway[3];
  root["sm"] = (String)submask[0] + "." + (String)submask[1] + "." + (String)submask[2] + "." + (String)submask[3];
  
  serializeJson(root, output);
  websrv.send(200, "text/plain", output);
}

void websrvNotFound() {
  websrv.send(404, "text/plain", WebLog("File Not Found\n\n"));
}


/*void convert_hue() {
  double      hh, p, q, t, ff, s, v;
  long        i;


  s = sat / 255.0;
  v = bri / 255.0;

  // Test for intensity for white LEDs
  float I = (float)(sat + bri) / 2;

  if (s <= 0.0) {      // < is bogus, just shuts up warnings
    rgbw[0] = v;
    rgbw[1] = v;
    rgbw[2] = v;
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
      rgbw[0] = v * 255.0;
      rgbw[1] = t * 255.0;
      rgbw[2] = p * 255.0;
      rgbw[3] = I;
      break;
    case 1:
      rgbw[0] = q * 255.0;
      rgbw[1] = v * 255.0;
      rgbw[2] = p * 255.0;
      rgbw[3] = I;
      break;
    case 2:
      rgbw[0] = p * 255.0;
      rgbw[1] = v * 255.0;
      rgbw[2] = t * 255.0;
      rgbw[3] = I;
      break;

    case 3:
      rgbw[0] = p * 255.0;
      rgbw[1] = q * 255.0;
      rgbw[2] = v * 255.0;
      rgbw[3] = I;
      break;
    case 4:
      rgbw[0] = t * 255.0;
      rgbw[1] = p * 255.0;
      rgbw[2] = v * 255.0;
      rgbw[3] = I;
      break;
    case 5:
    default:
      rgbw[0] = v * 255.0;
      rgbw[1] = p * 255.0;
      rgbw[2] = q * 255.0;
      rgbw[3] = I;
      break;
  }

} */


class HueApi {
  
  private:
    int transitiontime;
    bool inTransition;
    float maxDist;

    void convRgbToXy(int r, int g, int b, uint8_t bri, float X, float Y) { 
      float x, y, z;
      
      r = (r > 0.04045) ? pow((r + 0.055) / 1.055, 2.4) : r / 12.92;
      g = (g > 0.04045) ? pow((g + 0.055) / 1.055, 2.4) : g / 12.92;
      b = (b > 0.04045) ? pow((b + 0.055) / 1.055, 2.4) : b / 12.92;
    
      x = (r * 0.4124 + g * 0.3576 + b * 0.1805) / 0.95047;
      y = (r * 0.2126 + g * 0.7152 + b * 0.0722) / 1.00000;
      z = (r * 0.0193 + g * 0.1192 + b * 0.9505) / 1.08883;
    
      x = (x > 0.008856) ? pow(x, 1/3) : (7.787 * x) + 16/116;
      y = (y > 0.008856) ? pow(y, 1/3) : (7.787 * y) + 16/116;
      z = (z > 0.008856) ? pow(z, 1/3) : (7.787 * z) + 16/116;
    
      bri = (uint8_t) (116 * y) - 16;
      X = 500 * (x - y);
      Y = 200 * (y - z);
    }
        
    CRGB convXyToRgb(uint8_t bri, float x, float y) {
    /*  
      x = 0.95047 * ((x * x * x > 0.008856) ? x * x * x : (x - 16/116) / 7.787);
      y = 1.00000 * ((y * y * y > 0.008856) ? y * y * y : (y - 16/116) / 7.787);
      z = 1.08883 * ((z * z * z > 0.008856) ? z * z * z : (z - 16/116) / 7.787);
    
      r = x *  3.2406 + y * -1.5372 + z * -0.4986;
      g = x * -0.9689 + y *  1.8758 + z *  0.0415;
      b = x *  0.0557 + y * -0.2040 + z *  1.0570;
    
      r = (r > 0.0031308) ? (1.055 * Math.pow(r, 1/2.4) - 0.055) : 12.92 * r;
      g = (g > 0.0031308) ? (1.055 * Math.pow(g, 1/2.4) - 0.055) : 12.92 * g;
      b = (b > 0.0031308) ? (1.055 * Math.pow(b, 1/2.4) - 0.055) : 12.92 * b;
    
      return [Math.max(0, Math.min(1, r)) * 255, 
              Math.max(0, Math.min(1, g)) * 255, 
              Math.max(0, Math.min(1, b)) * 255]
     */
      int optimal_bri;
      float X, Y, Z, r, g, b, maxv;
      
      Y = y; X = x; Z = 1.0f - x - y;
    
      if (bri < 5)  optimal_bri = 5;  else  optimal_bri = bri;
    
      // sRGB D65 conversion
      r =  X * 3.2406f - Y * 1.5372f - Z * 0.4986f;
      g = -X * 0.9689f + Y * 1.8758f + Z * 0.0415f;
      b =  X * 0.0557f - Y * 0.2040f + Z * 1.0570f;
    
      //  // Apply gamma correction v.2
      //  // Altering exponents at end can create different gamma curves
      //  r = r <= 0.04045f ? r / 12.92f : pow((r + 0.055f) / (1.0f + 0.055f), 2.4f);
      //  g = g <= 0.04045f ? g / 12.92f : pow((g + 0.055f) / (1.0f + 0.055f), 2.4f);
      //  b = b <= 0.04045f ? b / 12.92f : pow((b + 0.055f) / (1.0f + 0.055f), 2.4f);
    
      // Apply gamma correction v.1 (better color accuracy), try this first!
      r = r <= 0.0031308f ? 12.92f * r : (1.0f + 0.055f) * pow(r, (1.0f / 2.4f)) - 0.055f;
      g = g <= 0.0031308f ? 12.92f * g : (1.0f + 0.055f) * pow(g, (1.0f / 2.4f)) - 0.055f;
      b = b <= 0.0031308f ? 12.92f * b : (1.0f + 0.055f) * pow(b, (1.0f / 2.4f)) - 0.055f;
    
      maxv = 0;// calc the maximum value of r g and b
      if (r > maxv) maxv = r;
      if (g > maxv) maxv = g;
      if (b > maxv) maxv = b;
    
      if (maxv > 0) {// only if maximum value is greater than zero, otherwise there would be division by zero
        r /= maxv;   // scale to maximum so the brightest light is always 1.0
        g /= maxv;
        b /= maxv;
      }
    
      r = r < 0 ? 0 : r;
      g = g < 0 ? 0 : g;
      b = b < 0 ? 0 : b;
      r = r > 1.0f ? 1.0f : r;
      g = g > 1.0f ? 1.0f : g;
      b = b > 1.0f ? 1.0f : b;
      
      r = (int) (r * optimal_bri); 
      g = (int) (g * optimal_bri); 
      b = (int) (b * optimal_bri); 
    
      return CRGB(r, g, b);
    }
    
    CRGB convCtToRgb(uint8_t bri, int ct) {
      
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
    
      // Apply multiplier for white correction
      r = r * RGB_R / 100;
      g = g * RGB_G / 100;
      b = b * RGB_B / 100;
    
      r = r * (bri / 255.0f); 
      g = g * (bri / 255.0f); 
      b = b * (bri / 255.0f);
    
      return CRGB(r, g, b);
    }
    
    float deltaE(uint8_t bri1, float x1, float y1, uint8_t bri2, float x2, float y2){
    // calculate the perceptual distance between colors in CIELAB
    // https://github.com/THEjoezack/ColorMine/blob/master/ColorMine/ColorSpaces/Comparisons/Cie94Comparison.cs
    
      uint8_t deltaBri;
      float deltaX, deltaY, deltaC, deltaH, deltaLKlsl, deltaCkcsc, deltaHkhsh, c1, c2, sc, sh, i;
      
      deltaBri = bri1 - bri2;
      deltaX = x1 - x2;
      deltaY = y1 - y2;
      c1 = sqrt(x1 * x1 + y1 * y1);
      c2 = sqrt(x2 * x2 + y2 * y2);
      deltaC = c1 - c2;
      deltaH = deltaX * deltaX + deltaY * deltaY - deltaC * deltaC;
      deltaH = deltaH < 0 ? 0 : sqrt(deltaH);
      sc = 1.0 + 0.045 * c1;
      sh = 1.0 + 0.015 * c1;
      deltaLKlsl = deltaBri / (1.0);
      deltaCkcsc = deltaC / (sc);
      deltaHkhsh = deltaH / (sh);
      i = deltaLKlsl * deltaLKlsl + deltaCkcsc * deltaCkcsc + deltaHkhsh * deltaHkhsh;
      
      return i < 0 ? 0 : sqrt(i);
    }
    
    float ColorDist(CRGB color1, CRGB color2) {
    
      uint8_t bri1, bri2;
      float x1, y1, x2, y2;
    
      convRgbToXy(color1.r, color1.g, color1.b, bri1, x1, y1);
      convRgbToXy(color2.r, color2.g, color2.b, bri2, x2, y2);
      
      return deltaE(bri1, x1, y1, bri2, x2, y2);
    }
    
    void nblendU8TowardU8( uint8_t& cur, const uint8_t target, uint8_t amount){
    // Helper function that blends one uint8_t toward another by a given amount
      
      uint8_t delta;
      
      if( cur == target) return;
      if( cur < target ) {
        delta = target - cur;
        delta = scale8_video( delta, amount);
        cur += delta;
      } else {
        delta = cur - target;
        delta = scale8_video( delta, amount);
        cur -= delta;
      }
    }
    
    CRGB fadeTowardColor( CRGB& cur, const CRGB& target, uint8_t amount){
    // Blend one CRGB color toward another CRGB color by a given amount.
    // Blending is linear, and done in the RGB color space.
    // This function modifies 'cur' in place.
      
      nblendU8TowardU8( cur.red,   target.red,   amount);
      nblendU8TowardU8( cur.green, target.green, amount);
      nblendU8TowardU8( cur.blue,  target.blue,  amount);
      return cur;
    }
    
    void fadeTowardColor( CRGB* L, const CRGB& bgColor, int M, int N, uint8_t fadeAmount){
    // Fade an entire array of CRGBs toward a given background color by a given amount
    // This function modifies the pixel array in place.
      
      for( uint16_t i = M; i <= N; i++) {
        this->fadeTowardColor( L[i], bgColor, fadeAmount);
      }
    }

  public:
    struct state {
      CRGB color;                     // new color
      CRGB* currentColor;             // pointer to one led representing the color of that virtual hue-light
      int stepLevel;                  // amount of transition in every loop
    
      int firstLed, lastLed;          // range of leds representing this emulated hue-light
      int lightNr;                    // hue light-nr
      bool lightState;                // true = On, false = Off
      uint8_t colorMode;              // 1 = xy, 2 = ct, 3 = hue/sat
      
      uint8_t bri;                    //brightness (1 - 254)
      int hue;                        // 0 - 65635
      uint8_t sat;                    // 0 - 254
      float x;                        // 0 - 1  x-coordinate of CIE color space
      float y;                        // 0 - 1  y-coordinate of CIE color space
      int ct;                         //color temperatur in mired (500 mired/2000 K - 153 mired/6500 K)
    };
    state* lights = new state[0];    //holds the emulated hue-lights
    String LightName_;
    uint8_t LightsCount_;         //number of emulated hue-lights
    uint16_t PixelPerLight_;         //number of leds forming one emulated hue-light
    uint8_t TransitionLeds_;      //number of 'space'-leds inbetween the emulated hue-lights; pixelCount must be divisible by this value
    String UserName_ = "Z0lt8pVCK4YqzwzO8U3HzzxNy1-oxyvL7jAb5YwF";             //hue-username (needs to be configured in the diyHue-bridge
    int FirstHueLightNr_;         //first free number for the first hue-light (look in diyHue config.json)
    CRGB* leds_;
    byte* mac_;
    bool NeedSave;            // indecates, if we have unsaved changes
    
    // constructor
    HueApi();
    HueApi(CRGB* Leds,
           byte* mac,
           String LightName = "", 
           uint8_t LightsCount = 1,  
           uint16_t PixelPerLight = 1, 
           uint8_t TransitionLeds = 1,  
           String UserName = "",  
           int FirstHueLightNr = 1) :  
           leds_(Leds),
           mac_(mac),
           LightName_(LightName),
           LightsCount_(LightsCount),
           TransitionLeds_(TransitionLeds),
           UserName_(UserName),
           FirstHueLightNr_(FirstHueLightNr) {
    
      int x;
       
      maxDist = ColorDist(CRGB::White, CRGB::Black);
      lights = new state[LightsCount];
      
      for (uint8_t i = 0; i < LightsCount_; i++) {
        lights[i].firstLed = TransitionLeds_ / 2 + i * PixelPerLight + TransitionLeds_ * i;     //﻿=transitionLeds / 2 + i * lightLeds + transitionLeds * i
        lights[i].lastLed = lights[i].firstLed + PixelPerLight;
        lights[i].lightNr = FirstHueLightNr_ + i;
        
        x = lights[i].firstLed + (int)((lights[i].lastLed - lights[i].firstLed) / 2);
        lights[i].currentColor = &leds_[x];
    
        lights[i].lightState = true;
        lights[i].color = CRGB::Yellow;
        
        processLightdata(i, 4);
      }          
    }

    void processLightdata(uint8_t light, float transitiontime) {
    
      float tmp;
      
      if (lights[light].colorMode == 1 && lights[light].lightState == true) {
        lights[light].color = convXyToRgb(lights[light].bri, lights[light].x, lights[light].y);
      } else if (lights[light].colorMode == 2 && lights[light].lightState == true) {
        lights[light].color = convCtToRgb(lights[light].bri, lights[light].ct);
      } else if (lights[light].lightState == false) {
        lights[light].color = CRGB(CRGB::Black);
      }
      
      transitiontime *= 17 - (PixelPerLight_ / 40);         //every extra led add a small delay that need to be counted
      if (lights[light].lightState) {
        tmp = ColorDist(*lights[light].currentColor, lights[light].color);
        tmp = tmp / maxDist * 255;
        lights[light].stepLevel =  (int) (tmp / transitiontime);
      } else {
        tmp = ColorDist(*lights[light].currentColor, CRGB::Black);
        tmp = tmp / maxDist * 255;
        lights[light].stepLevel = (int) (tmp / transitiontime);
      }
    }

    void lightEngine() {
      
      for (int light = 0; light < LightsCount_; light++) {
        
        if ( lights[light].lightState && lights[light].color != *lights[light].currentColor || *lights[light].currentColor > CRGB(CRGB::Black)) {
          inTransition = true;
          this->fadeTowardColor(leds_, lights[light].color, lights[light].firstLed, lights[light].lastLed, lights[light].stepLevel);
        } else {
          inTransition = false;
        }
      }
      if (inTransition) delay(6);
    }

    void apply_scene(uint8_t new_scene) {
    
      for (uint8_t light = 0; light < LightsCount_; light++) {
        if ( new_scene == 1) {
          lights[light].color = convCtToRgb(254, 346);
        } else if ( new_scene == 2) {
          lights[light].color = convCtToRgb(254, 233);
        }  else if ( new_scene == 3) {
          lights[light].color = convCtToRgb(254, 156);
        }  else if ( new_scene == 4) {
          lights[light].color = convCtToRgb(77, 367);
        }  else if ( new_scene == 5) {
          lights[light].color = convCtToRgb(254, 447);
        }  else if ( new_scene == 6) {
          lights[light].color = convXyToRgb(1, 0.561, 0.4042);
        }  else if ( new_scene == 7) {
          lights[light].color = convXyToRgb(203, 0.380328, 0.39986);
        }  else if ( new_scene == 8) {
          lights[light].color = convXyToRgb(112, 0.359168, 0.28807);
        }  else if ( new_scene == 9) {
          lights[light].color = convXyToRgb(142, 0.267102, 0.23755);
        }  else if ( new_scene == 10) {
          lights[light].color = convXyToRgb(216, 0.393209, 0.29961);
        } else {
          lights[light].color = convCtToRgb(144, 447);
        }
      }
    }

        
    String Detect() {
            
      String output;
      char macString[32] = {0};
      sprintf(macString, "%02X:%02X:%02X:%02X:%02X:%02X", mac_[0], mac_[1], mac_[2], mac_[3], mac_[4], mac_[5]);
      DynamicJsonDocument root(1024);
      
      root["name"] = LightName_;
      root["lights"] = LightsCount_;
      root["protocol"] = "native_multi";
      root["modelid"] = "LCT015";
      root["type"] = "SK9822_strip";
      root["mac"] = macString;
      root["version"] = LIGHT_VERSION;
      
      serializeJson(root, output);
      return output;
    }

    String StateGet(String arg) {
      
      String output;
      uint8_t light;
      DynamicJsonDocument root(1024);
      JsonArray xy;
      float x, y;
      int bri;
    
      light = arg.toInt() - 1;
      
      root["on"] = lights[light].lightState;
      root["bri"] = lights[light].bri;
      root["ct"] = lights[light].ct;
      root["hue"] = lights[light].hue;
      root["sat"] = lights[light].sat;
      if (lights[light].colorMode == 1)
        root["colormode"] = "xy";
      else if (lights[light].colorMode == 2)
        root["colormode"] = "ct";
      else if (lights[light].colorMode == 3)
        root["colormode"] = "hs";
    
      convRgbToXy(lights[light].color.r, lights[light].color.g, lights[light].color.b, bri, x, y);
      xy = root.createNestedArray("xy");
      xy.add(lights[light].x);
      xy.add(lights[light].y);
      
      serializeJson(root, output);
      return output;
   }
      
    String StatePut(String arg) {
      
      String output;
      DynamicJsonDocument root(1024);
      DeserializationError error;
      JsonString key;
      JsonVariant values;
      int light, transitiontime;
      char* buffer[1024];
      
      error = deserializeJson(root, arg); 
      if (error) {
        return "FAIL. " + arg;
      } else {
        
        for (JsonPair state : root.as<JsonObject>()) {
          key = state.key();
          light = atoi(key.c_str()) - 1;
          values = state.value();
          transitiontime = 4;
    
          if (values.containsKey("xy")) {
            lights[light].x = values["xy"][0];
            lights[light].y = values["xy"][1];
            lights[light].colorMode = 1;
            lights[light].color = convXyToRgb(lights[light].bri, lights[light].x, lights[light].y);
          } else if (values.containsKey("ct")) {
            lights[light].ct = values["ct"];
            lights[light].colorMode = 2;
            lights[light].color = convCtToRgb(lights[light].bri, lights[light].ct);
          } else {
            if (values.containsKey("hue")) {
              lights[light].hue = values["hue"];
              lights[light].colorMode = 3;
            }
            if (values.containsKey("sat")) {
              lights[light].sat = values["sat"];
              lights[light].colorMode = 3;
            }
            lights[light].color = CHSV(lights[light].hue, lights[light].sat, lights[light].bri);
          }
    
          if (values.containsKey("on")) {
            if (values["on"]) {
              lights[light].lightState = true;
            } else {
              lights[light].lightState = false;
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
              lights[light].color = CRGB::Black;
            } else {
              lights[light].color = CRGB::Blue;
            }
          }
          processLightdata(light, transitiontime);
          NeedSave = true;
        }
        
        serializeJson(root, output);
        return output;
      }
    }
      
};
