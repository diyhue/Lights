#!/usr/bin/env bash

# define colors
GRAY='\033[1;30m'; RED='\033[0;31m'; LRED='\033[1;31m'; GREEN='\033[0;32m'; LGREEN='\033[1;32m'; ORANGE='\033[0;33m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; LBLUE='\033[1;34m'; PURPLE='\033[0;35m'; LPURPLE='\033[1;35m'; CYAN='\033[0;36m'; LCYAN='\033[1;36m'; LGRAY='\033[0;37m'; WHITE='\033[1;37m';

echo -e "\n########################################################################";
echo -e "${YELLOW}INSTALLING ARDUINO CLI"
echo "########################################################################";

# if .travis.yml does not set version
if [ -z $ARDUINO_CLI_VERSION ]; then
export ARDUINO_CLI_VERSION="0.3.4-alpha.preview"
echo "NOTE: YOUR .TRAVIS.YML DOES NOT SPECIFY ARDUINO CLI VERSION, USING $ARDUINO_CLI_VERSION"
fi

# if newer version is requested
if [ ! -f $HOME/arduino_cli/$ARDUINO_CLI_VERSION ] && [ -f $HOME/arduino_cli/arduino-cli ]; then
echo -n "DIFFERENT VERSION OF ARDUINO IDE REQUESTED: "
#shopt -s extglob
cd $HOME/arduino_cli/
rm -rf *
if [ $? -ne 0 ]; then echo -e """$RED""\xe2\x9c\x96"; else echo -e """$GREEN""\xe2\x9c\x93"; fi
cd $OLDPWD
fi

# if not already cached, download and install arduino IDE
echo -n "ARDUINO CLI STATUS: "
if [ ! -f $HOME/arduino_cli/arduino-cli ]; then
echo -n "DOWNLOADING: "
wget --quiet https://downloads.arduino.cc/arduino-cli/arduino-cli-${ARDUINO_CLI_VERSION}-linux64.tar.bz2 -O $HOME/arduino-cli.tar.bz2
if [ $? -ne 0 ]; then echo -e """$RED""\xe2\x9c\x96"; else echo -e """$GREEN""\xe2\x9c\x93"; fi
echo -n "UNPACKING ARDUINO IDE: "
[ ! -d $HOME/arduino_cli/ ] && mkdir $HOME/arduino_cli
tar -xf $HOME/arduino-cli.tar.bz2 -C $HOME/arduino_cli
cd $HOME/arduino_cli
mv arduino-cli-${ARDUINO_CLI_VERSION}-linux64 arduino-cli
if [ $? -ne 0 ]; then echo -e """$RED""\xe2\x9c\x96"; else echo -e """$GREEN""\xe2\x9c\x93"; fi
touch $HOME/arduino_cli/$ARDUINO_CLI_VERSION
# delete arduino_cli.tar.bz2
rm $HOME/arduino-cli.tar.bz2
else
echo -n "CACHED: "
echo -e """$GREEN""\xe2\x9c\x93"
fi

# add the arduino CLI to our PATH
export PATH="$HOME/arduino_cli:$PATH"

echo -e "\n########################################################################";
echo -e "${YELLOW}INSTALLING DEPENDENCIES"
echo "########################################################################";


# install the due, esp8266, and adafruit board packages
echo -n "ADD BOARD INDEX: "
if [ ! -f $HOME/arduino_cli/.cli-config.yml ]; then
DEPENDENCY_OUTPUT=$(wget --quiet https://raw.githubusercontent.com/cheesemarathon/Lights/master/.cli-config.yml -P $HOME/arduino_cli/ 2>&1)
if [ $? -ne 0 ]; then echo -e """$RED""\xe2\x9c\x96"; else echo -e """$GREEN""\xe2\x9c\x93"; fi
else
echo -n "CACHED: "
echo -e """$GREEN""\xe2\x9c\x93"
fi

echo -n "REFRESH BOARD INDEX: "
DEPENDENCY_OUTPUT=$(arduino-cli core update-index 2>&1)
if [ $? -ne 0 ]; then echo -e """$RED""\xe2\x9c\x96"; else echo -e """$GREEN""\xe2\x9c\x93"; fi

#echo -n "ESP32: "
#DEPENDENCY_OUTPUT=$(arduino-cli core install esp32:esp32 2>&1)
#if [ $? -ne 0 ]; then echo -e "\xe2\x9c\x96 OR CACHED"; else echo -e """$GREEN""\xe2\x9c\x93"; fi

#echo -n "DUE: "
#DEPENDENCY_OUTPUT=$(arduino-cli core install arduino:sam 2>&1)
#if [ $? -ne 0 ]; then echo -e "\xe2\x9c\x96 OR CACHED"; else echo -e """$GREEN""\xe2\x9c\x93"; fi

#echo -n "ZERO: "
#DEPENDENCY_OUTPUT=$(arduino-cli core install arduino:samd 2>&1)
#if [ $? -ne 0 ]; then echo -e "\xe2\x9c\x96 OR CACHED"; else echo -e """$GREEN""\xe2\x9c\x93"; fi

echo -n "ESP8266: "
DEPENDENCY_OUTPUT=$(arduino-cli core install esp8266:esp8266 2>&1)
if [ $? -ne 0 ]; then echo -e "\xe2\x9c\x96 OR CACHED"; else echo -e """$GREEN""\xe2\x9c\x93"; fi

#echo -n "ADAFRUIT AVR: "
#DEPENDENCY_OUTPUT=$(arduino-cli core install adafruit:avr 2>&1)
#if [ $? -ne 0 ]; then echo -e "\xe2\x9c\x96 OR CACHED"; else echo -e """$GREEN""\xe2\x9c\x93"; fi

#echo -n "ADAFRUIT SAMD: "
#DEPENDENCY_OUTPUT=$(arduino-cli core install adafruit:samd 2>&1)
#if [ $? -ne 0 ]; then echo -e "\xe2\x9c\x96 OR CACHED"; else echo -e """$GREEN""\xe2\x9c\x93"; fi

# Install required libraries
echo -n "INSTALL LIBRARIES: "
DEPENDENCY_OUTPUT=$(arduino-cli lib install WiFiManager ArduinoJson@6.10.0 rc-switch FastLED "NeoPixelBus by Makuna" 2>&1)
if [ $? -ne 0 ]; then echo -e """$RED""\xe2\x9c\x96"; else echo -e """$GREEN""\xe2\x9c\x93"; fi

echo -e "\n########################################################################";
