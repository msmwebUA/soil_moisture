import socket
import json
import os
import base64
import random
import struct
import time
from datetime import datetime, timezone

SERVER = "192.168.1.110"
PORT = 1700

def main() -> None:
  while True:
    gateway_eui = bytes.fromhex("AA555A0000000001")
    soil = random.randint(900, 2200)
    temp = int(22.5 * 100)
    battery = 3950
    interval = 60
    current_timestamp = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')

    payload = bytearray(9)

    payload[0] = 0
    payload[1:3] = soil.to_bytes(2, "big")
    payload[3:5] = temp.to_bytes(2, "big", signed=True)
    payload[5:7] = battery.to_bytes(2, "big")
    payload[7:9] = interval.to_bytes(2, "big")

    phy_payload = base64.b64encode(payload).decode()

    data = {
      "rxpk": [{
        "time": current_timestamp,
        "tmst": random.randint(1, 4294967295),
        "freq": 868.1,
        "chan": 0,
        "rfch": 0,
        "stat": 1,
        "modu": "LORA",
        "datr": "SF9BW125",
        "codr": "4/5",
        "rssi": -70,
        "lsnr": 10.5,
        "size": len(payload),
        "data": phy_payload
      }]
    }

    token = os.urandom(2)
    packet = (
      b"\x02" +
      token +
      b"\x00" +
      gateway_eui +
      json.dumps(data).encode()
    )
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.sendto(packet, (SERVER, PORT))
    print(f"{current_timestamp}: Sent PUSH_DATA")
    time.sleep(10)

if __name__ == "__main__":
  main()