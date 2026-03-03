#!/usr/bin/env python3
"""Check latency and diagnostics on cubes 1-6"""

import socket
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed

UDP_PORT = 54321
TIMEOUT = 2.0

def ping_latency(ip):
    """Get ICMP ping latency in ms"""
    try:
        result = subprocess.run(
            ["ping", "-c", "1", "-W", "1000", ip],
            capture_output=True,
            text=True,
            timeout=2
        )
        if result.returncode == 0:
            for line in result.stdout.split('\n'):
                if 'time=' in line:
                    # Extract time value
                    import re
                    match = re.search(r'time=([\d.]+)', line)
                    if match:
                        return float(match.group(1))
    except Exception:
        pass
    return None

def udp_command(ip, cmd):
    """Send UDP command and get response"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(TIMEOUT)
        sock.sendto(cmd.encode(), (ip, UDP_PORT))
        data, _ = sock.recvfrom(1024)
        sock.close()
        return data.decode().strip()
    except socket.timeout:
        return None
    except Exception as e:
        return f"Error: {e}"

def check_cube(cube_id):
    """Check a single cube"""
    ip = f"192.168.8.{cube_id + 20}"
    results = {"cube_id": cube_id, "ip": ip}

    # ICMP ping
    ping_ms = ping_latency(ip)
    results["ping_icmp"] = ping_ms

    # UDP commands
    for cmd in ["ping", "rssi", "timing", "temp", "diag"]:
        response = udp_command(ip, cmd)
        results[cmd] = response

    return results

def format_results(results):
    """Format results for display"""
    cube_id = results["cube_id"]
    ip = results["ip"]
    print(f"\nCube {cube_id} ({ip}):")
    print("---")

    # Ping
    if results["ping_icmp"] is not None:
        print(f"  Ping (ICMP): {results['ping_icmp']}ms")
    else:
        print("  Ping (ICMP): FAILED")

    # UDP responses
    udp_ping = results.get("ping")
    if udp_ping == "pong":
        print(f"  UDP ping: OK")
    else:
        print(f"  UDP ping: {udp_ping or 'no response'}")

    # RSSI
    rssi = results.get("rssi")
    if rssi and rssi != "pong":
        print(f"  RSSI: {rssi}")

    # Temperature
    temp = results.get("temp")
    if temp and temp not in ["pong", None]:
        print(f"  Temperature: {temp}")

    # Timing
    timing = results.get("timing")
    if timing and timing not in ["pong", None]:
        print(f"  Timing: {timing}")

    # Diagnostics
    diag = results.get("diag")
    if diag and diag not in ["pong", None]:
        lines = diag.split('\\n') if '\\n' in diag else [diag]
        for line in lines[:5]:  # First 5 lines
            print(f"  Diag: {line}")

def main():
    print("Checking cubes 1-6...")
    print("=" * 40)

    with ThreadPoolExecutor(max_workers=6) as executor:
        futures = {executor.submit(check_cube, i): i for i in range(1, 7)}
        for future in as_completed(futures):
            try:
                results = future.result()
                format_results(results)
            except Exception as e:
                cube_id = futures[future]
                print(f"\nCube {cube_id}: ERROR - {e}")

    print("\n" + "=" * 40)
    print("Done.")

if __name__ == "__main__":
    main()
