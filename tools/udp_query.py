#!/usr/bin/env python3
import sys
import socket

def send_udp_query(host, port, message, timeout=1.0):
    """Send a UDP query and wait for response."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)

    try:
        sock.sendto(message.encode(), (host, port))
        data, addr = sock.recvfrom(1024)
        return data.decode()
    except socket.timeout:
        return None
    finally:
        sock.close()

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: udp_query.py <host> <port> [message] [timeout]", file=sys.stderr)
        sys.exit(1)

    host = sys.argv[1]
    port = int(sys.argv[2])
    message = sys.argv[3] if len(sys.argv) > 3 else "ping"
    timeout = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0

    response = send_udp_query(host, port, message, timeout)
    if response:
        print(response, end='')
        sys.exit(0)
    else:
        sys.exit(1)
