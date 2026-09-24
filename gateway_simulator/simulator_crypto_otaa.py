"""
LoRaWAN OTAA end-device + Semtech UDP packet-forwarder simulator.

Simulator sends real LoRaWAN PHY payloads (OTAA JoinReq and encrypted data frames) 
inside Semtech UDP JSON packets to ChirpStack Gateway Bridge.

AppKey is used as the root key and the join-accept/session keys follow
LoRaWAN 1.0.x. If RadioLib version is configured for LoRaWAN 1.1,
use --lw-version 1.1 and configure ChirpStack accordingly.

Dependencies:
    python3 -m pip install cryptography

Example:
    python3 lorawan_chirpstack_sim.py \
      --server 192.168.1.50 \
      --gateway-id AABBCCDDEEFF0011 \
      --join-eui 0000000000000000 \
      --dev-eui 70B3D57ED0001234 \
      --app-key 00112233445566778899AABBCCDDEEFF \
      --interval 10
"""

import argparse
import base64
import binascii
import json
import random
import socket
import struct
import sys
import time
from dataclasses import dataclass

from cryptography.hazmat.primitives.cmac import CMAC
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


def aes128_encrypt(key: bytes, block: bytes) -> bytes:
    if len(key) != 16 or len(block) != 16:
        raise ValueError("AES-128 requires 16-byte key and block")
    cipher = Cipher(algorithms.AES(key), modes.ECB())
    enc = cipher.encryptor()
    return enc.update(block) + enc.finalize()


def aes128_decrypt(key: bytes, block: bytes) -> bytes:
    if len(key) != 16 or len(block) != 16:
        raise ValueError("AES-128 requires 16-byte key and block")
    cipher = Cipher(algorithms.AES(key), modes.ECB())
    dec = cipher.decryptor()
    return dec.update(block) + dec.finalize()


def aes_cmac(key: bytes, data: bytes) -> bytes:
    c = CMAC(algorithms.AES(key))
    c.update(data)
    return c.finalize()


def hex_bytes(s: str, n: int, name: str) -> bytes:
    s = s.replace(":", "").replace("-", "").replace(" ", "")
    if len(s) != n * 2:
        raise ValueError(f"{name} must contain {n * 2} hex characters")
    try:
        return bytes.fromhex(s)
    except ValueError as e:
        raise ValueError(f"{name} is not valid hexadecimal") from e


def le_eui(s: str, name: str) -> bytes:
    # User-facing EUI notation is normally written MSB-first.
    # LoRaWAN JoinReq puts JoinEUI and DevEUI LSB first on the wire.
    return hex_bytes(s, 8, name)[::-1]


def mac_addr_bytes(addr: int) -> bytes:
    return struct.pack("<I", addr)


def cmac_mic(key: bytes, msg: bytes) -> bytes:
    return aes_cmac(key, msg)[:4]


def crypt_frm_payload(key: bytes, dev_addr: int, fcnt: int,
                      direction: int, payload: bytes) -> bytes:
    out = bytearray()
    blocks = (len(payload) + 15) // 16
    for i in range(1, blocks + 1):
        a = (
            b"\x01" +
            b"\x00\x00\x00\x00" +
            bytes([direction]) +
            struct.pack("<I", dev_addr) +
            struct.pack("<I", fcnt) +
            b"\x00" +
            bytes([i])
        )
        s = aes128_encrypt(key, a)
        chunk = payload[(i - 1) * 16:i * 16]
        out.extend(x ^ y for x, y in zip(chunk, s))
    return bytes(out)


def build_join_request(join_eui_wire: bytes, dev_eui_wire: bytes,
                        dev_nonce: int, root_key: bytes) -> bytes:
    mhdr = b"\x00"  # Join-request, LoRaWAN R1
    body = (
        mhdr +
        join_eui_wire +
        dev_eui_wire +
        struct.pack("<H", dev_nonce)
    )
    return body + cmac_mic(root_key, body)


def derive_10x_session_keys(app_key: bytes, app_nonce_wire: bytes,
                             net_id_wire: bytes, dev_nonce: int):
    base = app_nonce_wire + net_id_wire + struct.pack("<H", dev_nonce) + b"\x00" * 7
    nwk_s_key = aes128_encrypt(app_key, b"\x01" + base)
    app_s_key = aes128_encrypt(app_key, b"\x02" + base)
    return nwk_s_key, app_s_key


def derive_11x_session_keys(nwk_key: bytes, app_key: bytes,
                             join_nonce_wire: bytes, join_eui_wire: bytes,
                             dev_nonce: int):
    base = join_nonce_wire + join_eui_wire + struct.pack("<H", dev_nonce) + b"\x00" * 2
    f_nwk_s_int = aes128_encrypt(nwk_key, b"\x01" + base)
    app_s = aes128_encrypt(app_key, b"\x02" + base)
    s_nwk_s_int = aes128_encrypt(nwk_key, b"\x03" + base)
    nwk_s_enc = aes128_encrypt(nwk_key, b"\x04" + base)
    return f_nwk_s_int, s_nwk_s_int, nwk_s_enc, app_s


@dataclass
class Session:
    dev_addr: int
    nwk_s_key: bytes | None
    app_s_key: bytes
    # For 1.1:
    f_nwk_s_int_key: bytes | None = None
    s_nwk_s_int_key: bytes | None = None
    nwk_s_enc_key: bytes | None = None


class SemtechUDP:
    PROTOCOL_VERSION = 2
    PUSH_DATA = 0
    PUSH_ACK = 1
    PULL_DATA = 2
    PULL_RESP = 3
    PULL_ACK = 4

    def __init__(self, host: str, port: int, gateway_id: bytes):
        self.server = (host, port)
        self.gateway_id = gateway_id
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("0.0.0.0", 0))
        self.sock.settimeout(1.0)
        self.token = random.randrange(0, 65536)

    def _token_bytes(self):
        self.token = (self.token + 1) & 0xFFFF
        return struct.pack("!H", self.token)

    def push_data(self, rxpk: dict):
        token = self._token_bytes()
        header = (
            bytes([self.PROTOCOL_VERSION]) +
            token +
            bytes([self.PUSH_DATA]) +
            self.gateway_id
        )
        payload = json.dumps({"rxpk": [rxpk]}, separators=(",", ":")).encode()
        self.sock.sendto(header + payload, self.server)

    def pull_data(self):
        token = self._token_bytes()
        header = (
            bytes([self.PROTOCOL_VERSION]) +
            token +
            bytes([self.PULL_DATA]) +
            self.gateway_id
        )
        self.sock.sendto(header, self.server)

    def recv(self):
        try:
            data, _ = self.sock.recvfrom(65535)
        except socket.timeout:
            return None
        if len(data) < 4:
            return None
        ver, token_hi, token_lo, ident = data[:4]
        token = (token_hi << 8) | token_lo
        return ver, token, ident, data[4:]

    def close(self):
        self.sock.close()


def phy_uplink_rxpk(phy: bytes, freq_mhz: float, sf: int) -> dict:
    return {
        "tmst": int(time.monotonic() * 1_000_000) & 0xFFFFFFFF,
        "chan": 0,
        "rfch": 0,
        "freq": freq_mhz,
        "stat": 1,
        "modu": "LORA",
        "datr": f"SF{sf}BW125",
        "codr": "4/5",
        "lsnr": 7.5,
        "rssi": -45,
        "size": len(phy),
        "data": base64.b64encode(phy).decode(),
    }


class DeviceSimulator:
    def __init__(self, args):
        self.args = args
        self.gateway = SemtechUDP(
            args.server, args.port, hex_bytes(args.gateway_id, 8, "gateway-id")
        )
        self.join_eui_wire = le_eui(args.join_eui, "join-eui")
        self.dev_eui_wire = le_eui(args.dev_eui, "dev-eui")
        self.app_key = hex_bytes(args.app_key, 16, "app-key")
        self.nwk_key = (
            hex_bytes(args.nwk_key, 16, "nwk-key")
            if args.nwk_key else self.app_key
        )
        self.dev_nonce = args.dev_nonce
        self.fcnt_up = 0
        self.session: Session | None = None
        self.pending_confirmed_fcnt = None
        self.last_pull = 0.0

    def log(self, msg):
        print(time.strftime("%H:%M:%S"), msg, flush=True)

    def send_join(self):
        phy = build_join_request(
            self.join_eui_wire, self.dev_eui_wire,
            self.dev_nonce, self.nwk_key if self.args.lw_version == "1.1" else self.app_key
        )
        self.log(f"TX JoinReq devNonce={self.dev_nonce} PHY={phy.hex()}")
        self.gateway.push_data(phy_uplink_rxpk(phy, self.args.freq, self.args.sf))
        self.dev_nonce = (self.dev_nonce + 1) & 0xFFFF

    def decrypt_join_accept(self, phy: bytes):
        if len(phy) not in (17, 33):
            raise ValueError(f"unexpected JoinAccept length {len(phy)}")
        if (phy[0] & 0xE0) != 0x20:
            raise ValueError(f"not a JoinAccept MHDR: 0x{phy[0]:02x}")
        encrypted = phy[1:]
        # End-device uses AES-128 encrypt to decrypt a JoinAccept because the
        # network side used AES decrypt.
        key = self.nwk_key if self.args.lw_version == "1.1" else self.app_key
        clear = b"".join(
            aes128_encrypt(key, encrypted[i:i+16])
            for i in range(0, len(encrypted), 16)
        )
        if self.args.lw_version == "1.0":
            app_nonce = clear[0:3]
            net_id = clear[3:6]
            dev_addr = struct.unpack("<I", clear[6:10])[0]
            dl_settings = clear[10]
            rx_delay = clear[11]
            cf_len = 16 if len(clear) == 32 else 0
            body = phy[:1] + clear[:12 + cf_len]
            mic = clear[12 + cf_len:16 + cf_len]
            expected = cmac_mic(self.app_key, body)
            if mic != expected:
                raise ValueError(
                    f"JoinAccept MIC mismatch: got {mic.hex()} expected {expected.hex()}"
                )
            nwk, app = derive_10x_session_keys(
                self.app_key, app_nonce, net_id, (self.dev_nonce - 1) & 0xFFFF
            )
            self.session = Session(dev_addr, nwk, app)
            self.log(
                f"JOIN ACCEPT: DevAddr={dev_addr:08X} "
                f"RxDelay={rx_delay}s DLSettings=0x{dl_settings:02x}"
            )
        else:
            join_nonce = clear[0:3]
            net_id = clear[3:6]
            dev_addr = struct.unpack("<I", clear[6:10])[0]
            dl_settings = clear[10]
            rx_delay = clear[11]
            cf_len = 16 if len(clear) == 32 else 0
            body = phy[:1] + clear[:12 + cf_len]
            mic = clear[12 + cf_len:16 + cf_len]
            # This simulator supports the common ChirpStack direct/1.1 setup
            # where the join-accept MIC is validated with NwkKey.
            expected = cmac_mic(self.nwk_key, body)
            if mic != expected:
                raise ValueError(
                    f"1.1 JoinAccept MIC mismatch: got {mic.hex()} expected {expected.hex()}"
                )
            f, s, n, app = derive_11x_session_keys(
                self.nwk_key, self.app_key, join_nonce,
                self.join_eui_wire, (self.dev_nonce - 1) & 0xFFFF
            )
            self.session = Session(dev_addr, None, app, f, s, n)
            self.log(
                f"JOIN ACCEPT: DevAddr={dev_addr:08X} "
                f"RxDelay={rx_delay}s DLSettings=0x{dl_settings:02x}"
            )

    def handle_downlink(self, phy: bytes):
        if not phy:
            return
        mtype = phy[0] >> 5
        # JoinAccept
        if mtype == 1:
            self.decrypt_join_accept(phy)
            return
        # Data downlink. For this simulator we primarily care about ACK.
        if mtype not in (3, 5) or len(phy) < 8:
            return
        dev_addr = struct.unpack("<I", phy[1:5])[0]
        if not self.session or dev_addr != self.session.dev_addr:
            return
        fctrl = phy[5]
        fcnt16 = struct.unpack("<H", phy[6:8])[0]
        ack = bool(fctrl & 0x20)
        self.log(
            f"RX downlink DevAddr={dev_addr:08X} FCnt16={fcnt16} "
            f"ACK={'yes' if ack else 'no'} PHY={phy.hex()}"
        )
        if ack:
            self.pending_confirmed_fcnt = None

    def poll_downlinks(self, seconds: float):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now - self.last_pull > 2.0:
                self.gateway.pull_data()
                self.last_pull = now
            msg = self.gateway.recv()
            if msg:
                _, _, ident, body = msg
                if ident == self.gateway.PULL_RESP:
                    try:
                        obj = json.loads(body.decode())
                        data = base64.b64decode(obj["txpk"]["data"])
                        self.handle_downlink(data)
                    except Exception as e:
                        self.log(f"downlink parse error: {e}")
                elif ident == self.gateway.PULL_ACK:
                    pass
                elif ident == self.gateway.PUSH_ACK:
                    pass

    def make_payload(self):
        # Mirrors the supplied firmware's 9-byte application payload.
        # [0] flags, [1..2] soil raw, [3..4] temp x100 signed,
        # [5..6] battery mV, [7..8] interval minutes.
        soil = random.randint(1200, 3300)
        temp_c = random.uniform(17.0, 29.0)
        temp100 = int(round(temp_c * 100))
        batt = random.randint(3700, 4200)
        interval = int(self.args.interval)
        flags = 0
        if soil < 100 or soil > 4000:
            flags |= 0x02
        if batt < 3700:
            flags |= 0x04
        return struct.pack(">BHHHH", flags, soil, temp100 & 0xFFFF, batt, interval), soil, temp_c, batt

    def build_data_uplink(self, payload: bytes, confirmed: bool) -> bytes:
        if not self.session:
            raise RuntimeError("not joined")
        self.fcnt_up += 1
        mtype = 4 if confirmed else 2
        mhdr = bytes([mtype << 5])
        fhdr = mac_addr_bytes(self.session.dev_addr) + b"\x00" + struct.pack("<H", self.fcnt_up)
        msg = mhdr + fhdr + bytes([1]) + crypt_frm_payload(
            self.session.app_s_key, self.session.dev_addr, self.fcnt_up, 0, payload
        )
        if self.args.lw_version == "1.0":
            mic_key = self.session.nwk_s_key
            b0 = (
                b"\x49" + b"\x00\x00\x00\x00" +
                b"\x00" +
                mac_addr_bytes(self.session.dev_addr) +
                struct.pack("<I", self.fcnt_up) +
                b"\x00" + bytes([len(msg)])
            )
            mic = aes_cmac(mic_key, b0 + msg)[:4]
        else:
            # LoRaWAN 1.1. For normal unconfirmed/confirmed uplinks with no
            # downlink-ACK being piggybacked, ConfFCnt is zero. We use TxDr=3
            # (SF9) and TxCh=0 to match this simulator's radio metadata.
            b0 = (
                b"\x49" + b"\x00\x00" + b"\x00\x00" +
                b"\x00" +
                mac_addr_bytes(self.session.dev_addr) +
                struct.pack("<I", self.fcnt_up) +
                b"\x00" + bytes([len(msg)])
            )
            b1 = (
                b"\x49" + b"\x00\x00" +
                bytes([3]) + bytes([0]) +
                b"\x00" +
                mac_addr_bytes(self.session.dev_addr) +
                struct.pack("<I", self.fcnt_up) +
                b"\x00" + bytes([len(msg)])
            )
            cmac_s = aes_cmac(self.session.s_nwk_s_int_key, b1 + msg)
            cmac_f = aes_cmac(self.session.f_nwk_s_int_key, b0 + msg)
            mic = cmac_s[0:2] + cmac_f[0:2]
        return msg + mic

    def send_uplink(self):
        payload, soil, temp, batt = self.make_payload()
        confirmed = bool(self.args.confirmed)
        phy = self.build_data_uplink(payload, confirmed)
        self.log(
            f"TX uplink FCnt={self.fcnt_up} confirmed={confirmed} "
            f"soil={soil} temp={temp:.2f}C batt={batt}mV "
            f"payload={payload.hex()} PHY={phy.hex()}"
        )
        self.pending_confirmed_fcnt = self.fcnt_up if confirmed else None
        self.gateway.push_data(phy_uplink_rxpk(phy, self.args.freq, self.args.sf))
        self.poll_downlinks(6.5)

    def run(self):
        self.log(
            f"Semtech UDP -> {self.args.server}:{self.args.port}, "
            f"sim-gateway={self.args.gateway_id}, dev-eui={self.args.dev_eui}"
        )
        # Keep a PULL_DATA heartbeat running while joining.
        self.send_join()
        join_deadline = time.monotonic() + self.args.join_timeout
        while not self.session and time.monotonic() < join_deadline:
            self.poll_downlinks(1.0)
        if not self.session:
            raise RuntimeError(
                "No JoinAccept received. Check Gateway Bridge UDP/1700, "
                "gateway registration, device credentials, and server logs."
            )
        self.log("OTAA joined successfully.")
        while True:
            self.send_uplink()
            time.sleep(self.args.interval)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--server", required=True, help="ChirpStack Gateway Bridge IP/DNS")
    p.add_argument("--port", type=int, default=1700)
    p.add_argument("--gateway-id", required=True, help="8-byte simulated gateway EUI")
    p.add_argument("--join-eui", required=True, help="8-byte JoinEUI/AppEUI")
    p.add_argument("--dev-eui", required=True, help="8-byte DevEUI")
    p.add_argument("--app-key", required=True, help="16-byte AppKey")
    p.add_argument("--nwk-key", help="16-byte NwkKey (default: AppKey)")
    p.add_argument("--lw-version", choices=("1.0", "1.1"), default="1.1")
    p.add_argument("--dev-nonce", type=int, default=1)
    p.add_argument("--interval", type=float, default=10.0,
                   help="seconds between application uplinks (test value)")
    p.add_argument("--join-timeout", type=float, default=30.0)
    p.add_argument("--freq", type=float, default=868.1)
    p.add_argument("--sf", type=int, default=9)
    p.add_argument("--confirmed", action="store_true",
                   help="request a confirmed uplink, like the supplied firmware")
    args = p.parse_args()
    if not (0 <= args.dev_nonce <= 0xFFFF):
        p.error("--dev-nonce must be 0..65535")
    try:
        DeviceSimulator(args).run()
    except KeyboardInterrupt:
        print("\nStopped.")
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
