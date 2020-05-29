#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

////////////////////////////// USER SETTINGS ////////////////////////////// 

#define devicesCount 2                          //  How many Light attach 
#define light_name "SonoffTX2C"                 //  default light name (ArduinoOTA name)

//uint8_t devicesPins[devicesCount] = {12};     //  the pin that the light is attached to  (FOR SonoffTX 1 gang)
uint8_t devicesPins[devicesCount] = {12,5};     //  the pin that the light is attached to  (FOR SonoffTX 2 gang)
//uint8_t devicesPins[devicesCount] = {12,5,4}; //  the pin that the light is attached to  (FOR SonoffTX 3 gang)

#ifndef STASSID
#define STASSID "YOUR SSID"                   //  Set your WIFI SSID to connect to
#define STAPSK  "YOUR PASSWORD"                       //  Set your WIFI PASSWORD to connect to
#endif

uint8_t ledPin = 13;                             // the pin that the indicator light is attached to
uint8_t button1Pin = 0;                          // the pin that the pushbutton1 is attached to
uint8_t button2Pin = 9;                          // the pin that the pushbutton2 is attached to
uint8_t button3Pin = 10;                         // the pin that the pushbutton3 is attached to

////////////////////////////////////////////////////////////////////////

uint8_t button1State = digitalRead(button1Pin);
uint8_t button2State = digitalRead(button2Pin);
uint8_t button3State = digitalRead(button3Pin);
uint8_t lastButton1State = button1State;
uint8_t lastButton2State = button2State;
uint8_t lastButton3State = button3State;
unsigned long lastButton1Push = 0;
unsigned long lastButton2Push = 0;
unsigned long lastButton3Push = 0;
uint8_t buttonThreshold = 10;
bool device_state[devicesCount];
byte mac[6];
const char* ssid     = STASSID;
const char* password = STAPSK;
ESP8266WebServer server(80);

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

for (uint8_t ch = 0; ch < devicesCount; ch++) {
pinMode(devicesPins[ch], OUTPUT);
  }
  
digitalWrite(devicesPins[0], HIGH);
digitalWrite(devicesPins[1], HIGH);
digitalWrite(devicesPins[2], HIGH);
EEPROM.begin(512);
pinMode(ledPin, OUTPUT);
pinMode(button1Pin, INPUT);
pinMode(button2Pin, INPUT);
pinMode(button3Pin, INPUT);
digitalWrite(ledPin, HIGH);
WiFi.mode(WIFI_STA);
WiFi.begin(ssid, password);

  while (WiFi.status() == WL_CONNECTED) {
  }
  if (EEPROM.read(1) == 1 || (EEPROM.read(1) == 0 && EEPROM.read(0) == 1)) {
    for (uint8_t ch = 0; ch < devicesCount; ch++) {
      digitalWrite(devicesPins[ch], OUTPUT);
    }
  }

  delay(10000);
  WiFi.macAddress(mac); 
  ArduinoOTA.setHostname(light_name);
  ArduinoOTA.begin();
      digitalWrite(ledPin, LOW);
      delay(40);
      digitalWrite(ledPin, HIGH);
      delay(40);
      digitalWrite(ledPin, LOW);
      delay(40);
      digitalWrite(ledPin, HIGH);
      delay(40);
      digitalWrite(ledPin, LOW);
      delay(40);
      digitalWrite(ledPin, HIGH);
      delay(40);
      digitalWrite(ledPin, LOW);
      delay(40);
      digitalWrite(ledPin, HIGH);
      delay(40);
      
  server.on("/set", []() {
    uint8_t device;
    for (uint8_t i = 0; i < server.args(); i++) {
      if (server.argName(i) == "light") {
        device = server.arg(i).toInt() - 1;
      }
      else if (server.argName(i) == "on") {
        if (server.arg(i) == "True" || server.arg(i) == "true") {
          if (EEPROM.read(1) == 0 && EEPROM.read(0) != 1) {
            EEPROM.write(0, 1);
            EEPROM.commit();
          }
          device_state[device] = true;
          digitalWrite(devicesPins[device], HIGH);
        }
        else {
          if (EEPROM.read(1) == 0 && EEPROM.read(0) != 0) {
            EEPROM.write(0, 0);
            EEPROM.commit();
          }
          device_state[device] = false;
          digitalWrite(devicesPins[device], LOW);
        }
      }
    }
    server.send(200, "text/plain", "OK, state:" + device_state[device]);
  });
  server.on("/get", []() {
    uint8_t light;
    if (server.hasArg("light"))
      light = server.arg("light").toInt() - 1;
    String power_status;
    power_status = device_state[light] ? "true" : "false";
    server.send(200, "text/plain", "{\"on\": " + power_status + "}");
  });
  server.on("/detect", []() {
    server.send(200, "text/plain", "{\"hue\": \"bulb\",\"lights\": " + String(devicesCount) + ",\"name\": \"" light_name "\",\"modelid\": \"Plug 01\",\"mac\": \"" + String(mac[5], HEX) + ":"  + String(mac[4], HEX) + ":" + String(mac[3], HEX) + ":" + String(mac[2], HEX) + ":" + String(mac[1], HEX) + ":" + String(mac[0], HEX) + "\"}");
  });

  server.on("/", []() {
    float transitiontime = 100;
    if (server.hasArg("startup")) {
      if (  EEPROM.read(1) != server.arg("startup").toInt()) {
        EEPROM.write(1, server.arg("startup").toInt());
        EEPROM.commit();
      }
    }
    for (uint8_t device = 0; device < devicesCount; device++) {
      if (server.hasArg("on")) {
        if (server.arg("on") == "true") {
          device_state[device] = true;
          digitalWrite(devicesPins[device], HIGH);
          if (EEPROM.read(1) == 0 && EEPROM.read(0) != 1) {
            EEPROM.write(0, 1);
            EEPROM.commit();
          }
        } else {
          device_state[device] = false;
          digitalWrite(devicesPins[device], LOW);
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
    http_content += "<a class=\"pure-button"; if (device_state[0]) http_content += "  pure-button-primary"; http_content += "\" href=\"/?on=true\">ON</a>";
    http_content += "<a class=\"pure-button"; if (!device_state[0]) http_content += "  pure-button-primary"; http_content += "\" href=\"/?on=false\">OFF</a>";
    http_content += "</div>";
    http_content += "<div class=\"pure-control-group\">";
    http_content += "<label for=\"startup\">Startup</label>";
    http_content += "<select onchange=\"this.form.submit()\" id=\"startup\" name=\"startup\">";
    http_content += "<option "; if (EEPROM.read(1) == 0) http_content += "selected=\"selected\""; http_content += " value=\"0\">Last state</option>";
    http_content += "<option "; if (EEPROM.read(1) == 1) http_content += "selected=\"selected\""; http_content += " value=\"1\">On</option>";
    http_content += "<option "; if (EEPROM.read(1) == 2) http_content += "selected=\"selected\""; http_content += " value=\"2\">Off</option>";
    http_content += "</select>";
    http_content += "</div>";
    http_content += "<div class=\"pure-controls\">";
    http_content += "<span class=\"pure-form-message\"><a href=\"/?alert=1\">alert</a> or <a href=\"/?reset=1\">reset</a></span>";
    http_content += "<label for=\"cb\" class=\"pure-checkbox\">";
    http_content += "</label>";
    http_content += "<button type=\"submit\" class=\"pure-button pure-button-primary\">Save</button>";
    http_content += "</div>";
    http_content += "</fieldset>";
    http_content += "</form>";
    http_content += "</body>";
    http_content += "</html>";
    server.send(200, "text/html", http_content);
  });
  server.onNotFound(handleNotFound);
  server.begin();
}

void btn1read () {
  
  if (millis() < lastButton1Push + buttonThreshold) return; // check button only when the threshold after last push is reached
  lastButton1Push = millis();
  button1State = digitalRead(button1Pin);
  if (button1State == lastButton1State) return;
  if (button1State == HIGH) {
    for (uint8_t device = 0; device < devicesCount; device++) {
      device_state[device] = !device_state[device];
      if (device_state[0] == true) {
        digitalWrite(devicesPins[0], HIGH);
        device_state[0] == true;
        if (EEPROM.read(1) == 0 && EEPROM.read(0) != 1) {
          EEPROM.write(0, 1);
          EEPROM.commit();
        }
      } else {
        digitalWrite(devicesPins[0], LOW);
        device_state[0] == false;
       
        if (EEPROM.read(1) == 0 && EEPROM.read(0) != 0) {
          EEPROM.write(0, 0);
          EEPROM.commit();
        }
      }
    }
  }
  lastButton1State = button1State;
    String power_status;
    power_status = device_state[0] ? "true" : "false";
    server.send(200, "text/plain", "{\"on\": " + power_status + "}");
}

void btn2read () {
  
  if (millis() < lastButton2Push + buttonThreshold) return; // check button only when the threshold after last push is reached
  lastButton2Push = millis();
  button2State = digitalRead(button2Pin);
  if (button2State == lastButton2State) return;
  if (button2State == HIGH) {
    for (uint8_t device = 0; device < devicesCount; device++) {
      device_state[device] = !device_state[device];
      if (device_state[1] == true) {
        digitalWrite(devicesPins[1], HIGH);
        device_state[1] == true;
        if (EEPROM.read(1) == 0 && EEPROM.read(0) != 1) {
          EEPROM.write(0, 1);
          EEPROM.commit();
        }
      } else {
        digitalWrite(devicesPins[1], LOW);
        device_state[1] == false;
   
        if (EEPROM.read(1) == 0 && EEPROM.read(0) != 0) {
          EEPROM.write(0, 0);
          EEPROM.commit();
        }
      }
    }
  }
  lastButton2State = button2State;
}

void btn3read () {
  
  if (millis() < lastButton3Push + buttonThreshold) return; // check button only when the threshold after last push is reached
  lastButton3Push = millis();
  button3State = digitalRead(button3Pin);
  if (button3State == lastButton3State) return;
  if (button3State == HIGH) {
    for (uint8_t device = 0; device < devicesCount; device++) {
      device_state[device] = !device_state[device];
      if (device_state[2] == true) {
        digitalWrite(devicesPins[2], HIGH);
        device_state[2] == true;
        if (EEPROM.read(1) == 0 && EEPROM.read(0) != 1) {
          EEPROM.write(0, 1);
          EEPROM.commit();
        }
      } else {
        digitalWrite(devicesPins[2], LOW);
        device_state[2] == false;
     
        if (EEPROM.read(1) == 0 && EEPROM.read(0) != 0) {
          EEPROM.write(0, 0);
          EEPROM.commit();
        }
      }
    }
  }
  lastButton3State = button3State;
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  
  if (devicesCount == 1) {
   btn1read(); 
     }
     
  if (devicesCount == 2) {
   btn1read(); 
   btn2read();
     }
     
  if (devicesCount == 3) {
   btn1read(); 
   btn2read();
   btn3read();
     }
}
