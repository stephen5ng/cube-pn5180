#!/bin/bash -e

while IFS= read -r line; do
  mosquitto_pub -h $MQTT_SERVER -t "cube/$line/reboot" -m "0"
#  mosquitto_pub -h localhost -t "cube/$line/sleep" -m "0" --retain
done < "cube_ids.txt"
