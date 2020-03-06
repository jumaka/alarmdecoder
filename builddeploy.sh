#!/usr/bin/env bash
#
# Run this as ./builddeploy.sh
# picks up the parent folder to build the code and if successful push to the 
# device
#
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328old `pwd` && \
  arduino-cli upload -p /dev/ttyUSB0 -v --fqbn arduino:avr:nano:cpu=atmega328old `pwd`
