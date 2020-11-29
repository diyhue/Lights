#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ir_Coolix.h> // replace library based on your AC unit model, check https://github.com/markszabo/IRremoteESP8266


// all variables can be set from light WebGUI. Change them here only if you want different defaults.

IPAddress address ( 192,  168,   0,  95); // choose an unique IP Adress
IPAddress gateway ( 192,  168,   0,   1); // Router IP
IPAddress submask(255, 255, 255,   0);

const uint16_t kIrLed = D2;  // ESP8266 GPIO pin to use. Recommended: 4 (D2).
IRCoolixAC ac(kIrLed);  // Set the GPIO to be used to sending the message

#define minDelay 1000 // millis
unsigned long lastEPMillis;

struct state {
  uint8_t bri = 100;
  bool lightState;
  int ct = 200;
};

//core

state light;
bool inTransition, useDhcp = true;
byte mac[6], packetBuffer[8];

//settings
char *lightName = "IR Remote Control";

ESP8266WebServer server(80);
WiFiUDP Udp;
ESP8266HTTPUpdateServer httpUpdateServer;


bool saveConfig() {
  DynamicJsonDocument json(1024);
  json["name"] = lightName;
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
  ac.begin();


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

  WiFi.macAddress(mac);

  httpUpdateServer.setup(&server);

  Udp.begin(2100);


  server.on("/state", HTTP_PUT, []() {
    DynamicJsonDocument root(1024);
    DeserializationError error = deserializeJson(root, server.arg("plain"));
    if (error) {
      server.send(404, "text/plain", "FAIL. " + server.arg("plain"));
    } else {
      if (root.containsKey("on")) {
        light.lightState = root["on"];
      }

      if (root.containsKey("bri")) {
        light.bri = (uint8_t) root["bri"];
      }

      if (root.containsKey("ct")) {
        light.ct = (int) root["ct"];
      }

      String output;
      serializeJson(root, output);
      server.send(200, "text/plain", output);

      if ((millis() - lastEPMillis) >= minDelay) {
        delay(500);

        ///perform on action to AC unit
        if (light.lightState) {
          ac.on();
          ///perform actions based on brightness
          ac.setTemp(light.bri / 20 + 17);

          ac.setMode(kCoolixAuto);
          ac.setFan(kCoolixFanAuto);
        } else {
          ac.off();
        }
        ac.send();
        lastEPMillis = millis();

      }
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
    root["type"] = "ir";
    root["mac"] = String(macString);
    root["version"] = 2.0;
    String output;
    serializeJson(root, output);
    server.send(200, "text/plain", output);
  });

  server.on("/config", []() {
    DynamicJsonDocument root(1024);
    root["name"] = lightName;
    root["dhcp"] = (int)useDhcp;
    root["addr"] = (String)address[0] + "." + (String)address[1] + "." + (String)address[2] + "." + (String)address[3];
    root["gw"] = (String)gateway[0] + "." + (String)gateway[1] + "." + (String)gateway[2] + "." + (String)gateway[3];
    root["sm"] = (String)submask[0] + "." + (String)submask[1] + "." + (String)submask[2] + "." + (String)submask[3];
    String output;
    serializeJson(root, output);
    server.send(200, "text/plain", output);
  });

  server.on("/", []() {
    if (server.hasArg("name")) {
      server.arg("name").toCharArray(lightName, server.arg("name").length() + 1);
      saveConfig();
    } else if (server.hasArg("dhcp")) {
      useDhcp = server.arg("dhcp").toInt();
      address.fromString(server.arg("addr"));
      gateway.fromString(server.arg("gw"));
      submask.fromString(server.arg("sm"));
      saveConfig();
    }

    const char * htmlContent = "<!DOCTYPE html><html> <head> <meta charset=\"UTF-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"> <title>Hue Light</title> <link rel=\"stylesheet\" href=\"https://diyhue.org/cdn/bootstrap.min.css\"> <link rel=\"stylesheet\" href=\"https://diyhue.org/cdn/ion.rangeSlider.min.css\"/> <script src=\"https://diyhue.org/cdn/jquery-3.3.1.min.js\"></script> <script src=\"https://diyhue.org/cdn/bootstrap.min.js\"></script> <script src=\"https://diyhue.org/cdn/ion.rangeSlider.min.js\"></script> </head> <body> <nav class=\"navbar navbar-expand-lg navbar-light bg-light rounded\"> <button class=\"navbar-toggler\" type=\"button\" data-toggle=\"collapse\" data-target=\"#navbarToggler\" aria-controls=\"navbarToggler\" aria-expanded=\"false\" aria-label=\"Toggle navigation\"> <span class=\"navbar-toggler-icon\"></span> </button> <h2></h2> <div class=\"collapse navbar-collapse justify-content-md-center\" id=\"navbarToggler\"> <ul class=\"nav nav-pills\"> <li class=\"nav-item\"> <a class=\"nav-link active\" data-toggle=\"pill\" href=\"#home\">Home</a> </li><li class=\"nav-item\"> <a class=\"nav-link\" data-toggle=\"pill\" href=\"#menu1\">Settings</a> </li><li class=\"nav-item\"> <a class=\"nav-link\" data-toggle=\"pill\" href=\"#menu2\">Network</a> </li><li class=\"nav-item\"> <a class=\"nav-link\" data-toggle=\"pill\" href=\"#\" disabled> </a> </li><li class=\"nav-item\"> <a class=\"nav-link\" data-toggle=\"pill\" href=\"#\" disabled> </a> </li></ul> </div></nav> <div class=\"tab-content\"> <div class=\"tab-pane container active\" id=\"home\"> <br><br><form> <div class=\"form-group row\"> <label for=\"power\" class=\"col-sm-2 col-form-label\">Power</label> <div class=\"col-sm-10\"> <div id=\"power\" class=\"btn-group\" role=\"group\"> <button type=\"button\" class=\"btn btn-default border\" id=\"power-on\">On</button> <button type=\"button\" class=\"btn btn-default border\" id=\"power-off\">Off</button> </div></div></div><div class=\"form-group row\"> <label for=\"bri\" class=\"col-sm-2 col-form-label\">Brightness</label> <div class=\"col-sm-10\"> <input type=\"text\" id=\"bri\" class=\"js-range-slider\" name=\"bri\" value=\"\"/> </div></div><div class=\"form-group row\"> <label for=\"hue\" class=\"col-sm-2 col-form-label\">Color</label> <div class=\"col-sm-10\"> <canvas id=\"hue\" width=\"320px\" height=\"320px\" style=\"display:none;\"></canvas> </div></div><div class=\"form-group row\"> <label for=\"color\" class=\"col-sm-2 col-form-label\">Temp</label> <div class=\"col-sm-10\"> <canvas id=\"ct\" width=\"320px\" height=\"50px\" style=\"border:1px solid #d3d3d3;\"></canvas> </div></div></form> </div><div class=\"tab-pane container fade\" id=\"menu1\"> <br><form method=\"POST\" action=\"/\"> <div class=\"form-group row\"> <label for=\"name\" class=\"col-sm-2 col-form-label\">Light Name</label> <div class=\"col-sm-6\"> <input type=\"text\" class=\"form-control\" id=\"name\" name=\"name\"> </div></div><div class=\"form-group row\"> <div class=\"col-sm-10\"> <button type=\"submit\" class=\"btn btn-primary\">Save</button> </div></div></form> </div><div class=\"tab-pane container fade\" id=\"menu2\"> <br><form method=\"POST\" action=\"/\"> <div class=\"form-group row\"> <label class=\"control-label col-sm-2\" for=\"dhcp\">DHCP:</label> <div class=\"col-sm-3\"> <select class=\"form-control\" name=\"dhcp\" id=\"dhcp\"> <option value=\"1\">On</option> <option value=\"0\">Off</option> </select> </div></div><div class=\"form-group row\"> <label for=\"addr\" class=\"col-sm-2 col-form-label\">Ip</label> <div class=\"col-sm-4\"> <input type=\"text\" class=\"form-control\" id=\"addr\" name=\"addr\"> </div></div><div class=\"form-group row\"> <label for=\"sm\" class=\"col-sm-2 col-form-label\">Submask</label> <div class=\"col-sm-4\"> <input type=\"text\" class=\"form-control\" id=\"sm\" name=\"sm\"> </div></div><div class=\"form-group row\"> <label for=\"gw\" class=\"col-sm-2 col-form-label\">Gateway</label> <div class=\"col-sm-4\"> <input type=\"text\" class=\"form-control\" id=\"gw\" name=\"gw\"> </div></div><div class=\"form-group row\"> <div class=\"col-sm-10\"> <button type=\"submit\" class=\"btn btn-primary\">Save</button> </div></div></form> </div></div><script src=\"https://diyhue.org/cdn/color.min.js\"></script> </body></html>";
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
}
