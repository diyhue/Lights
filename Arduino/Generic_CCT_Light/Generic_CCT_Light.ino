#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>

// all variables can be set from light WebGUI. Change them here only if you want different defaults.

IPAddress address ( 192,  168,   0,  95); // choose an unique IP Adress
IPAddress gateway ( 192,  168,   0,   1); // Router IP
IPAddress submask(255, 255, 255,   0);

#define PWM_CHANNELS 2

struct state {
  uint8_t colors[PWM_CHANNELS], bri = 100;
  bool lightState;
  int ct = 200;
  float stepLevel[PWM_CHANNELS], currentColors[PWM_CHANNELS];
};

//core

state light;
bool inTransition, useDhcp = true;
byte mac[6], packetBuffer[8];

//settings
char *lightName = "New Hue CCT light";
uint8_t scene = 0, startup = false, onPin = 1, offPin = 3, pins[] = {4, 5}; //could white, warm white
bool hwSwitch = false;

ESP8266WebServer server(80);
WiFiUDP Udp;
ESP8266HTTPUpdateServer httpUpdateServer;


void convert_ct() {

  int optimal_bri = int( 10 + light.bri / 1.04);

  uint8 percent_warm = ((light.ct - 150) * 100) / 350;

  light.colors[0] =  (optimal_bri * (100 - percent_warm)) / 100;
  light.colors[1] = (optimal_bri * percent_warm) / 100;

}

void apply_scene(uint8_t new_scene) {
  if ( new_scene == 1) {
    light.bri = 254; light.ct = 346; convert_ct();
  } else if ( new_scene == 2) {
    light.bri = 254; light.ct = 233; convert_ct();
  }  else if ( new_scene == 3) {
    light.bri = 254; light.ct = 156; convert_ct();
  }  else if ( new_scene == 4) {
    light.bri = 77; light.ct = 367; convert_ct();
  }  else if ( new_scene == 5) {
    light.bri = 254; light.ct = 447; convert_ct();
  }  else {
    light.bri = 144; light.ct = 447; convert_ct();
  }
}

void processLightdata(float transitiontime) {
  convert_ct();
  transitiontime *= 16;
  for (uint8_t color = 0; color < PWM_CHANNELS; color++) {
    if (light.lightState) {
      light.stepLevel[color] = (light.colors[color] - light.currentColors[color]) / transitiontime;
    } else {
      light.stepLevel[color] = light.currentColors[color] / transitiontime;
    }
  }
}

void lightEngine() {
  for (uint8_t color = 0; color < PWM_CHANNELS; color++) {
    if (light.lightState) {
      if (light.colors[color] != light.currentColors[color] ) {
        inTransition = true;
        light.currentColors[color] += light.stepLevel[color];
        if ((light.stepLevel[color] > 0.0f && light.currentColors[color] > light.colors[color]) || (light.stepLevel[color] < 0.0f && light.currentColors[color] < light.colors[color])) light.currentColors[color] = light.colors[color];
        analogWrite(pins[color], (int)(light.currentColors[color] * 4.0));
      }
    } else {
      if (light.currentColors[color] != 0) {
        inTransition = true;
        light.currentColors[color] -= light.stepLevel[color];
        if (light.currentColors[color] < 0.0f) light.currentColors[color] = 0;
        analogWrite(pins[color], (int)(light.currentColors[color] * 4.0));
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
        light.lightState = true;
      }
      else {
        // there was a long press
        light.bri += 56;
        if (light.bri > 254) {
          // don't increase the brightness more then maximum value
          light.bri = 254;
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
        light.lightState = false;
      }
      else {
        // there was a long press
        light.bri -= 56;
        if (light.bri < 1) {
          // don't decrease the brightness less than minimum value.
          light.bri = 1;
        }
      }
      processLightdata(4);
    }
  }
}


void saveState() {
  DynamicJsonDocument json(1024);
  json["on"] = light.lightState;
  json["bri"] = light.bri;
  json["ct"] = light.ct;
  File stateFile = SPIFFS.open("/state.json", "w");
  serializeJson(json, stateFile);
}


void restoreState() {
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
  light.lightState = json["on"];
  light.bri = (uint8_t)json["bri"];
  light.ct = json["ct"];
}


bool saveConfig() {
  DynamicJsonDocument json(1024);
  json["name"] = lightName;
  json["startup"] = startup;
  json["scene"] = scene;
  json["c"] = pins[0];
  json["w"] = pins[1];
  json["on"] = onPin;
  json["off"] = offPin;
  json["hw"] = hwSwitch;
  json["dhcp"] = useDhcp;
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

bool loadConfig() {
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    //Serial.println("Create new file with default values");
    return saveConfig();
  }

  if (configFile.size() > 1024) {
    Serial.println("Config file size is too large");
    return false;
  }

  DynamicJsonDocument json(1024);
  DeserializationError error = deserializeJson(json, configFile.readString());
  if (error) {
    Serial.println("Failed to parse config file");
    return false;
  }

  strcpy(lightName, json["name"]);
  startup = (uint8_t) json["startup"];
  scene  = (uint8_t) json["scene"];
  pins[0] = (uint8_t) json["c"];
  pins[1] = (uint8_t) json["w"];
  onPin = (uint8_t) json["on"];
  offPin = (uint8_t) json["off"];
  hwSwitch = json["hw"];
  useDhcp = json["dhcp"];
  address = {json["addr"][0], json["addr"][1], json["addr"][2], json["addr"][3]};
  submask = {json["mask"][0], json["mask"][1], json["mask"][2], json["mask"][3]};
  gateway = {json["gw"][0], json["gw"][1], json["gw"][2], json["gw"][3]};
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

  if (startup == 1) {
    light.lightState = true;
  }
  if (startup == 0) {
    restoreState();
  } else {
    apply_scene(scene);
  }
  processLightdata(4);
  if (light.lightState) {
    for (uint8_t i = 0; i < 200; i++) {
      lightEngine();
    }
  }
  WiFi.mode(WIFI_STA);
  WiFiManager wifiManager;

  if (!useDhcp) {
    wifiManager.setSTAStaticIPConfig(address, gateway, submask);
  }

  if (!wifiManager.autoConnect(lightName)) {
    delay(3000);
    ESP.reset();
    delay(5000);
  }

  if (useDhcp) {
    address = WiFi.localIP();
    gateway = WiFi.gatewayIP();
    submask = WiFi.subnetMask();
  }

  if (! light.lightState)  {
    // Show that we are connected
    analogWrite(pins[1], 100);
    delay(500);
    analogWrite(pins[1], 0);
  }
  WiFi.macAddress(mac);

  httpUpdateServer.setup(&server);

  Udp.begin(2100);


  server.on("/state", HTTP_PUT, []() {
    DynamicJsonDocument root(1024);
    DeserializationError error = deserializeJson(root, server.arg("plain"));
    if (error) {
      server.send(404, "text/plain", "FAIL. " + server.arg("plain"));
    } else {
      int transitiontime = 4;


      if (root.containsKey("ct")) {
        light.ct = root["ct"];
      }
      if (root.containsKey("on")) {
        if (root["on"]) {
          light.lightState = true;
        } else {
          light.lightState = false;
        }
        if (startup == 0) {
          saveState();
        }
      }

      if (root.containsKey("bri")) {
        light.bri = root["bri"];
      }

      if (root.containsKey("bri_inc")) {
        light.bri += (int) root["bri_inc"];
        if (light.bri > 255) light.bri = 255;
        else if (light.bri < 1) light.bri = 1;
      }

      if (root.containsKey("transitiontime")) {
        transitiontime = root["transitiontime"];
      }

      if (root.containsKey("alert") && root["alert"] == "select") {
        if (light.lightState) {
          light.currentColors[0] = 0; light.currentColors[1] = 0; light.currentColors[2] = 0; light.currentColors[3] = 0;
        } else {
          light.currentColors[3] = 126; light.currentColors[4] = 126;
        }
      }
      String output;
      serializeJson(root, output);
      server.send(200, "text/plain", output);
      processLightdata(transitiontime);
    }
  });

  server.on("/state", HTTP_GET, []() {
    DynamicJsonDocument root(1024);
    root["on"] = light.lightState;
    root["bri"] = light.bri;
    root["ct"] = light.ct;
    root["colormode"] = "ct";
    String output;
    serializeJson(root, output);
    server.send(200, "text/plain", output);
  });

  server.on("/detect", []() {
    char macString[32] = {0};
    sprintf(macString, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    DynamicJsonDocument root(1024);
    root["name"] = lightName;
    root["protocol"] = "native_single";
    root["modelid"] = "LTW001";
    root["type"] = "cct";
    root["mac"] = String(macString);
    root["version"] = 2.0;
    String output;
    serializeJson(root, output);
    server.send(200, "text/plain", output);
  });

  server.on("/config", []() {
    DynamicJsonDocument root(1024);
    root["name"] = lightName;
    root["scene"] = scene;
    root["startup"] = startup;
    root["cw"] = pins[0];
    root["ww"] = pins[1];
    root["hw"] = hwSwitch;
    root["on"] = onPin;
    root["off"] = offPin;
    root["hwswitch"] = (int)hwSwitch;
    root["dhcp"] = (int)useDhcp;
    root["addr"] = (String)address[0] + "." + (String)address[1] + "." + (String)address[2] + "." + (String)address[3];
    root["gw"] = (String)gateway[0] + "." + (String)gateway[1] + "." + (String)gateway[2] + "." + (String)gateway[3];
    root["sm"] = (String)submask[0] + "." + (String)submask[1] + "." + (String)submask[2] + "." + (String)submask[3];
    String output;
    serializeJson(root, output);
    server.send(200, "text/plain", output);
  });

  server.on("/", []() {
    if (server.hasArg("scene")) {
      server.arg("name").toCharArray(lightName, server.arg("name").length() + 1);
      startup = server.arg("startup").toInt();
      scene = server.arg("scene").toInt();
      pins[0] = server.arg("cw").toInt();
      pins[1] = server.arg("ww").toInt();
      hwSwitch = server.arg("hwswitch").toInt();
      onPin = server.arg("on").toInt();
      offPin = server.arg("off").toInt();
      saveConfig();
    } else if (server.hasArg("dhcp")) {
      useDhcp = server.arg("dhcp").toInt();
      address.fromString(server.arg("addr"));
      gateway.fromString(server.arg("gw"));
      submask.fromString(server.arg("sm"));
      saveConfig();
    }

    const char * htmlContent = "<!DOCTYPE html><html> <head> <meta charset=\"UTF-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"> <title>Hue Light</title> <link rel=\"stylesheet\" href=\"https://diyhue.org/cdn/bootstrap.min.css\"> <link rel=\"stylesheet\" href=\"https://diyhue.org/cdn/ion.rangeSlider.min.css\"/> <script src=\"https://diyhue.org/cdn/jquery-3.3.1.min.js\"></script> <script src=\"https://diyhue.org/cdn/bootstrap.min.js\"></script> <script src=\"https://diyhue.org/cdn/ion.rangeSlider.min.js\"></script> </head> <body> <nav class=\"navbar navbar-expand-lg navbar-light bg-light rounded\"> <button class=\"navbar-toggler\" type=\"button\" data-toggle=\"collapse\" data-target=\"#navbarToggler\" aria-controls=\"navbarToggler\" aria-expanded=\"false\" aria-label=\"Toggle navigation\"> <span class=\"navbar-toggler-icon\"></span> </button> <h2></h2> <div class=\"collapse navbar-collapse justify-content-md-center\" id=\"navbarToggler\"> <ul class=\"nav nav-pills\"> <li class=\"nav-item\"> <a class=\"nav-link active\" data-toggle=\"pill\" href=\"#home\">Home</a> </li><li class=\"nav-item\"> <a class=\"nav-link\" data-toggle=\"pill\" href=\"#menu1\">Settings</a> </li><li class=\"nav-item\"> <a class=\"nav-link\" data-toggle=\"pill\" href=\"#menu2\">Network</a> </li><li class=\"nav-item\"> <a class=\"nav-link\" data-toggle=\"pill\" href=\"#\" disabled> </a> </li><li class=\"nav-item\"> <a class=\"nav-link\" data-toggle=\"pill\" href=\"#\" disabled> </a> </li></ul> </div></nav> <div class=\"tab-content\"> <div class=\"tab-pane container active\" id=\"home\"> <br><br><form> <div class=\"form-group row\"> <label for=\"power\" class=\"col-sm-2 col-form-label\">Power</label> <div class=\"col-sm-10\"> <div id=\"power\" class=\"btn-group\" role=\"group\"> <button type=\"button\" class=\"btn btn-default border\" id=\"power-on\">On</button> <button type=\"button\" class=\"btn btn-default border\" id=\"power-off\">Off</button> </div></div></div><div class=\"form-group row\"> <label for=\"bri\" class=\"col-sm-2 col-form-label\">Brightness</label> <div class=\"col-sm-10\"> <input type=\"text\" id=\"bri\" class=\"js-range-slider\" name=\"bri\" value=\"\"/> </div></div><div class=\"form-group row\"> <label for=\"hue\" class=\"col-sm-2 col-form-label\">Color</label> <div class=\"col-sm-10\"> <canvas id=\"hue\" width=\"320px\" height=\"320px\" style=\"display:none;\"></canvas> </div></div><div class=\"form-group row\"> <label for=\"color\" class=\"col-sm-2 col-form-label\">Color Temp</label> <div class=\"col-sm-10\"> <canvas id=\"ct\" width=\"320px\" height=\"50px\" style=\"border:1px solid #d3d3d3;\"></canvas> </div></div></form> </div><div class=\"tab-pane container fade\" id=\"menu1\"> <br><form method=\"POST\" action=\"/\"> <div class=\"form-group row\"> <label for=\"name\" class=\"col-sm-2 col-form-label\">Light Name</label> <div class=\"col-sm-6\"> <input type=\"text\" class=\"form-control\" id=\"name\" name=\"name\"> </div></div><div class=\"form-group row\"> <label class=\"control-label col-sm-2\" for=\"startup\">Default Power:</label> <div class=\"col-sm-4\"> <select class=\"form-control\" name=\"startup\" id=\"startup\"> <option value=\"0\">Last State</option> <option value=\"1\">On</option> <option value=\"2\">Off</option> </select> </div></div><div class=\"form-group row\"> <label class=\"control-label col-sm-2\" for=\"scene\">Default Scene:</label> <div class=\"col-sm-4\"> <select class=\"form-control\" name=\"scene\" id=\"scene\"> < <option value=\"0\">Relax</option> <option value=\"1\">Read</option> <option value=\"2\">Concentrate</option> <option value=\"3\">Energize</option> <option value=\"4\">Bright</option> <option value=\"5\">Dimmed</option> </select> </div></div><div class=\"form-group row\"> <label for=\"ww\" class=\"col-sm-2 col-form-label\">Warm W Pin</label> <div class=\"col-sm-3\"> <input type=\"number\" class=\"form-control\" id=\"ww\" name=\"ww\" placeholder=\"\"> </div></div><div class=\"form-group row\"> <label for=\"cw\" class=\"col-sm-2 col-form-label\">Cold W Pin</label> <div class=\"col-sm-3\"> <input type=\"number\" class=\"form-control\" id=\"cw\" name=\"cw\" placeholder=\"\"> </div></div><div class=\"form-group row\"> <label class=\"control-label col-sm-2\" for=\"hwswitch\">HW Switch:</label> <div class=\"col-sm-2\"> <select class=\"form-control\" name=\"hwswitch\" id=\"hwswitch\"> <option value=\"1\">Yes</option> <option value=\"0\">No</option> </select> </div></div><div class=\"form-group row\"> <label for=\"on\" class=\"col-sm-2 col-form-label\">On Pin</label> <div class=\"col-sm-3\"> <input type=\"number\" class=\"form-control\" id=\"on\" name=\"on\" placeholder=\"\"> </div></div><div class=\"form-group row\"> <label for=\"off\" class=\"col-sm-2 col-form-label\">Off Pin</label> <div class=\"col-sm-3\"> <input type=\"number\" class=\"form-control\" id=\"off\" name=\"off\" placeholder=\"\"> </div></div><div class=\"form-group row\"> <div class=\"col-sm-10\"> <button type=\"submit\" class=\"btn btn-primary\">Save</button> </div></div></form> </div><div class=\"tab-pane container fade\" id=\"menu2\"> <br><form method=\"POST\" action=\"/\"> <div class=\"form-group row\"> <label class=\"control-label col-sm-2\" for=\"dhcp\">DHCP:</label> <div class=\"col-sm-3\"> <select class=\"form-control\" name=\"dhcp\" id=\"dhcp\"> <option value=\"1\">On</option> <option value=\"0\">Off</option> </select> </div></div><div class=\"form-group row\"> <label for=\"addr\" class=\"col-sm-2 col-form-label\">Ip</label> <div class=\"col-sm-4\"> <input type=\"text\" class=\"form-control\" id=\"addr\" name=\"addr\"> </div></div><div class=\"form-group row\"> <label for=\"sm\" class=\"col-sm-2 col-form-label\">Submask</label> <div class=\"col-sm-4\"> <input type=\"text\" class=\"form-control\" id=\"sm\" name=\"sm\"> </div></div><div class=\"form-group row\"> <label for=\"gw\" class=\"col-sm-2 col-form-label\">Gateway</label> <div class=\"col-sm-4\"> <input type=\"text\" class=\"form-control\" id=\"gw\" name=\"gw\"> </div></div><div class=\"form-group row\"> <div class=\"col-sm-10\"> <button type=\"submit\" class=\"btn btn-primary\">Save</button> </div></div></form> </div></div><script src=\"https://diyhue.org/cdn/color.min.js\"></script> </body></html>";
    server.send(200, "text/html", htmlContent);
    if (server.args()) {
      delay(100);
      ESP.reset();
    }

  });

  server.on("/reset", []() {
    server.send(200, "text/html", "reset");
    delay(100);
    ESP.reset();
  });

  server.onNotFound(handleNotFound);

  server.begin();
}


void loop() {
  server.handleClient();
  lightEngine();
}
