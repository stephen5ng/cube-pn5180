#!/usr/bin/env python3
"""Display each cube's ID number on its LED matrix for identification."""

import paho.mqtt.client as mqtt

BROKER = "192.168.8.247"
# P0 cubes: 1-6, P1 cubes: 11-16
CUBE_IDS = [1, 2, 3, 4, 5, 6, 11, 12, 13, 14, 15, 16]

def main():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.connect(BROKER, 1883, 60)
    client.loop_start()

    for cube_id in CUBE_IDS:
        topic = f"cube/{cube_id}/letter"
        display_text = str(cube_id)
        client.publish(topic, display_text)
        print(f"Cube {cube_id}: published '{display_text}' to {topic}")

    client.loop_stop()
    client.disconnect()
    print("\nDone. Numbers will remain on displays.")

if __name__ == "__main__":
    main()
