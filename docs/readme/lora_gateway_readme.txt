### Lora gateway setup ###

Hardware:
- Elecrow LR1302 LoraWan gateway 868MHz module (PCI-E) with LoRa antenna and GPS antenna
- Elecrow LR1302 RPI HAT 868MHz with LoRa and GPS antennas
- RPI4 4Gb, Linux Debian 13 (Trixie)

1. Enable SPI and I2C on RPI
  sudo raspi-config
    Interface Options:
      SPI -> enable
      I2C -> enable
      Serial Port -> here is two stages:
        Serial console (login shell on UART) -> disabled
        Serial hardware (the UART itself) -> enabled

2. Clone and build gateware software (Semtech's repo)
  git clone https://github.com/Lora-net/sx1302_hal.git
  cd sx1302_hal
  make clean all

3. Install executable
  nano target.cfg 
    replace TARGET_DIR with path you want to install gateway (dirs must be created manually)
              example: /home/piadmin/Documents/lora_gateway/sx1302_hal/bin
            TARGET_USER with rpi admin user's name
  make install
  make install_conf

4. Configurations
  Go to dir where executable installed
    example: ~/Documents/lora_gateway/sx1302_hal/bin
  Replace content of reset_lgw.sh with this text:
    Note. Got example from https://devops.datenkollektiv.de/pages/lorawan-gateway-rak5146-trixie.html

      #!/bin/sh

      SX1302_RESET_PIN=17
      SX1302_POWER_EN_PIN=18
      SX1261_RESET_PIN=5
      AD5338R_RESET_PIN=13

      WAIT_GPIO() {
          sleep 0.01
      }

      iot_sk_reset() {
          pinctrl set $SX1302_RESET_PIN op dl
          pinctrl set $SX1302_POWER_EN_PIN op dl
          pinctrl set $SX1261_RESET_PIN op dl
          pinctrl set $AD5338R_RESET_PIN op dl
          WAIT_GPIO

          pinctrl set $SX1302_POWER_EN_PIN dh
          WAIT_GPIO

          pinctrl set $SX1302_RESET_PIN dh
          WAIT_GPIO
          pinctrl set $SX1302_RESET_PIN dl
          WAIT_GPIO

          pinctrl set $SX1261_RESET_PIN dl
          WAIT_GPIO

          pinctrl set $AD5338R_RESET_PIN dl
          WAIT_GPIO
          pinctrl set $AD5338R_RESET_PIN dh
          WAIT_GPIO
      }

      iot_sk_term() {
          :
      }

      case "$1" in
          stop)
              iot_sk_term
              ;;
          *)
              iot_sk_reset
              ;;
      esac

      exit 0

  Remove needless configuration files except global_conf.json.sx1250.EU868
  Set server address and port of chirpstack gateway bridge
    nano global_conf.json.sx1250.EU868
      In section "gateway_conf":
        Set "server_address": <server_ip>
            "serv_port_up": 1700
            "serv_port_down": 1700
      Replace or copy EUI of gateway with/for Chirpstack's settings

5. Start gateway
  sudo ./reset_lgw.sh start
  sudo ./lora_pkt_fwd -c global_conf.json.sx1250.EU868
    Example of succesful output:
        *** Packet Forwarder ***
        Version: 2.1.0
        *** SX1302 HAL library version info ***
        Version: 2.1.0;
        ***
        INFO: Little endian host
        INFO: found configuration file global_conf.json.sx1250.EU868, parsing it
        INFO: global_conf.json.sx1250.EU868 does contain a JSON object named SX130x_conf, parsing SX1302 parameters
        INFO: com_type SPI, com_path /dev/spidev0.0, lorawan_public 1, clksrc 0, full_duplex 0
        INFO: antenna_gain 0 dBi
        INFO: Configuring legacy timestamp
        INFO: Configuring Tx Gain LUT for rf_chain 0 with 16 indexes for sx1250
        INFO: radio 0 enabled (type SX1250), center frequency 867500000, RSSI offset -215.399994, tx enabled 1, single input mode 0
        INFO: radio 1 enabled (type SX1250), center frequency 868500000, RSSI offset -215.399994, tx enabled 0, single input mode 0
        INFO: Lora multi-SF channel 0>  radio 1, IF -400000 Hz, 125 kHz bw, SF 5 to 12
        INFO: Lora multi-SF channel 1>  radio 1, IF -200000 Hz, 125 kHz bw, SF 5 to 12
        INFO: Lora multi-SF channel 2>  radio 1, IF 0 Hz, 125 kHz bw, SF 5 to 12
        INFO: Lora multi-SF channel 3>  radio 0, IF -400000 Hz, 125 kHz bw, SF 5 to 12
        INFO: Lora multi-SF channel 4>  radio 0, IF -200000 Hz, 125 kHz bw, SF 5 to 12
        INFO: Lora multi-SF channel 5>  radio 0, IF 0 Hz, 125 kHz bw, SF 5 to 12
        INFO: Lora multi-SF channel 6>  radio 0, IF 200000 Hz, 125 kHz bw, SF 5 to 12
        INFO: Lora multi-SF channel 7>  radio 0, IF 400000 Hz, 125 kHz bw, SF 5 to 12
        INFO: Lora std channel> radio 1, IF -200000 Hz, 250000 Hz bw, SF 7, Explicit header
        INFO: FSK channel> radio 1, IF 300000 Hz, 125000 Hz bw, 50000 bps datarate
        INFO: global_conf.json.sx1250.EU868 does contain a JSON object named gateway_conf, parsing gateway parameters
        INFO: gateway MAC address is configured to AA555A0000000000
        INFO: server hostname or IP address is configured to "192.168.1.110"
        INFO: upstream port is configured to "1700"
        INFO: downstream port is configured to "1700"
        INFO: downstream keep-alive interval is configured to 10 seconds
        INFO: statistics display interval is configured to 30 seconds
        INFO: upstream PUSH_DATA time-out is configured to 100 ms
        INFO: packets received with a valid CRC will be forwarded
        INFO: packets received with a CRC error will NOT be forwarded
        INFO: packets received with no CRC will NOT be forwarded
        INFO: GPS serial port path is configured to "/dev/ttyS0"
        INFO: Reference latitude is configured to 0.000000 deg
        INFO: Reference longitude is configured to 0.000000 deg
        INFO: Reference altitude is configured to 0 meters
        INFO: Beaconing period is configured to 0 seconds
        INFO: Beaconing signal will be emitted at 869525000 Hz
        INFO: Beaconing datarate is set to SF9
        INFO: Beaconing modulation bandwidth is set to 125000Hz
        INFO: Beaconing TX power is set to 14dBm
        INFO: Beaconing information descriptor is set to 0
        INFO: global_conf.json.sx1250.EU868 does contain a JSON object named debug_conf, parsing debug parameters
        INFO: got 2 debug reference payload
        INFO: reference payload ID 0 is 0xCAFE1234
        INFO: reference payload ID 1 is 0xCAFE2345
        INFO: setting debug log file name to loragw_hal.log
        INFO: [main] TTY port /dev/ttyS0 open for GPS synchronization
        Opening SPI communication interface
        Note: chip version is 0x10 (v1.0)
        INFO: using legacy timestamp
        INFO: LoRa Service modem: configuring preamble size to 8 symbols
        ARB: dual demodulation disabled for all SF
        INFO: found temperature sensor on port 0x39
        INFO: [main] concentrator started, packet can now be received
        INFO: concentrator EUI: 0x0016c001f13ffd2d
        WARNING: [gps] GPS out of sync, keeping previous time reference
        WARNING: [gps] GPS out of sync, keeping previous time reference
        INFO: [down] PULL_ACK received in 3 ms
        INFO: [down] PULL_ACK received in 6 ms
        INFO: [down] PULL_ACK received in 3 ms

        ##### 2026-10-14 09:50:27 GMT #####
        ### [UPSTREAM] ###
        # RF packets received by concentrator: 0
        # CRC_OK: 0.00%, CRC_FAIL: 0.00%, NO_CRC: 0.00%
        # RF packets forwarded: 0 (0 bytes)
        # PUSH_DATA datagrams sent: 0 (0 bytes)
        # PUSH_DATA acknowledged: 0.00%
        ### [DOWNSTREAM] ###
        # PULL_DATA sent: 3 (100.00% acknowledged)
        # PULL_RESP(onse) datagrams received: 0 (0 bytes)
        # RF packets sent to concentrator: 0 (0 bytes)
        # TX errors: 0
        ### SX1302 Status ###
        # SX1302 counter (INST): 30721937
        # SX1302 counter (PPS):  30610424
        # BEACON queued: 0
        # BEACON sent so far: 0
        # BEACON rejected: 0
        ### [JIT] ###
        src/jitqueue.c:440:jit_print_queue(): INFO: [jit] queue is empty
        #--------
        src/jitqueue.c:440:jit_print_queue(): INFO: [jit] queue is empty
        ### [GPS] ###
        # Valid time reference (age: 0 sec)
        # no valid GPS coordinates available yet
        ### Concentrator temperature: 26 C ###
        ##### END #####

        JSON up: {"stat":{"time":"2026-10-14 09:50:27 GMT","rxnb":0,"rxok":0,"rxfw":0,"ackr":0.0,"dwnb":0,"txnb":0,"temp":25.7}}
        INFO: [down] PULL_ACK received in 5 ms
        INFO: [up] PUSH_ACK received in 63 ms
        INFO: [down] PULL_ACK received in 24 ms
        INFO: [down] PULL_ACK received in 2 ms

6. Start gateway on OS boot:
  sudo nano /etc/systemd/system/lora-packet-forwarder.service
    Paste and edit if needed:

        [Unit]
        Description=LoRaWAN Semtech UDP Packet Forwarder
        After=network-online.target
        Wants=network-online.target

        [Service]
        Type=simple
        User=root
        WorkingDirectory=/home/piadmin/Documents/lora_gateway/sx1302_hal/bin
        ExecStart=/home/piadmin/Documents/lora_gateway/sx1302_hal/bin/lora_pkt_fwd -c global_conf.json.sx1250.EU868

        Restart=on-failure
        RestartSec=5

        [Install]
        WantedBy=multi-user.target
  
  sudo systemctl daemon-reload
  sudo systemctl enable --now lora-packet-forwarder
    Check status
      sudo systemctl status lora-packet-forwarder
    Log
      sudo journalctl -u lora-packet-forwarder -f

7. Add gateway to ChirpStack app
  Open ChirpStack app located on the server
    Choose tenant
    Open Gateways
      Add new gateway with EUI copied from gateway config (json file), or generate new EUI (in this case copy it to gateway config)
    If OK, "Last seen" will be updated to current timestamp