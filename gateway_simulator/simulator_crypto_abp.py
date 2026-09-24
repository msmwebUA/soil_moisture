"""
Simulates field sensor unite and the packet-forwarder gateway. It sends
encrypted LoRaWAN uplinks to ChirpStack's built-in UDP
listener (chirpstack-gateway-bridge).

This uses ABP (Activation By Personalization) instead of OTAA, so there is
no join procedure to simulate, just DevAddr, NwkSKey, AppSKey.

See ChirpStack_readme.txt for instructions on how to set up ChirpStack ABP (profile, device and gateway)

Usage:
    open venv
    pip3 install pycryptodome
    python3 simulator.py --host 192.168.1.110 --interval 60

    --host is the IP of the machine running ChirpStack (the UDP listener
    is chirpstack-gateway-bridge, default port 1700).

Limitations:
    Uplink only. Downlinks (e.g. changing the interval from ThingsBoard)
    are not received.
    FCnt is kept in memory only, restarting the script resets it to 0, so
        create device without frame counter validation, or alternatively, delete and re-add the device (or reset FCnt) in ChirpStack after each run. 
"""

import argparse
import base64
import json
import random
import socket
import struct
import time
from Crypto.Cipher import AES
from Crypto.Hash import CMAC

# ABP
GATEWAY_EUI = "16e4e44b2d48fc22"          # 16 hex chars, Gateway ID in ChirpStack
DEV_ADDR    = "01bbb5a4"                  # 8 hex chars (4 bytes)
NWK_SKEY    = "305d844d0b434fbf32ffa838171aec65"  # 32 hex chars (16 bytes)
APP_SKEY    = "58ab2e17a4a9c3cf78b30c562ba6ff8f"  # 32 hex chars (16 bytes)

FPORT_DATA = 1

# semtech udp
PROTOCOL_VERSION = 0x02
PUSH_DATA = 0x00
PULL_DATA = 0x02

def h2b(h):
    return bytes.fromhex(h)

def aes_encrypt_block(key, block):
    return AES.new(key, AES.MODE_ECB).encrypt(block)

def lorawan_encrypt_payload(key_hex, dev_addr_hex, fcnt, direction, payload):
    """Encrypt FRMPayload per LoRaWAN 1.0.x spec (CTR-like construction)."""
    key = h2b(key_hex)
    dev_addr = h2b(dev_addr_hex)[::-1]  # little-endian on air
    n_blocks = (len(payload) + 15) // 16
    s = b""
    for i in range(1, n_blocks + 1):
        a_i = bytes([0x01, 0, 0, 0, 0, direction]) + dev_addr + \
              struct.pack("<I", fcnt) + bytes([0, i])
        s += aes_encrypt_block(key, a_i)
    padded = payload + b"\x00" * (n_blocks * 16 - len(payload))
    xored = bytes(a ^ b for a, b in zip(padded, s))
    return xored[:len(payload)]

def lorawan_mic(key_hex, dev_addr_hex, fcnt, direction, msg):
    """Compute the 4-byte MIC (AES-CMAC) over B0 || msg."""
    key = h2b(key_hex)
    dev_addr = h2b(dev_addr_hex)[::-1]
    b0 = bytes([0x49, 0, 0, 0, 0, direction]) + dev_addr + \
         struct.pack("<I", fcnt) + bytes([0, len(msg)])
    cobj = CMAC.new(key, ciphermod=AES)
    cobj.update(b0 + msg)
    return cobj.digest()[:4]

def build_phypayload(fcnt, payload_bytes):
    """Build a LoRaWAN uplink frame."""
    mhdr = bytes([0x40])                       # unconfirmed data up, major=0
    dev_addr_le = h2b(DEV_ADDR)[::-1]
    fctrl = bytes([0x00])                      # ADR off, no ACK, FOptsLen=0
    fcnt_le = struct.pack("<H", fcnt & 0xFFFF)
    fhdr = dev_addr_le + fctrl + fcnt_le        # no FOpts
    fport = bytes([FPORT_DATA])

    enc_payload = lorawan_encrypt_payload(APP_SKEY, DEV_ADDR, fcnt, 0, payload_bytes)

    msg = mhdr + fhdr + fport + enc_payload
    mic = lorawan_mic(NWK_SKEY, DEV_ADDR, fcnt, 0, msg)
    return msg + mic

# payload
def read_fake_sensors():
    """Generate fake sensor data."""
    soil_raw = random.randint(1500, 3000)      # 12-bit ADC counts
    temp_c = round(random.uniform(15.0, 25.0), 2)
    vbat_mv = random.randint(3700, 4700)
    return soil_raw, temp_c, vbat_mv

def compose_payload(soil_raw, temp_c, vbat_mv, interval_min):
    """ Mirrors the supplied firmware's 9-byte application payload."""
    flags = 0
    if soil_raw < 100 or soil_raw > 4000:
        flags |= 0x02
    if vbat_mv < 3700:
        flags |= 0x04

    t100 = int(round(temp_c * 100))
    b = bytearray(9)
    b[0] = flags
    b[1] = (soil_raw >> 8) & 0xFF
    b[2] = soil_raw & 0xFF
    b[3] = (t100 >> 8) & 0xFF
    b[4] = t100 & 0xFF
    b[5] = (vbat_mv >> 8) & 0xFF
    b[6] = vbat_mv & 0xFF
    b[7] = (interval_min >> 8) & 0xFF
    b[8] = interval_min & 0xFF
    return bytes(b)

def send_push_data(sock, addr, phy_payload):
    token = random.randint(0, 0xFFFF)
    header = struct.pack(">BHB", PROTOCOL_VERSION, token, PUSH_DATA) + h2b(GATEWAY_EUI)

    rxpk = {
        "tmst": int(time.time() * 1000) & 0xFFFFFFFF,
        "chan": 0,
        "rfch": 0,
        "freq": 868.1,
        "stat": 1,
        "modu": "LORA",
        "datr": "SF7BW125",
        "codr": "4/5",
        "lsnr": 7.0,
        "rssi": -55,
        "size": len(phy_payload),
        "data": base64.b64encode(phy_payload).decode("ascii"),
    }
    body = json.dumps({"rxpk": [rxpk]}).encode("utf-8")
    sock.sendto(header + body, addr)

def send_pull_data(sock, addr):
    """Optional: tells the server this gateway is online (needed only if
    later want to test downlinks)."""
    token = random.randint(0, 0xFFFF)
    packet = struct.pack(">BHB", PROTOCOL_VERSION, token, PULL_DATA) + h2b(GATEWAY_EUI)
    sock.sendto(packet, addr)

def main():
    ap = argparse.ArgumentParser(description="Simulate a Wio-E5 soil node -> ChirpStack (UDP)")
    ap.add_argument("--host", required=True, help="LAN IP of the ChirpStack server")
    ap.add_argument("--port", type=int, default=1700, help="Gateway Bridge UDP port (default 1700)")
    ap.add_argument("--interval", type=int, default=60, help="Seconds between uplinks (default 60)")
    ap.add_argument("--count", type=int, default=0, help="Number of uplinks to send (0 = forever)")
    args = ap.parse_args()

    addr = (args.host, args.port)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    print(f"Sending simulated uplinks to {args.host}:{args.port} every {args.interval}s")
    print(f"Gateway EUI: {GATEWAY_EUI}  DevAddr: {DEV_ADDR}")
    send_pull_data(sock, addr)  # register the gateway as "online" once

    fcnt = 0
    sent = 0
    try:
        while args.count == 0 or sent < args.count:
            soil_raw, temp_c, vbat_mv = read_fake_sensors()
            payload = compose_payload(soil_raw, temp_c, vbat_mv, args.interval // 60 or 1)
            phy = build_phypayload(fcnt, payload)

            send_push_data(sock, addr, phy)
            print(f"[{time.strftime('%H:%M:%S')}] FCnt={fcnt:5d}  "
                  f"soil={soil_raw:4d}  temp={temp_c:5.2f}C  vbat={vbat_mv:4d}mV  "
                  f"payload={payload.hex()}  phy_len={len(phy)}")

            fcnt += 1
            sent += 1
            if args.count == 0 or sent < args.count:
                time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\nStopped.")


if __name__ == "__main__":
    main()