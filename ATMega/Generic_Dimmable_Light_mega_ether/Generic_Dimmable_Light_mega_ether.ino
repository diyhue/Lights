#include <NeoPixelBus.h>
#include <EthernetWebServer.h> //https://github.com/khoih-prog/EthernetWebServer
//Uncommand Ethernet.h if EthernetWebServer.h > version 2.0.0
//#include <Ethernet.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <avr/wdt.h>
#include <EEPROM.h>

//#define DEBUG

#define light_name "Dimmable Hue Light"  //default light name
#define LIGHT_VERSION 2.1

#define use_hardware_switch false // To control on/off state and brightness using GPIO/Pushbutton, set this value to true.
//For GPIO based on/off and brightness control, it is mandatory to connect the following GPIO pins to ground using 10k resistor
#define button1_pin 22 // on and brightness up
#define button2_pin 23 // off and brightness down

//define pins
#define LIGHTS_COUNT 10
uint8_t pins[LIGHTS_COUNT] = {2, 3, 5, 6, 7, 8, 9, 11, 12, 44};

//#define USE_STATIC_IP //! uncomment to enable Static IP Adress
#ifdef USE_STATIC_IP
IPAddress address ( 192,  168,   1,  95); // choose an unique IP Adress
IPAddress gateway ( 192,  168,   0,   1); // Router IP
IPAddress submask(255, 255, 255,   0);
#endif

uint8_t scene;
bool light_state[LIGHTS_COUNT], in_transition;
int transitiontime[LIGHTS_COUNT], bri[LIGHTS_COUNT];
float step_level[LIGHTS_COUNT], current_bri[LIGHTS_COUNT];
byte mac[] = {
  0xDE, 0xAD, 0xEB, 0xFF, 0xEF, 0xDE
};

EthernetWebServer server(80);
EthernetUDP Udp;

void softwareReset( uint8_t prescaller) {
  wdt_enable( prescaller);
  while (1) {}
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


void apply_scene(uint8_t new_scene,  uint8_t light) {
  if ( new_scene == 0) {
    bri[light] = 144;
  } else if ( new_scene == 1) {
    bri[light] = 254;
  } else if ( new_scene == 2) {
    bri[light] = 1;
  }
}

void process_lightdata(uint8_t light, float transitiontime) {
  transitiontime *= 16;
  if (light_state[light]) {
    step_level[light] = (bri[light] - current_bri[light]) / transitiontime;
  } else {
    step_level[light] = current_bri[light] / transitiontime;
  }
}

void lightEngine() {
  for (int i = 0; i < LIGHTS_COUNT; i++) {
    if (light_state[i]) {
      if (bri[i] != current_bri[i]) {
        in_transition = true;
        current_bri[i] += step_level[i];
        if ((step_level[i] > 0.0 && current_bri[i] > bri[i]) || (step_level[i] < 0.0 && current_bri[i] < bri[i])) {
          current_bri[i] = bri[i];
        }
        analogWrite(pins[i], (int)(current_bri[i]));
#ifdef DEBUG
        Serial.print(F("current_bri["));
        Serial.print(i);
        Serial.print(F("] : "));
        Serial.println(current_bri[i]);
#endif
      }
    } else {
      if (current_bri[i] != 0 ) {
        in_transition = true;
        current_bri[i] -= step_level[i];
        if (current_bri[i] < 0) {
          current_bri[i] = 0;
        }
        analogWrite(pins[i], (int)(current_bri[i]));
#ifdef DEBUG
        Serial.print(F("else current_bri["));
        Serial.print(i);
        Serial.print(F("] : "));
        Serial.println(current_bri[i]);
#endif
      }
    }
  }
  if (in_transition) {
    delay(6);
    in_transition = false;
  } else if (use_hardware_switch == true) {
    if (digitalRead(button1_pin) == HIGH) {
      int i = 0;
      while (digitalRead(button1_pin) == HIGH && i < 30) {
        delay(20);
        i++;
      }
      for (int light = 0; light < LIGHTS_COUNT; light++) {
        if (i < 30) {
          // there was a short press
          light_state[light] = true;
        }
        else {
          // there was a long press
          bri[light] += 56;
          if (bri[light] > 254) {
            // don't increase the brightness more then maximum value
            bri[light] = 254;
          }
        }
        process_lightdata(light, 4);
      }
    } else if (digitalRead(button2_pin) == HIGH) {
      int i = 0;
      while (digitalRead(button2_pin) == HIGH && i < 30) {
        delay(20);
        i++;
      }
      for (int light = 0; light < LIGHTS_COUNT; light++) {
        if (i < 30) {
          // there was a short press
          light_state[light] = false;
        }
        else {
          // there was a long press
          bri[light] -= 56;
          if (bri[light] < 1) {
            // don't decrease the brightness less than minimum value.
            bri[light] = 1;
          }
        }
        process_lightdata(light, 4);
      }
    }
  }
}

void setup() {
  digitalWrite(4, HIGH);
  delay(1000);

#ifdef USE_STATIC_IP
  //Serial.println(F("use static Ip"));
  Ethernet.begin(mac, address);
  //wifiManager.setSTAStaticIPConfig(address, gateway, submask);
#else
  Ethernet.begin(mac);
#endif

#ifdef DEBUG
  Serial.begin(115200);
  Serial.println();
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
    while (true) {
      delay(1); // do nothing, no point running without Ethernet hardware
    }
  }
  if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println("Ethernet cable is not connected.");
  }
  // Just info to know how to connect correctly
  Serial.println(F("========================="));
  Serial.println(F("Currently Used SPI pinout:"));
  Serial.print(F("MOSI:"));
  Serial.println(MOSI);
  Serial.print(F("MISO:"));
  Serial.println(MISO);
  Serial.print(F("SCK:"));
  Serial.println(SCK);
  Serial.print(F("SS:"));
  Serial.println(SS);
  Serial.println(F("========================="));

  byte macBuffer[6];  // create a buffer to hold the MAC address
  Ethernet.MACAddress(macBuffer); // fill the buffer
  Serial.print(F("The MAC address is: "));
  for (byte octet = 0; octet < 6; octet++) {
    Serial.print(macBuffer[octet], HEX);
    if (octet < 5) {
      Serial.print('-');
    }
  }
  Serial.println();

  Serial.print(F("Connected! IP address: "));
  Serial.println(Ethernet.localIP());
#endif

  for (uint8_t light = 0; light < LIGHTS_COUNT; light++) {
    apply_scene(EEPROM.read(2), light);
    step_level[light] = bri[light] / 150.0;
  }

  if (EEPROM.read(1) == 1 || (EEPROM.read(1) == 0 && EEPROM.read(0) == 1)) {
    for (int i = 0; i < LIGHTS_COUNT; i++) {
      light_state[i] = true;
    }
    for (int j = 0; j < 200; j++) {
      lightEngine();
    }
  }

  if (! light_state[0])  {
    // Show that we are connected
    analogWrite(pins[0], 100);
    delay(500);
    analogWrite(pins[0], 0);
  }

  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off by making the voltage HIGH

  if (use_hardware_switch == true) {
    pinMode(button1_pin, INPUT);
    pinMode(button2_pin, INPUT);
  }

  server.on("/state", HTTP_PUT, []() {
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
            if (EEPROM.read(1) == 0 && EEPROM.read(0) == 0) {
              EEPROM.write(0, 1);
            }
          } else {
            light_state[light] = false;
            if (EEPROM.read(1) == 0 && EEPROM.read(0) == 1) {
              EEPROM.write(0, 0);
            }
          }
        }

        if (values.containsKey("bri")) {
          bri[light] = values["bri"];
        }

        if (values.containsKey("bri_inc")) {
          bri[light] += (int) values["bri_inc"];
          if (bri[light] > 255) bri[light] = 255;
          else if (bri[light] < 1) bri[light] = 1;
        }

        if (values.containsKey("transitiontime")) {
          transitiontime = values["transitiontime"];
        }
        process_lightdata(light, transitiontime);
      }
      String output;
      serializeJson(root, output);
#ifdef DEBUG
      Serial.println(output);
#endif
      server.send(200, "text/plain", output);
    }
  });

  server.on("/state", HTTP_GET, []() {
    uint8_t light = server.arg("light").toInt() - 1;
    DynamicJsonDocument root(1024);
    root["on"] = light_state[light];
    root["bri"] = bri[light];
    String output;
    serializeJson(root, output);
#ifdef DEBUG
    Serial.println(output);
#endif
    server.send(200, "text/plain", output);
  });

  server.on("/detect", []() {
    char macString[32] = {0};
    sprintf(macString, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    DynamicJsonDocument root(1024);
    root["name"] = light_name;
    root["lights"] = LIGHTS_COUNT;
    root["protocol"] = "native_multi";
    root["modelid"] = "LWB010";
    root["type"] = "dimmable_light";
    root["mac"] = String(macString);
    root["version"] = LIGHT_VERSION;
    String output;
    serializeJson(root, output);
#ifdef DEBUG
    Serial.println(output);
#endif
    server.send(200, "text/plain", output);
  });

  server.on("/", []() {
    float transitiontime = 4;
    if (server.hasArg("startup")) {
      if (  EEPROM.read(1) != server.arg("startup").toInt()) {
        EEPROM.write(1, server.arg("startup").toInt());
      }
    }

    for (int light = 0; light < LIGHTS_COUNT; light++) {
      if (server.hasArg("scene")) {
        if (server.arg("bri") == "" && server.arg("hue") == "" && server.arg("ct") == "" && server.arg("sat") == "") {
          if (  EEPROM.read(2) != server.arg("scene").toInt()) {
            EEPROM.write(2, server.arg("scene").toInt());
          }
          apply_scene(server.arg("scene").toInt(), light);
        } else {
          if (server.arg("bri") != "") {
            bri[light] = server.arg("bri").toInt();
          }
        }
      } else if (server.hasArg("on")) {
        if (server.arg("on") == "true") {
          light_state[light] = true; {
            if (EEPROM.read(1) == 0 && EEPROM.read(0) == 0) {
              EEPROM.write(0, 1);
            }
          }
        } else {
          light_state[light] = false;
          if (EEPROM.read(1) == 0 && EEPROM.read(0) == 1) {
            EEPROM.write(0, 0);
          }
        }
      } else if (server.hasArg("alert")) {
        if (light_state[light]) {
          current_bri[light] = 0;
        } else {
          current_bri[light] = 255;
        }
      }
      if (light_state[light]) {
        step_level[light] = ((float)bri[light] - current_bri[light]) / transitiontime;
      } else {
        step_level[light] = current_bri[light] / transitiontime;
      }
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
    http_content += "<div class=\"pure-control-group\">";
    http_content += "<label for=\"startup\">Startup</label>";
    http_content += "<select onchange=\"this.form.submit()\" id=\"startup\" name=\"startup\">";
    http_content += "<option "; if (EEPROM.read(1) == 0) http_content += "selected=\"selected\""; http_content += " value=\"0\">Last state</option>";
    http_content += "<option "; if (EEPROM.read(1) == 1) http_content += "selected=\"selected\""; http_content += " value=\"1\">On</option>";
    http_content += "<option "; if (EEPROM.read(1) == 2) http_content += "selected=\"selected\""; http_content += " value=\"2\">Off</option>";
    http_content += "</select>";
    http_content += "</div>";
    http_content += "<div class=\"pure-control-group\">";
    http_content += "<label for=\"scene\">Scene</label>";
    http_content += "<select onchange = \"this.form.submit()\" id=\"scene\" name=\"scene\">";
    http_content += "<option "; if (EEPROM.read(2) == 0) http_content += "selected=\"selected\""; http_content += " value=\"0\">Relax</option>";
    http_content += "<option "; if (EEPROM.read(2) == 1) http_content += "selected=\"selected\""; http_content += " value=\"1\">Bright</option>";
    http_content += "<option "; if (EEPROM.read(2) == 2) http_content += "selected=\"selected\""; http_content += " value=\"2\">Nightly</option>";
    http_content += "</select>";
    http_content += "</div>";
    http_content += "<br>";
    http_content += "<div class=\"pure-control-group\">";
    http_content += "<label for=\"state\"><strong>State</strong></label>";
    http_content += "</div>";
    http_content += "<div class=\"pure-control-group\">";
    http_content += "<label for=\"bri\">Bri</label>";
    http_content += "<input id=\"bri\" name=\"bri\" type=\"text\" placeholder=\"" + (String)bri[0] + "\">";
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

  server.on("/reset", []() {
    server.send(200, "text/html", "reset");
    softwareReset( WDTO_1S);
  });

  server.onNotFound(handleNotFound);

  server.begin();
}

void loop() {
  server.handleClient();
  lightEngine();
}
