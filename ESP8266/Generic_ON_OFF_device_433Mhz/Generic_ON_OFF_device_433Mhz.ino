

/*
  This can control Generic Power Outlets with Remote Control over 433Mhz.
  Maximum 8 Devices.
  Simulates 2 Remotes with 4 Items each.
  Showing 8 Individual Hue Bulbs

*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <RCSwitch.h>

RCSwitch mySwitch = RCSwitch();


//############ CONFIG ############

#define light_name "Hue Plug"  //default light name
#define LIGHT_VERSION 2.1
#define LIGHTS_COUNT 8 // 4 or 8 --> maximum 8

char* houseCodeA = "11110"; //Group A --> Remote Code for Socket 1-4
char* houseCodeB = "11100"; //Group B --> Remote Code for Socket 5-8
uint8_t transmitterPin = 4;     // What Pin is the Transmitter conected?
uint8_t transmitterDelay = 100; // Delay between sending commands in ms //default 100
uint8_t repeatTransmit = 2; // Number of Transmit attempts //default 2

//##########END OF CONFIG ##############



uint8_t pins[LIGHTS_COUNT] = {12, 13, 14, 5, 12, 13, 14, 5}; //irrelevant
char* deviceId[] = {"10000", "01000", "00100", "00010", "10000", "01000", "00100", "00010"};
int c;

//############ CONFIG ############

//#define USE_STATIC_IP //! uncomment to enable Static IP Adress
#ifdef USE_STATIC_IP
IPAddress strip_ip ( 192,  168,   0,  95); // choose an unique IP Adress
IPAddress gateway_ip ( 192,  168,   0,   1); // Router IP
IPAddress subnet_mask(255, 255, 255,   0);
#endif

bool light_state[LIGHTS_COUNT];
byte mac[6];

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdateServer;

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

void SwitchOn433(uint8_t c) {

  for (int x = 0; x < repeatTransmit; x++) {

    if (c <= 3) {
      mySwitch.switchOn(houseCodeA, deviceId[c]);
      delay(transmitterDelay);
    }
    else {
      mySwitch.switchOn(houseCodeB, deviceId[c]);
      delay(transmitterDelay);

    }

  }

}
void SwitchOff433(uint8_t c) {

  for (int x = 0; x < repeatTransmit; x++) {


    if (c <= 3) {
      mySwitch.switchOff(houseCodeA, deviceId[c]);
      delay(transmitterDelay);

    } else {

      mySwitch.switchOff(houseCodeB, deviceId[c]);
      delay(transmitterDelay);
    }

  }
}

void setup() {
  EEPROM.begin(512);
  Serial.begin(115200);
  mySwitch.enableTransmit(transmitterPin);

  for (uint8_t ch = 0; ch < LIGHTS_COUNT; ch++) {
    pinMode(pins[ch], OUTPUT);
  }


 #ifdef USE_STATIC_IP
  WiFi.config(strip_ip, gateway_ip, subnet_mask);
#endif


  if (EEPROM.read(1) == 1 || (EEPROM.read(1) == 0 && EEPROM.read(0) == 1)) {
    for (uint8_t ch = 0; ch < LIGHTS_COUNT; ch++) {
      digitalWrite(pins[ch], OUTPUT);
    }

  }

  WiFi.mode(WIFI_STA);
  WiFiManager wifiManager;

  wifiManager.setConfigPortalTimeout(120);
  wifiManager.autoConnect(light_name);

  WiFi.macAddress(mac);


  server.on("/state", HTTP_PUT, []() { // HTTP PUT request used to set a new light state
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

        if (values.containsKey("on")) {
          if (values["on"]) {
            light_state[light] = true;
            digitalWrite(pins[light], HIGH);
            SwitchOn433(light);
            if (EEPROM.read(1) == 0 && EEPROM.read(0) == 0) {
              EEPROM.write(0, 1);
            }
          } else {
            light_state[light] = false;
            digitalWrite(pins[light], LOW);
            SwitchOff433(light);
            if (EEPROM.read(1) == 0 && EEPROM.read(0) == 1) {
              EEPROM.write(0, 0);
            }
          }
        }
      }
      String output;
      serializeJson(root, output);
      server.send(200, "text/plain", output);
    }
  });

  server.on("/state", HTTP_GET, []() { // HTTP GET request used to fetch current light state
    uint8_t light = server.arg("light").toInt() - 1;
    DynamicJsonDocument root(1024);
    root["on"] = light_state[light];
    String output;
    serializeJson(root, output);
    server.send(200, "text/plain", output);
  });


  server.on("/detect", []() { // HTTP GET request used to discover the light type
    char macString[32] = {0};
    sprintf(macString, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    DynamicJsonDocument root(1024);
    root["name"] = light_name;
    root["lights"] = LIGHTS_COUNT;
    root["protocol"] = "native_multi";
    root["modelid"] = "LOM001";
    root["type"] = "plug_device";
    root["mac"] = String(macString);
    root["version"] = LIGHT_VERSION;
    String output;
    serializeJson(root, output);
    server.send(200, "text/plain", output);
  });

  server.on("/", []() {
    float transitiontime = 4;
    if (server.hasArg("startup")) {
      if (  EEPROM.read(1) != server.arg("startup").toInt()) {
        EEPROM.write(1, server.arg("startup").toInt());
        EEPROM.commit();
      }
    }

    for (uint8_t device = 0; device < LIGHTS_COUNT; device++) {

      if (server.hasArg("on")) {
        if (server.arg("on") == "true") {
          light_state[device] = true;
          digitalWrite(pins[device], HIGH);
          if (EEPROM.read(1) == 0 && EEPROM.read(0) != 1) {
            EEPROM.write(0, 1);
            EEPROM.commit();
          }
        } else {
          light_state[device] = false;
          digitalWrite(pins[device], LOW);
          SwitchOff433(device);
          if (EEPROM.read(1) == 0 && EEPROM.read(0) != 0) {
            EEPROM.write(0, 0);
            EEPROM.commit();
          }
        }
      }
    }
    if (server.hasArg("reset")) {
      ESP.reset();
    }


    String http_content = "<!doctype html>";
    http_content += "<html>";
    http_content += "<head>";
    http_content += "<meta charset=\"utf-8\">";
    http_content += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
    http_content += "<title>Light Setup</title>";
    http_content += "<link rel=\"stylesheet\" href=\"https://unpkg.com/purecss@0.6.2/build/pure-min.css\">";
    http_content += "</head>";
    http_content += "<body>";
    http_content += "<fieldset>";
    http_content += "<h3>Light Setup</h3>";
    http_content += "<form class=\"pure-form pure-form-aligned\" action=\"/\" method=\"post\">";
    http_content += "<div class=\"pure-control-group\">";
    http_content += "<label for=\"power\"><strong>Power</strong></label>";
    http_content += "<a class=\"pure-button"; if (light_state[0]) http_content += "  pure-button-primary"; http_content += "\" href=\"/?on=true\">ON</a>";
    http_content += "<a class=\"pure-button"; if (!light_state[0]) http_content += "  pure-button-primary"; http_content += "\" href=\"/?on=false\">OFF</a>";
    http_content += "</div>";
    http_content += "</fieldset>";
    http_content += "</form>";
    http_content += "</body>";
    http_content += "</html>";

    server.send(200, "text/html", http_content);

  });

  server.on("/reset", []() { // trigger manual reset
    server.send(200, "text/html", "reset");
    delay(1000);
    ESP.restart();
  });

  server.onNotFound(handleNotFound);

  server.begin();
}

void loop() {
  server.handleClient();
}
