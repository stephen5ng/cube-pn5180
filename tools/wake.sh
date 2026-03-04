#!/bin/bash -e

MQTT_HOST=${MQTT_SERVER:-192.168.8.247}

if [ -z "$1" ]; then
  # Wake all cubes: clear flags for all known cube IDs
  for id in 1 2 3 4 5 6 11 12 13 14 15 16; do
    mosquitto_pub -h $MQTT_HOST -t "cube/$id/auto_sleep" -m "" --retain
  done
  echo "Cleared auto_sleep flag for all cubes"
else
  # Wake specific cube
  mosquitto_pub -h $MQTT_HOST -t "cube/$1/auto_sleep" -m "" --retain
  echo "Cleared auto_sleep flag for cube $1"
fi
