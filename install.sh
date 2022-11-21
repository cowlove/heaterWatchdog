#!/bin/bash
cd ~/Arduino/libraries
git clone git@github.com:cowlove/esp32jimlib 

arduino-cli lib install PubSubClient
