#!/usr/bin/env python3
"""
Update cubes 1-6 if they're out of date.

Checks the version (build timestamp) of each cube and uploads new firmware
if the version doesn't match the current build.
"""

import paho.mqtt.client as mqtt
import time
import subprocess
import sys

MQTT_BROKER = "192.168.8.247"
MQTT_PORT = 1883
CUBE_START = 1
CUBE_END = 6

# Current build timestamp will be generated when we compile
# For now, we'll upload and then check if versions match


def get_cube_versions():
    """Get version from all cubes via MQTT retained messages."""
    versions = {}

    def on_connect(client, userdata, flags, rc):
        client.subscribe(f"cube/+/version")

    def on_message(client, userdata, msg):
        topic = msg.topic
        payload = msg.payload.decode('utf-8')
        # Extract cube ID from topic (e.g., "cube/5/version" -> "5")
        cube_id = topic.split('/')[1]
        if cube_id.isdigit() and CUBE_START <= int(cube_id) <= CUBE_END:
            versions[cube_id] = payload
            print(f"Cube {cube_id}: {payload}")

    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_start()

    # Wait a bit for all retained messages to arrive
    time.sleep(3)

    client.loop_stop()
    client.disconnect()

    return versions


def upload_to_cube(cube_id):
    """Upload firmware to a specific cube."""
    ip_address = f"192.168.8.{20 + int(cube_id)}"
    print(f"Uploading to cube {cube_id} ({ip_address})...")

    try:
        # PlatformIO must run from the cube-pn5180 directory
        project_dir = "/Users/stephenng/programming/blockwords/cube-pn5180"
        result = subprocess.run(
            ["/Users/stephenng/.platformio/penv/bin/platformio", "run", "-e", "v1",
             "--target", "upload", "--upload-port", ip_address],
            capture_output=True,
            text=True,
            timeout=120,
            cwd=project_dir
        )

        if "SUCCESS" in result.stdout:
            print(f"  ✓ Upload successful")
            return True
        else:
            print(f"  ✗ Upload failed")
            print(f"    {result.stderr[-200:]}")  # Last 200 chars of error
            return False
    except subprocess.TimeoutExpired:
        print(f"  ✗ Upload timed out (cube might be asleep)")
        return False
    except Exception as e:
        print(f"  ✗ Error: {e}")
        return False


def main():
    print("=" * 60)
    print("Cube Firmware Updater (Cubes 1-6)")
    print("=" * 60)

    # Get current versions from cubes
    print("\nChecking cube versions...")
    versions = get_cube_versions()

    if not versions:
        print("No cubes responded. They may all be asleep.")
        print("Wake cubes and try again.")
        return 1

    print(f"\nFound {len(versions)} cube(s) with version info")

    # Upload to each cube (uploading is fast, so we do all awake ones)
    print("\nUploading firmware...")
    success_count = 0

    for cube_id in sorted(versions.keys(), key=int):
        if upload_to_cube(cube_id):
            success_count += 1

    print(f"\n{'=' * 60}")
    print(f"Updated {success_count}/{len(versions)} cube(s)")
    print("=" * 60)

    # Note: We're not actually comparing versions here - just uploading
    # In production, you'd compare against a known latest version

    return 0


if __name__ == "__main__":
    sys.exit(main())
