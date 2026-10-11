import socket
import json
import os
import random
import time
from datetime import datetime, timezone

SERVER = "192.168.1.110"
PORT = 1700

GATEWAY_ID = bytes.fromhex("dea72cabaa3c116a")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

while True:
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S GMT")

    stat = {
        "stat": {
            "time": now,
            "lati": 60.98,
            "long": 25.66,
            "alti": 120,
            "rxnb": 100,
            "rxok": 100,
            "rxfw": 100,
            "ackr": 100.0,
            "dwnb": 0,
            "txnb": 0
        }
    }

    token = os.urandom(2)

    packet = (
        b"\x02" +
        token +
        b"\x00" +
        GATEWAY_ID +
        json.dumps(stat).encode()
    )

    sock.sendto(packet, (SERVER, PORT))

    print(f"{now}: Sent STAT")

    time.sleep(10)