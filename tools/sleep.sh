#!/bin/bash -e

MQTT_HOST=${MQTT_SERVER:-192.168.8.247}
mosquitto_pub -h $MQTT_HOST -t "cube/sleep" -m "1" --retain
