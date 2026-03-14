#!/bin/bash -e

MQTT_HOST=${MQTT_SERVER:-192.168.8.247}

# Reboot all cubes (global topic - firmware doesn't support single-cube reboot)
mosquitto_pub -h $MQTT_HOST -t "cube/reboot" -m "1"
