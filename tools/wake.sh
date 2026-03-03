#!/bin/bash -e

mosquitto_pub -h $MQTT_SERVER -t "cube/sleep" -m "" --retain
