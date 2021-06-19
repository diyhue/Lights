# Lights

Create your own diyHue compatible Lights!

Steps to Follow:
- Select the correct Sketch according to your Harware Setup (e.g. WS2812b)
- Upload Firmware to Microcontroller
  - Easy Web Tool https://install.diyhue.org (Use Chrome or Edge Browser)
  - or
  - with Arduino IDE (No Changes to Code)
- Connect Hardware (LEDs) and Powersupply
- Connect to ESP8266 WiFi (e.g. search for new WiFi on Smartphone) --> `"Hue rgb strip"`
- Browse `192.168.4.1` --> Connect ESP8266 to your local WiFi
- Check and browse local IP of ESP8266
    - Configure Light from WEB UI (Number of Emulated Lights, Total Number of connected LEDs)
    - Rename you new diyHue Light

- HUE APP: Add new created diyHue Light within APP by searching for new Devices.


More Information is available in our [documentation](https://diyhue.readthedocs.io/en/latest/lights/diylights.html)

www.diyhue.org
<!---
## Contribuiting
The following is a set of guidelines for contributing to diyHue Lights. These are mostly guidelines, not rules. Use your best judgment, and feel free to propose changes to this document in a pull request.

### Building with travis
In order for your sketch to be built with travis, please upload only the `sketch.ino` file in a folder within the Arduino dir. Please make sure that the folder and sketch name are exactly the same, ignoring the extension. For example the sketch would be saved in `Lights/Arduino/Generic_Fun_Light/` and called `Generic_Fun_Light.ino`.

Also within you PR, please create a commit to `.travis.yml`, adding a line in the matrix section. This should be in the format `- SKETCH="YOUR_SKETCH_NAME_HERE"`. Following the above example, `.travis.yml` would look like:

```
...
 matrix:
    - SKETCH="Generic_RGBW_Light"
    - SKETCH="Generic_RGB_Light"
    - SKETCH="Generic_CCT_Light"
    - SKETCH="Generic_RGB_CCT_Light"
    - SKETCH="Generic_WS2812_Strip"
    - SKETCH="Generic_SK6812_Strip"
    - SKETCH="Generic_Fun_Strip"
...
```

Finaly, if your sketch requires any of the libraries not installed on [this](https://github.com/diyhue/Lights/blob/675d2693afdb5f38fd9e61fdcf21aa042a7817b4/install.sh#L94) line of `install.sh`, then please add a commit adding them.

--->
