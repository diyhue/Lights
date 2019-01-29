/*
  This can control bulbs with 5 pwm channels (red, gree, blue, warm white and could wihite). Is tested with MiLight colors bulb.
*/
#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>


//#define USE_STATIC_IP //! uncomment to enable Static IP Adress
#ifdef USE_STATIC_IP
IPAddress strip_ip ( 192,  168,   0,  95); // choose an unique IP Adress
IPAddress gateway_ip ( 192,  168,   0,   1); // Router IP
IPAddress subnet_mask(255, 255, 255,   0);
#endif

#define PWM_CHANNELS 5

//core
uint8_t colors[PWM_CHANNELS], bri, sat, color_mode;
bool light_state, in_transition;
int ct, hue;
float step_level[PWM_CHANNELS], current_colors[PWM_CHANNELS], x, y;
byte mac[6];
byte packetBuffer[8];

//settings
char *light_name = "New Hue RGB-CCT light";
uint8_t scene, startup, onPin, offPin, pins[PWM_CHANNELS]; //red, green, blue, could white, warm white
bool hwSwitch;

ESP8266WebServer server(80);
WiFiUDP Udp;

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

  colors[0] = (int) (r * optimal_bri); colors[1] = (int) (g * optimal_bri); colors[2] = (int) (b * optimal_bri); colors[3] = 0; colors[4] = 0;

}

void convert_ct() {

  int optimal_bri = int( 10 + bri / 1.04);

  colors[0] = 0;
  colors[1] = 0;
  colors[2] = 0;

  uint8 percent_warm = ((ct - 150) * 100) / 350;

  colors[3] =  (optimal_bri * (100 - percent_warm)) / 100;
  colors[4] = (optimal_bri * percent_warm) / 100;

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

void apply_scene(uint8_t new_scene) {
  if ( new_scene == 1) {
    bri = 254; ct = 346; color_mode = 2; convert_ct();
  } else if ( new_scene == 2) {
    bri = 254; ct = 233; color_mode = 2; convert_ct();
  }  else if ( new_scene == 3) {
    bri = 254; ct = 156; color_mode = 2; convert_ct();
  }  else if ( new_scene == 4) {
    bri = 77; ct = 367; color_mode = 2; convert_ct();
  }  else if ( new_scene == 5) {
    bri = 254; ct = 447; color_mode = 2; convert_ct();
  }  else if ( new_scene == 6) {
    bri = 1; x = 0.561; y = 0.4042; color_mode = 1; convert_xy();
  }  else if ( new_scene == 7) {
    bri = 203; x = 0.380328; y = 0.39986; color_mode = 1; convert_xy();
  }  else if ( new_scene == 8) {
    bri = 112; x = 0.359168; y = 0.28807; color_mode = 1; convert_xy();
  }  else if ( new_scene == 9) {
    bri = 142; x = 0.267102; y = 0.23755; color_mode = 1; convert_xy();
  }  else if ( new_scene == 10) {
    bri = 216; x = 0.393209; y = 0.29961; color_mode = 1; convert_xy();
  }  else {
    bri = 144; ct = 447; color_mode = 2; convert_ct();
  }
}

void process_lightdata(float transitiontime) {
  if (color_mode == 1 && light_state == true) {
    convert_xy();
  } else if (color_mode == 2 && light_state == true) {
    convert_ct();
  } else if (color_mode == 3 && light_state == true) {
    convert_hue();
  }
  transitiontime *= 16;
  for (uint8_t color = 0; color < PWM_CHANNELS; color++) {
    if (light_state) {
      step_level[color] = (colors[color] - current_colors[color]) / transitiontime;
    } else {
      step_level[color] = current_colors[color] / transitiontime;
    }
  }
}

void lightEngine() {
  for (uint8_t color = 0; color < PWM_CHANNELS; color++) {
    if (light_state) {
      if (colors[color] != current_colors[color] ) {
        in_transition = true;
        current_colors[color] += step_level[color];
        if ((step_level[color] > 0.0f && current_colors[color] > colors[color]) || (step_level[color] < 0.0f && current_colors[color] < colors[color])) current_colors[color] = colors[color];
        analogWrite(pins[color], (int)(current_colors[color] * 4.0));
      }
    } else {
      if (current_colors[color] != 0) {
        in_transition = true;
        current_colors[color] -= step_level[color];
        if (current_colors[color] < 0.0f) current_colors[color] = 0;
        analogWrite(pins[color], (int)(current_colors[color] * 4.0));
      }
    }
  }
  if (in_transition) {
    //wthis will be executed when one time when light is off in order to avoid the flash generated by eeprom.commit()
    if (!light_state && startup == 0 && current_colors[0] == 0 && current_colors[1] == 0 && current_colors[2] == 0 && current_colors[3] == 0 && current_colors[4] == 0) {
      //EEPROM.write(0, 0);
      //EEPROM.commit();
    }
    delay(6);
    in_transition = false;
  } else if (hwSwitch == true) {
    if (digitalRead(onPin) == HIGH) {
      int i = 0;
      while (digitalRead(onPin) == HIGH && i < 30) {
        delay(20);
        i++;
      }
      if (i < 30) {
        // there was a short press
        light_state = true;
      }
      else {
        // there was a long press
        bri += 56;
        if (bri > 254) {
          // don't increase the brightness more then maximum value
          bri = 254;
        }
      }
      process_lightdata(4);
    } else if (digitalRead(offPin) == HIGH) {
      int i = 0;
      while (digitalRead(offPin) == HIGH && i < 30) {
        delay(20);
        i++;
      }
      if (i < 30) {
        // there was a short press
        light_state = false;
      }
      else {
        // there was a long press
        bri -= 56;
        if (bri < 1) {
          // don't decrease the brightness less than minimum value.
          bri = 1;
        }
      }
      process_lightdata(4);
    }
  }
}

bool saveConfig() {
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["name"] = light_name;
  json["r"] = pins[0];
  json["g"] = pins[1];
  json["b"] = pins[2];
  json["c"] = pins[3];
  json["w"] = pins[4];
  json["on"] = onPin;
  json["off"] = offPin;
  json["hw"] = hwSwitch;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return false;
  }

  json.printTo(configFile);
  return true;
}

bool loadConfig() {
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Create new file with default values");
    light_name = "New Hue RGB-CCT light";
    pins[0] = 12; pins[1] = 13; pins[2] = 14; pins[3] = 4; pins[4] = 5; onPin = 0; offPin = 2; hwSwitch = false;
    return saveConfig();
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);

  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());

  if (!json.success()) {
    Serial.println("Failed to parse config file");
    return false;
  }

  strcpy(light_name, json["name"]);
  pins[0] = (uint8_t) json["r"];
  pins[1] = (uint8_t) json["g"];
  pins[2] = (uint8_t) json["b"];
  pins[3] = (uint8_t) json["c"];
  pins[4] = (uint8_t) json["w"];
  onPin = (uint8_t) json["on"];
  offPin = (uint8_t) json["off"];
  hwSwitch = json["hw"];

  //Serial.print("Name: ");
  //Serial.println(light_name);
  //Serial.println((String)pins[0] + ", " + (String)pins[1] + ", " + (String)pins[2] + ", " + (String)pins[3] + ", " + (String)pins[4]);
  return true;
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  delay(1000);

  Serial.println("mounting FS...");

  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  }

  if (!loadConfig()) {
    Serial.println("Failed to load config");
  } else {
    Serial.println("Config loaded");
  }

  for (uint8_t pin = 0; pin < PWM_CHANNELS; pin++) {
    pinMode(pins[pin], OUTPUT);
    analogWrite(pins[pin], 0);
  }

#ifdef USE_STATIC_IP
  WiFi.config(strip_ip, gateway_ip, subnet_mask);
#endif

  apply_scene(scene);
  step_level[0] = colors[0] / 150.0; step_level[1] = colors[1] / 150.0; step_level[2] = colors[2] / 150.0; step_level[3] = colors[3] / 150.0; step_level[4] = colors[4] / 150.0;

  if (startup == 1) {// || (startup == 0 && EEPROM.read(0) == 1)
    light_state = true;
    for (uint8_t i = 0; i < 200; i++) {
      lightEngine();
    }
  }
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.autoConnect(light_name);

  if (! light_state)  {
    // Show that we are connected
    analogWrite(pins[1], 100);
    delay(500);
    analogWrite(pins[1], 0);
  }
  WiFi.macAddress(mac);

  ArduinoOTA.setHostname(light_name);

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.begin();
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
        color_mode = 1;
      } else if (root.containsKey("ct")) {
        ct = root["ct"];
        color_mode = 2;
      } else {
        if (root.containsKey("hue")) {
          hue = root["hue"];
          color_mode = 3;
        }
        if (root.containsKey("sat")) {
          sat = root["sat"];
          color_mode = 3;
        }
      }

      if (root.containsKey("on")) {
        if (root["on"]) {
          if (startup == 0) { //&& EEPROM.read(0) != 1) {
            //EEPROM.write(0, 1);
            //EEPROM.commit();
          }
          light_state = true;
        } else {
          light_state = false;
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
        if (light_state) {
          current_colors[0] = 0; current_colors[1] = 0; current_colors[2] = 0; current_colors[3] = 0;
        } else {
          current_colors[0] = 255; current_colors[1] = 255; current_colors[2] = 255; current_colors[3] = 255;
        }
      }
      String output;
      root.printTo(output);
      server.send(200, "text/plain", output);
      process_lightdata(transitiontime);
    }
  });

  server.on("/get", []() {
    DynamicJsonBuffer newBuffer;
    JsonObject& root = newBuffer.createObject();

    root["on"] = light_state;
    root["bri"] = bri;
    JsonArray& xy = root.createNestedArray("xy");
    xy.add(x);
    xy.add(y);
    root["ct"] = ct;
    root["hue"] = hue;
    root["sat"] = sat;
    if (color_mode == 1)
      root["colormode"] = "xy";
    else if (color_mode == 2)
      root["colormode"] = "ct";
    else if (color_mode == 3)
      root["colormode"] = "hs";
    String output;
    root.printTo(output);
    server.send(200, "text/plain", output);
  });

  server.on("/detect", []() {
    DynamicJsonBuffer newBuffer;
    JsonObject& root = newBuffer.createObject();
    root["name"] = light_name;
    root["hue"] = "bulb";
    root["protocol"] = "native_single";
    root["lights"] = 1;
    root["modelid"] = "LCT015";
    root["mac"] = String(mac[5], HEX) + ":"  + String(mac[4], HEX) + ":" + String(mac[3], HEX) + ":" + String(mac[2], HEX) + ":" + String(mac[1], HEX) + ":" + String(mac[0], HEX);
    root["version"] = 2.0;
    String output;
    root.printTo(output);
    server.send(200, "text/plain", output);
  });

  server.on("/config", []() {
    DynamicJsonBuffer newBuffer;
    JsonObject& root = newBuffer.createObject();
    root["name"] = light_name;
    root["scene"] = scene;
    root["startup"] = startup;
    root["red"] = pins[0];
    root["green"] = pins[1];
    root["blue"] = pins[2];
    root["cw"] = pins[3];
    root["ww"] = pins[4];
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
      server.arg("name").toCharArray(light_name, server.arg("name").length() + 1);
      startup = server.arg("startup").toInt();
      scene = server.arg("scene").toInt();
      pins[0] = server.arg("red").toInt();
      pins[1] = server.arg("green").toInt();
      pins[2] = server.arg("blue").toInt();
      pins[3] = server.arg("cw").toInt();
      pins[4] = server.arg("ww").toInt();
      hwSwitch = server.arg("hwswitch").toInt();
      onPin = server.arg("on").toInt();
      offPin = server.arg("off").toInt();
      saveConfig();
    }

    const char * htmlContent = "<!DOCTYPE html><html><head> <meta charset=\"UTF-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"> <title>Hue Light</title> <link rel=\"stylesheet\" href=\"https://stackpath.bootstrapcdn.com/bootstrap/4.2.1/css/bootstrap.min.css\"> <link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/ion-rangeslider/2.3.0/css/ion.rangeSlider.min.css\" /> <script src=\"https://code.jquery.com/jquery-3.3.1.min.js\"></script> <script src=\"https://stackpath.bootstrapcdn.com/bootstrap/4.2.1/js/bootstrap.min.js\"></script> <script src=\"https://cdnjs.cloudflare.com/ajax/libs/ion-rangeslider/2.3.0/js/ion.rangeSlider.min.js\"></script></head><body> <nav class=\"navbar navbar-expand-lg navbar-light bg-light rounded\"> <button class=\"navbar-toggler\" type=\"button\" data-toggle=\"collapse\" data-target=\"#navbarToggler\" aria-controls=\"navbarToggler\" aria-expanded=\"false\" aria-label=\"Toggle navigation\"> <span class=\"navbar-toggler-icon\"></span> </button> <h2></h2> <div class=\"collapse navbar-collapse justify-content-md-center\" id=\"navbarToggler\"> <ul class=\"nav nav-pills\"> <li class=\"nav-item\"> <a class=\"nav-link active\" data-toggle=\"pill\" href=\"#home\">Home</a> </li> <li class=\"nav-item\"> <a class=\"nav-link\" data-toggle=\"pill\" href=\"#menu1\">Settings</a> </li> <li class=\"nav-item\"> <a class=\"nav-link\" data-toggle=\"pill\" href=\"#\" disabled> </a> </li> <li class=\"nav-item\"> <a class=\"nav-link\" data-toggle=\"pill\" href=\"#\" disabled> </a> </li> </ul> </div> </nav> <!-- Tab panes --> <div class=\"tab-content\"> <div class=\"tab-pane container active\" id=\"home\"> <br><br> <form> <div class=\"form-group row\"> <label for=\"power\" class=\"col-sm-2 col-form-label\">Power</label> <div class=\"col-sm-10\"> <div id=\"power\" class=\"btn-group\" role=\"group\"> <button type=\"button\" class=\"btn btn-default border\" id=\"power-on\">On</button> <button type=\"button\" class=\"btn btn-default border\" id=\"power-off\">Off</button> </div> </div> </div> <div class=\"form-group row\"> <label for=\"bri\" class=\"col-sm-2 col-form-label\">Brightness</label> <div class=\"col-sm-10\"> <input type=\"text\" id=\"bri\" class=\"js-range-slider\" name=\"bri\" value=\"\" /> </div> </div> <div class=\"form-group row\"> <label for=\"hue\" class=\"col-sm-2 col-form-label\">Color</label> <div class=\"col-sm-10\"> <canvas id=\"hue\" width=\"320px\" height=\"320px\" style=\"border:1px solid #d3d3d3;\"></canvas> </div> </div> <div class=\"form-group row\"> <label for=\"color\" class=\"col-sm-2 col-form-label\">Color Temp</label> <div class=\"col-sm-10\"> <canvas id=\"ct\" width=\"320px\" height=\"50px\" style=\"border:1px solid #d3d3d3;\"></canvas> </div> </div> </form> </div> <div class=\"tab-pane container fade\" id=\"menu1\"> <br> <form method=\"POST\" action=\"/\"> <div class=\"form-group row\"> <label for=\"name\" class=\"col-sm-2 col-form-label\">Light Name</label> <div class=\"col-sm-6\"> <input type=\"text\" class=\"form-control\" id=\"name\" name=\"name\"> </div> </div> <div class=\"form-group row\"> <label class=\"control-label col-sm-2\" for=\"startup\">Default Power:</label> <div class=\"col-sm-4\"> <select class=\"form-control\" name=\"startup\" id=\"startup\"> <option value=\"0\">Last State</option> <option value=\"1\">On</option> <option value=\"2\">Off</option> </select> </div> </div> <div class=\"form-group row\"> <label class=\"control-label col-sm-2\" for=\"scene\">Default Scene:</label> <div class=\"col-sm-4\"> <select class=\"form-control\" name=\"scene\" id=\"scene\"> <<option value=\"0\">Relax</option> <option value=\"1\">Read</option> <option value=\"2\">Concentrate</option> <option value=\"3\">Energize</option> <option value=\"4\">Bright</option> <option value=\"5\">Dimmed</option> <option value=\"6\">Nightlight</option> <option value=\"7\">Savanna sunset</option> <option value=\"8\">Tropical twilight</option> <option value=\"9\">Arctic aurora</option> <option value=\"10\">Spring blossom</option> </select> </div> </div> <div class=\"form-group row\"> <label for=\"red\" class=\"col-sm-2 col-form-label\">Red Pin</label> <div class=\"col-sm-3\"> <input type=\"number\" class=\"form-control\" id=\"red\" name=\"red\" placeholder=\"\"> </div> </div> <div class=\"form-group row\"> <label for=\"green\" class=\"col-sm-2 col-form-label\">Green Pin</label> <div class=\"col-sm-3\"> <input type=\"number\" class=\"form-control\" id=\"green\" name=\"green\" placeholder=\"\"> </div> </div> <div class=\"form-group row\"> <label for=\"blue\" class=\"col-sm-2 col-form-label\">Blue Pin</label> <div class=\"col-sm-3\"> <input type=\"number\" class=\"form-control\" id=\"blue\" name=\"blue\" placeholder=\"\"> </div> </div> <div class=\"form-group row\"> <label for=\"ww\" class=\"col-sm-2 col-form-label\">Warm W Pin</label> <div class=\"col-sm-3\"> <input type=\"number\" class=\"form-control\" id=\"ww\" name=\"ww\" placeholder=\"\"> </div> </div> <div class=\"form-group row\"> <label for=\"cw\" class=\"col-sm-2 col-form-label\">Cold W Pin</label> <div class=\"col-sm-3\"> <input type=\"number\" class=\"form-control\" id=\"cw\" name=\"cw\" placeholder=\"\"> </div> </div> <div class=\"form-group row\"> <label class=\"control-label col-sm-2\" for=\"hwswitch\">HW Switch:</label> <div class=\"col-sm-2\"> <select class=\"form-control\" name=\"hwswitch\" id=\"hwswitch\"> <option value=\"1\">Yes</option> <option value=\"0\">No</option> </select> </div> </div> <div class=\"form-group row\"> <label for=\"on\" class=\"col-sm-2 col-form-label\">On Pin</label> <div class=\"col-sm-3\"> <input type=\"number\" class=\"form-control\" id=\"on\" name=\"on\" placeholder=\"\"> </div> </div> <div class=\"form-group row\"> <label for=\"off\" class=\"col-sm-2 col-form-label\">Off Pin</label> <div class=\"col-sm-3\"> <input type=\"number\" class=\"form-control\" id=\"off\" name=\"off\" placeholder=\"\"> </div> </div> <div class=\"form-group row\"> <div class=\"col-sm-10\"> <button type=\"submit\" class=\"btn btn-primary\">Save</button> </div> </div> </form> </div> </div> <script>function postData(t){var e=new XMLHttpRequest;e.open(\"POST\",\"/state\",!0),e.setRequestHeader(\"Content-Type\",\"application/json\"),console.log(JSON.stringify(t)),e.send(JSON.stringify(t))}for(var $range=$(\".js-range-slider\").ionRangeSlider({min:1,max:100,from:1,step:1,skin:\"big\",onFinish:function(t){postData({bri:Math.floor(2.54*t.from)})}}),instance=$range.data(\"ionRangeSlider\"),context=(canvas=document.getElementById(\"hue\")).getContext(\"2d\"),x=canvas.width/2,y=canvas.height/2,radius=150,counterClockwise=!1,angle=0;angle<=360;angle+=1){var startAngle=(angle-2)*Math.PI/180,endAngle=angle*Math.PI/180;context.beginPath(),context.moveTo(x,y),context.arc(x,y,radius,startAngle,endAngle,counterClockwise),context.closePath();var gradient=context.createRadialGradient(x,y,20,x,y,radius);angle<270?(gradient.addColorStop(0,\"hsl(\"+(angle+90)+\", 20%, 100%)\"),gradient.addColorStop(1,\"hsl(\"+(angle+90)+\", 100%, 50%)\")):(gradient.addColorStop(0,\"hsl(\"+(angle-270)+\", 20%, 100%)\"),gradient.addColorStop(1,\"hsl(\"+(angle-270)+\", 100%, 50%)\")),context.fillStyle=gradient,context.fill()}var canvas,ctx=(canvas=document.getElementById(\"ct\")).getContext(\"2d\");function getPosition(t){var e=0,a=0;if(t.offsetParent){do{e+=t.offsetLeft,a+=t.offsetTop}while(t=t.offsetParent);return{x:e,y:a}}}function rgb_to_cie(t,e,a){var n=.664511*(t=t>.04045?Math.pow((t+.055)/1.055,2.4):t/12.92)+.154324*(e=e>.04045?Math.pow((e+.055)/1.055,2.4):e/12.92)+.162028*(a=a>.04045?Math.pow((a+.055)/1.055,2.4):a/12.92),o=.283881*t+.668433*e+.047685*a,r=88e-6*t+.07231*e+.986039*a,i=(n/(n+o+r)).toFixed(4),s=(o/(n+o+r)).toFixed(4);return isNaN(i)&&(i=0),isNaN(s)&&(s=0),[parseFloat(i),parseFloat(s)]}(gradient=ctx.createLinearGradient(20,0,300,0)).addColorStop(0,\"#ACEDFF\"),gradient.addColorStop(.5,\"#ffffff\"),gradient.addColorStop(1,\"#FEFFDE\"),ctx.fillStyle=gradient,ctx.fillRect(0,0,320,60),$(\"#hue\").click(function(t){var e=getPosition(this),a=t.pageX-e.x,n=t.pageY-e.y,o=this.getContext(\"2d\").getImageData(a,n,1,1).data;postData({xy:rgb_to_cie(o[0],o[1],o[2])})}),$(\"#ct\").click(function(t){var e=getPosition(this);postData({ct:t.pageX-e.x+153})}),$(\"#power-on\").click(function(){postData({on:!0}),$(\"#power-on\").addClass(\"btn-primary\"),$(\"#power-off\").removeClass(\"btn-primary\")}),$(\"#power-off\").click(function(){postData({on:!1}),$(\"#power-on\").removeClass(\"btn-primary\"),$(\"#power-off\").addClass(\"btn-primary\")}),$(document).ready(function(){jQuery.getJSON(\"/config\",function(t){$.each(t,function(t,e){$(\"#\"+t).val(e)}),$(\"h2\").text(t.name)}),function t(){jQuery.getJSON(\"/get\",function(t){t.on?($(\"#power-on\").addClass(\"btn-primary\"),$(\"#power-off\").removeClass(\"btn-primary\")):($(\"#power-on\").removeClass(\"btn-primary\"),$(\"#power-off\").addClass(\"btn-primary\")),instance.update({from:t.bri/2.54})}),setTimeout(t,2e3)}()}); </script></body></html>";
    server.send(200, "text/html", htmlContent);

  });

  server.on("/reset", []() {
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
  ArduinoOTA.handle();
  server.handleClient();
  lightEngine();
  entertainment();
}
