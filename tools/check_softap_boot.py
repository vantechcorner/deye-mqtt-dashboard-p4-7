#!/usr/bin/env python3
"""Watch serial for SoftAP portal boot messages (local bring-up helper)."""

import re
import sys
import time

try:
    import serial
except ImportError:
    print("pip install pyserial", file=sys.stderr)
    sys.exit(1)

port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200

ser = serial.Serial(port, baud, timeout=0.5)
ser.setDTR(False)
ser.setRTS(True)
time.sleep(0.1)
ser.setRTS(False)
deadline = time.time() + 50
buf = b""
while time.time() < deadline:
    chunk = ser.read(512)
    if chunk:
        sys.stdout.buffer.write(chunk)
        sys.stdout.buffer.flush()
        buf += chunk
        if b"SoftAP" in buf and b"http://192.168.4.1" in buf and b"Deye-P4-" in buf:
            time.sleep(0.5)
            buf += ser.read(1024)
            break
ser.close()
m = re.findall(rb'SoftAP "(Deye-P4-[0-9A-Fa-f]{4})"', buf)
print("\n--- AP SSID ---", m[-1].decode() if m else "NOT FOUND")
print("portal", b"http://192.168.4.1" in buf)
print("no_nvs", b"No NVS WiFi SSID" in buf)
