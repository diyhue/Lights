#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <FastLED.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include "Arduino.h"
#include <ArduinoJson.h>

// default Setting (try to do not cange ;-) )
#define MODEL_ID "LST002" //in case of an RGB Stripe (alias: Philips Hue lightstrip plus)
#define LIGHT_NAME_MAX_LENGTH 32 // Longer name will get stripped
#define LIGHT_VERSION 3.1 // in case you are using LST002

// Start individually Settings:
// ---------------------------
//Setting:  Naming
char lightName[LIGHT_NAME_MAX_LENGTH] = "Livingroom LED"; //default light name

//Setting:  Network
bool useDhcp = true;
//Only usefull in case you will a static-IP
IPAddress address(192, 168, 0, 95); // choose an unique IP Adress
IPAddress gateway(192, 168, 0, 1); // Router IP
IPAddress submask(255, 255, 255, 0);

//Setting:  FastLED
// Use Correction from fastLED library or not
#define USE_F_LED_CC true
// How many LED Colors are there including CW and/or WW
// fastLED only controls rgb, not w
#define LED_COLORS 3
#define NUM_LEDS 1 // 1 led with 3 colors - thanks to CRGB
uint8_t lightsCount = 1;
// FastLED settings, data and clock pin for spi communication
// Note that the protocol for SM16716 is the same for the SM16726
#define DATA_PIN 13
#define CLOCK_PIN 12
#define COLOR_ORDER RGB
#define LED_TYPE P9813
#define CORRECTION TypicalSMD5050

//Setting:  HardwareSwitch
// May get messed up with SPI CLOCK_PIN with this particular bulb
bool hwSwitch = false; // To control on/off state and brightness using GPIO/Pushbutton, set this value to true.
//For GPIO based on/off and brightness control, it is mandatory to connect the following GPIO pins to ground using 10k resistor
uint8_t onPin = 4, offPin = 5; // off and brightness down

//Setting:  RGB+W
// !!!!!!!! Experimental !!!!!!!!!!
// True - add cold white LEDs according to luminance/ whiteness in xy color selector
// False - Don't
#define W_ON_XY false

//Setting:  PWM-Pins
// How many colors are controlled by basic PWM, not fastLED
#define PWM_CHANNELS 1
uint8_t pins[PWM_CHANNELS] = {5};

// ------------------
// -- End of Settings

//Every light has this attributes:
// I will interate with lights[light] or lights[i]
// for colors etc it will be colors[color] or colors[j] (see also stepLevel and currentColors)
struct state {
    uint8_t colors[3], bri = 100, sat = 254, colorMode = 2;
    bool lightState;
    int ct = 200, hue;
    float stepLevel[3], currentColors[3], x, y;
};

//Let's create the Lights! 10 -> 10 Attributes (starts with 0)
state lights[10]; // unconverted Hue-Lights
// Set up array for use by FastLED
CRGB leds[NUM_LEDS]; // converted from Hue in RGB

// InfoLight doesn't seem to work...
CRGB red = CRGB(255, 0, 0);
CRGB green = CRGB(0, 255, 0);
CRGB white = CRGB(255, 255, 255);
CRGB black = CRGB(0, 0, 0);

byte mac[6]; //mac-address
int transitiontime;
bool in_transition;
uint8_t scene, startup;
uint8_t rgb_multiplier[] = {100, 100, 100}; // light multiplier in percentage /R, G, B/

ESP8266WebServer server(80);

void convertHue(uint8_t light) {
    double hh, p, q, t, ff, s, v;
    long i;

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
    i = (long) hh;
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
}

void convertXy(uint8_t light) {
    int optimal_bri = lights[light].bri;
    if (optimal_bri < 5) {
        optimal_bri = 5;
    }
    float Y = lights[light].y;
    float X = lights[light].x;
    float Z = 1.0f - lights[light].x - lights[light].y;

    // sRGB D65 conversion
    float r = X * 3.2406f - Y * 1.5372f - Z * 0.4986f;
    float g = -X * 0.9689f + Y * 1.8758f + Z * 0.0415f;
    float b = X * 0.0557f - Y * 0.2040f + Z * 1.0570f;


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
    } else if (g > b && g > r) {
        // green is biggest
        if (g > 1.0f) {
            r = r / g;
            b = b / g;
            g = 1.0f;
        }
    } else if (b > r && b > g) {
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

    lights[light].colors[0] = (int) (r * optimal_bri);
    lights[light].colors[1] = (int) (g * optimal_bri);
    lights[light].colors[2] = (int) (b * optimal_bri);
}

void convertCt(uint8_t light) {
    int hectemp = 10000 / lights[light].ct;
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

    lights[light].colors[0] = r * (lights[light].bri / 255.0f);
    lights[light].colors[1] = g * (lights[light].bri / 255.0f);
    lights[light].colors[2] = b * (lights[light].bri / 255.0f);
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

void infoLight(CRGB color) {
    // Flash the strip in the selected color.
    // White = booted, green = WLAN connected, red = WLAN could not connect
    for (int i = 0; i < lightsCount; i++) {
        //Will fill up color from frist to last Led
        leds[i] = color;
        FastLED.show();
        FastLED.delay(10); //in ms | Change to 500 or higher for better feedback (in debugging) with small count of LED
    }
    for (int i = lightsCount; i >= 0; i--) {
        //Will remove color from last to first Led
        leds[i] = CRGB::Black;
        FastLED.show();
        FastLED.delay(10); //in ms | Change to 500 or higher for better feedback (in debugging)
    }
}

// Possible Hue Scenes. You can change it if you want.
// The following is "default"
void apply_scene(uint8_t new_scene) {
    for (uint8_t light = 0; light < lightsCount; light++) {
        if (new_scene == 1) {
            lights[light].bri = 254;
            lights[light].ct = 346;
            lights[light].colorMode = 2;
            convertCt(light);
        } else if (new_scene == 2) {
            lights[light].bri = 254;
            lights[light].ct = 233;
            lights[light].colorMode = 2;
            convertCt(light);
        } else if (new_scene == 3) {
            lights[light].bri = 254;
            lights[light].ct = 156;
            lights[light].colorMode = 2;
            convertCt(light);
        } else if (new_scene == 4) {
            lights[light].bri = 77;
            lights[light].ct = 367;
            lights[light].colorMode = 2;
            convertCt(light);
        } else if (new_scene == 5) {
            lights[light].bri = 254;
            lights[light].ct = 447;
            lights[light].colorMode = 2;
            convertCt(light);
        } else if (new_scene == 6) {
            lights[light].bri = 1;
            lights[light].x = 0.561;
            lights[light].y = 0.4042;
            lights[light].colorMode = 1;
            convertXy(light);
        } else if (new_scene == 7) {
            lights[light].bri = 203;
            lights[light].x = 0.380328;
            lights[light].y = 0.39986;
            lights[light].colorMode = 1;
            convertXy(light);
        } else if (new_scene == 8) {
            lights[light].bri = 112;
            lights[light].x = 0.359168;
            lights[light].y = 0.28807;
            lights[light].colorMode = 1;
            convertXy(light);
        } else if (new_scene == 9) {
            lights[light].bri = 142;
            lights[light].x = 0.267102;
            lights[light].y = 0.23755;
            lights[light].colorMode = 1;
            convertXy(light);
        } else if (new_scene == 10) {
            lights[light].bri = 216;
            lights[light].x = 0.393209;
            lights[light].y = 0.29961;
            lights[light].colorMode = 1;
            convertXy(light);
        } else {
            lights[light].bri = 144;
            lights[light].ct = 447;
            lights[light].colorMode = 2;
            convertCt(light);
        }
    }
}

void processLightdata(uint8_t light, float transitiontime) {
    transitiontime *= 17 - (lightsCount / 40); //every extra led add a small delay that need to be counted
    if (lights[light].colorMode == 1 && lights[light].lightState) {
        convertXy(light);
    } else if (lights[light].colorMode == 2 && lights[light].lightState) {
        convertCt(light);
    } else if (lights[light].colorMode == 3 && lights[light].lightState) {
        convertHue(light);
    }
    for (uint8_t i = 0; i < 3; i++) {
        if (lights[light].lightState) {
            lights[light].stepLevel[i] =
                    ((float) lights[light].colors[i] - lights[light].currentColors[i]) / transitiontime;
        } else {
            lights[light].stepLevel[i] = lights[light].currentColors[i] / transitiontime;
        }
    }
}

void lightEngine() {
    for (int i = 0; i < lightsCount; i++) {
        for (uint8_t color = 0; color < LED_COLORS; color++) {
            if (lights[i].lightState) {
                if (lights[i].colors[color] != lights[i].currentColors[color]) {
                    in_transition = true;
                    lights[i].currentColors[color] += lights[i].stepLevel[color];
                    if ((lights[i].stepLevel[color] > 0.0f &&
                         lights[i].currentColors[color] > lights[i].colors[color]) ||
                        (lights[i].stepLevel[color] < 0.0f &&
                         lights[i].currentColors[color] < lights[i].colors[color])) {
                        lights[i].currentColors[color] = lights[i].colors[color];
                    }
                    leds[i] = CRGB((int) lights[i].currentColors[0], (int) lights[i].currentColors[1],
                                   (int) lights[i].currentColors[2]);

                    FastLED.show();
                    //The '4' or 'W' Color in case of RGBW (FastLED doesn't support 'W')
                    analogWrite(pins[0], (int) (lights[i].currentColors[3]));
                }
            } else {
                if (lights[i].currentColors[color] != 0) {
                    in_transition = true;
                    lights[i].currentColors[color] -= lights[i].stepLevel[color];
                    if (lights[i].currentColors[color] < 0.0f) {
                        lights[i].currentColors[color] = 0;
                    }
                    leds[i] = CRGB((int) lights[i].currentColors[0], (int) lights[i].currentColors[1],
                                   (int) lights[i].currentColors[2]);

                    FastLED.show();
                    //The '4' or 'W' Color in case of RGBW (FastLED doesn't support 'W')
                    analogWrite(pins[0], (int) (lights[i].currentColors[3]));
                }
            }
        }
        if (in_transition) {
            FastLED.delay(6);
            in_transition = false;
        }
    }
}

void saveState() {
    DynamicJsonDocument json(1024);
    for (uint8_t i = 0; i < lightsCount; i++) {
        JsonObject light = json.createNestedObject((String) i);
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
    for (JsonPair state : json.as<JsonObject>()) {
        const char *key = state.key().c_str();
        int lightId = atoi(key);
        JsonObject values = state.value();
        lights[lightId].lightState = values["on"];
        lights[lightId].bri = (uint8_t) values["bri"];
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

void setup() {
    Serial.begin(115200);
    Serial.println();
    delay(1000);

    if (USE_F_LED_CC) {
        FastLED.addLeds<LED_TYPE, DATA_PIN, CLOCK_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(CORRECTION);
    } else {
        FastLED.addLeds<LED_TYPE, DATA_PIN, CLOCK_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    }
    EEPROM.begin(512);

    analogWriteFreq(1000);
    analogWriteRange(255);

    for (uint8_t i = 0; i < lightsCount; i++) {
        float transitiontime = (17 - (NUM_LEDS / 40)) * 4;
        apply_scene(EEPROM.read(2));
        for (uint8_t j = 0; j < 3; j++) {
            lights[i].stepLevel[j] = ((float) lights[i].colors[j] - lights[i].currentColors[j]) / transitiontime;
        }
    }

    if (EEPROM.read(1) == 1 || (EEPROM.read(1) == 0 && EEPROM.read(0) == 1)) {
        for (int i = 0; i < NUM_LEDS; i++) {
            lights[i].lightState = true;
        }
        for (uint8_t j = 0; j < 200; j++) {
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

    if (!lights[0].lightState) {
        infoLight(white);
        while (WiFi.status() != WL_CONNECTED) {
            infoLight(red);
            delay(500);
        }
        // Show that we are connected
        infoLight(green);
    }

    WiFi.macAddress(mac);

    // Port defaults to 8266
    // ArduinoOTA.setPort(8266);

    // Hostname defaults to esp8266-[ChipID]
    // ArduinoOTA.setHostname("myesp8266");

    // No authentication by default
    // ArduinoOTA.setPassword((const char *)"123");

    ArduinoOTA.begin();

    pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
    digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off by making the voltage HIGH
    //init Hardware-Switches
    if (hwSwitch) {
        pinMode(onPin, INPUT);
        pinMode(offPin, INPUT);
    }

    //used for both models
    server.on("/detect", []() {
        char macString[32] = {0};
        sprintf(macString, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        DynamicJsonDocument root(1024);
        root["name"] = lightName;
        root["lights"] = (int) NUM_LEDS;
        root["protocol"] = "native_multi";
        root["modelid"] = MODEL_ID;
        root["type"] = "ws2812_strip";
        root["version"] = LIGHT_VERSION;
        root["mac"] = String(macString);

        String output;
        serializeJson(root, output);
        server.send(200, "text/plain", output);
    });

    server.on("/state", HTTP_PUT, []() {
        bool stateSave = false;
        DynamicJsonDocument root(1024);
        DeserializationError error = deserializeJson(root, server.arg("plain"));

        if (error) {
            server.send(404, "text/plain", "FAIL. " + server.arg("plain"));
        } else {
            for (JsonPair state : root.as<JsonObject>()) {
                const char *key = state.key().c_str();
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
                        lights[light].currentColors[0] = 0;
                        lights[light].currentColors[1] = 0;
                        lights[light].currentColors[2] = 0;
                    } else {
                        lights[light].currentColors[1] = 126;
                        lights[light].currentColors[2] = 126;
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

    server.on("/state", HTTP_GET, []() {
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

    server.on("/config", []() {
        DynamicJsonDocument root(1024);
        root["name"] = lightName;
        root["scene"] = scene;
        root["startup"] = startup;
        root["hw"] = hwSwitch;
        root["on"] = onPin;
        root["off"] = offPin;
        root["hwswitch"] = (int) hwSwitch;
        root["lightscount"] = lightsCount;
//          Not Supported (pixelCount and lightscount are the same)
//        root["pixelcount"] = pixelcount;
//        root["transitionleds"] = transitionLeds;
        root["rpct"] = rgb_multiplier[0];
        root["gpct"] = rgb_multiplier[1];
        root["bpct"] = rgb_multiplier[2];
        root["disdhcp"] = (int) !useDhcp;
        root["addr"] =
                (String) address[0] + "." + (String) address[1] + "." + (String) address[2] + "." +
                (String) address[3];
        root["gw"] =
                (String) gateway[0] + "." + (String) gateway[1] + "." + (String) gateway[2] + "." + (String) gateway[3];
        root["sm"] =
                (String) submask[0] + "." + (String) submask[1] + "." + (String) submask[2] + "." + (String) submask[3];
        String output;
        serializeJson(root, output);
        server.send(200, "text/plain", output);
    });

    server.on("/", []() {
        if (server.arg("section").toInt() == 1) {
            server.arg("name").toCharArray(lightName, LIGHT_NAME_MAX_LENGTH);
            startup = server.arg("startup").toInt();
            scene = server.arg("scene").toInt();
            lightsCount = server.arg("lightscount").toInt();
//              Not Supported (pixelCount and lightscount are the same)
//            pixelCount = server.arg("pixelcount").toInt();
//            transitionLeds = server.arg("transitionleds").toInt();
            rgb_multiplier[0] = server.arg("rpct").toInt();
            rgb_multiplier[1] = server.arg("gpct").toInt();
            rgb_multiplier[2] = server.arg("bpct").toInt();
            hwSwitch = server.hasArg("hwswitch") ? server.arg("hwswitch").toInt() : 0;
            if (server.hasArg("hwswitch")) {
                onPin = server.arg("on").toInt();
                offPin = server.arg("off").toInt();
            }
        } else if (server.arg("section").toInt() == 2) {
            useDhcp = (!server.hasArg("disdhcp")) ? 1 : server.arg("disdhcp").toInt();
            if (server.hasArg("disdhcp")) {
                address.fromString(server.arg("addr"));
                gateway.fromString(server.arg("gw"));
                submask.fromString(server.arg("sm"));
            }
        }

        String htmlContent = "<!DOCTYPE html> <html> <head> <meta charset=\"UTF-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"> <title>Hue Light</title> <link href=\"https://fonts.googleapis.com/icon?family=Material+Icons\" rel=\"stylesheet\"> <link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/css/materialize.min.css\"> <link rel=\"stylesheet\" href=\"https://cerny.in/nouislider.css\"/> </head> <body> <div class=\"wrapper\"> <nav class=\"nav-extended row deep-purple\"> <div class=\"nav-wrapper col s12\"> <a href=\"#\" class=\"brand-logo\">DiyHue</a> <ul id=\"nav-mobile\" class=\"right hide-on-med-and-down\" style=\"position: relative;z-index: 10;\"> <li><a target=\"_blank\" href=\"https://github.com/diyhue\"><i class=\"material-icons left\">language</i>GitHub</a></li> <li><a target=\"_blank\" href=\"https://diyhue.readthedocs.io/en/latest/\"><i class=\"material-icons left\">description</i>Documentation</a></li> <li><a target=\"_blank\" href=\"https://diyhue.slack.com/\" ><i class=\"material-icons left\">question_answer</i>Slack channel</a></li> </ul> </div> <div class=\"nav-content\"> <ul class=\"tabs tabs-transparent\"> <li class=\"tab\"><a class=\"active\" href=\"#test1\">Home</a></li> <li class=\"tab\"><a href=\"#test2\">Preferences</a></li> <li class=\"tab\"><a href=\"#test3\">Network settings</a></li> </ul> </div> </nav> <ul class=\"sidenav\" id=\"mobile-demo\"> <li><a target=\"_blank\" href=\"https://github.com/diyhue\">GitHub</a></li> <li><a target=\"_blank\" href=\"https://diyhue.readthedocs.io/en/latest/\">Documentation</a></li> <li><a target=\"_blank\" href=\"https://diyhue.slack.com/\" >Slack channel</a></li> </ul> <div class=\"container\"> <div class=\"section\"> <div id=\"test1\" class=\"col s12\"> <form> <input type=\"hidden\" name=\"section\" value=\"1\"> <div class=\"row\"> <div class=\"col s10\"> <label for=\"power\">Power</label> <div id=\"power\" class=\"switch section\"> <label> Off <input type=\"checkbox\" name=\"pow\" id=\"pow\" value=\"1\"> <span class=\"lever\"></span> On </label> </div> </div> </div> <div class=\"row\"> <div class=\"col s12 m10\"> <label for=\"bri\">Brightness</label> <input type=\"text\" id=\"bri\" class=\"js-range-slider\" name=\"bri\" value=\"\"/> </div> </div> <div class=\"row\"> <div class=\"col s12\"> <label for=\"hue\">Color</label> <div> <canvas id=\"hue\" width=\"320px\" height=\"320px\" style=\"border:1px solid #d3d3d3;\"></canvas> </div> </div> </div> <div class=\"row\"> <div class=\"col s12\"> <label for=\"ct\">Color Temp</label> <div> <canvas id=\"ct\" width=\"320px\" height=\"50px\" style=\"border:1px solid #d3d3d3;\"></canvas> </div> </div> </div> </form> </div> <div id=\"test2\" class=\"col s12\"> <form method=\"POST\" action=\"/\"> <input type=\"hidden\" name=\"section\" value=\"1\"> <div class=\"row\"> <div class=\"col s12\"> <label for=\"name\">Light Name</label> <input type=\"text\" id=\"name\" name=\"name\"> </div> </div> <div class=\"row\"> <div class=\"col s12 m6\"> <label for=\"startup\">Default Power:</label> <select name=\"startup\" id=\"startup\"> <option value=\"0\">Last State</option> <option value=\"1\">On</option> <option value=\"2\">Off</option> </select> </div> </div> <div class=\"row\"> <div class=\"col s12 m6\"> <label for=\"scene\">Default Scene:</label> <select name=\"scene\" id=\"scene\"> <option value=\"0\">Relax</option> <option value=\"1\">Read</option> <option value=\"2\">Concentrate</option> <option value=\"3\">Energize</option> <option value=\"4\">Bright</option> <option value=\"5\">Dimmed</option> <option value=\"6\">Nightlight</option> <option value=\"7\">Savanna sunset</option> <option value=\"8\">Tropical twilight</option> <option value=\"9\">Arctic aurora</option> <option value=\"10\">Spring blossom</option> </select> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"lightscount\" class=\"col-form-label\">Lights count</label> <input type=\"number\" id=\"lightscount\" name=\"lightscount\"> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"rpct\" class=\"form-label\">Red multiplier</label> <input type=\"number\" id=\"rpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"rpct\" value=\"\"/> </div> <div class=\"col s4 m3\"> <label for=\"gpct\" class=\"form-label\">Green multiplier</label> <input type=\"number\" id=\"gpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"gpct\" value=\"\"/> </div> <div class=\"col s4 m3\"> <label for=\"bpct\" class=\"form-label\">Blue multiplier</label> <input type=\"number\" id=\"bpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"bpct\" value=\"\"/> </div> </div> <div class=\"row\"><label class=\"control-label col s10\">HW buttons:</label> <div class=\"col s10\"> <div class=\"switch section\"> <label> Disable <input type=\"checkbox\" name=\"hwswitch\" id=\"hwswitch\" value=\"1\"> <span class=\"lever\"></span> Enable </label> </div> </div> </div> <div class=\"switchable\"> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"on\">On Pin</label> <input type=\"number\" id=\"on\" name=\"on\"> </div> <div class=\"col s4 m3\"> <label for=\"off\">Off Pin</label> <input type=\"number\" id=\"off\" name=\"off\"> </div> </div> </div> <div class=\"row\"> <div class=\"col s10\"> <button type=\"submit\" class=\"waves-effect waves-light btn teal\">Save</button> <!--<button type=\"submit\" name=\"reboot\" class=\"waves-effect waves-light btn grey lighten-1\">Reboot</button>--> </div> </div> </form> </div> <div id=\"test3\" class=\"col s12\"> <form method=\"POST\" action=\"/\"> <input type=\"hidden\" name=\"section\" value=\"2\"> <div class=\"row\"> <div class=\"col s12\"> <label class=\"control-label\">Manual IP assignment:</label> <div class=\"switch section\"> <label> Disable <input type=\"checkbox\" name=\"disdhcp\" id=\"disdhcp\" value=\"0\"> <span class=\"lever\"></span> Enable </label> </div> </div> </div> <div class=\"switchable\"> <div class=\"row\"> <div class=\"col s12 m3\"> <label for=\"addr\">Ip</label> <input type=\"text\" id=\"addr\" name=\"addr\"> </div> <div class=\"col s12 m3\"> <label for=\"sm\">Submask</label> <input type=\"text\" id=\"sm\" name=\"sm\"> </div> <div class=\"col s12 m3\"> <label for=\"gw\">Gateway</label> <input type=\"text\" id=\"gw\" name=\"gw\"> </div> </div> </div> <div class=\"row\"> <div class=\"col s10\"> <button type=\"submit\" class=\"waves-effect waves-light btn teal\">Save</button> <!--<button type=\"submit\" name=\"reboot\" class=\"waves-effect waves-light btn grey lighten-1\">Reboot</button>--> <!--<button type=\"submit\" name=\"reboot\" class=\"waves-effect waves-light btn grey lighten-1\">Reboot</button>--> </div> </div> </form> </div> </div> </div> </div> <script src=\"https://cdnjs.cloudflare.com/ajax/libs/jquery/3.4.1/jquery.min.js\"></script> <script src=\"https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/js/materialize.min.js\"></script> <script src=\"https://cerny.in/nouislider.js\"></script> <script src=\"https://cerny.in/diyhue.js\"></script> </body> </html>";

        server.send(200, "text/html", htmlContent);
        if (server.args()) {
            delay(1000); // needs to wait until response is received by browser. If ESP restarts too soon, browser will think there was an error.
            ESP.restart();
        }
    });


    server.onNotFound(handleNotFound);

    server.begin();
}

void loop() {
    ArduinoOTA.handle();
    server.handleClient();
    lightEngine();
}
