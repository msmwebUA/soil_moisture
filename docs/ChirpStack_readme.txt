### ChirpStack Configuration using UI ###

1. Add tenant
  Name: Tenant-Name
  Can have gateways: yes	
  Private gateways (uplink): no
  Private gateways (down): no
  Max. gateways: unlimited
  Max. devices: unlimited

2. Create application for new tenant
  Name: App-Name

3. Create device profile for new tenant
  Device Profiles → Add device profile
  Example:
    1 Name: Profile-Name
    2 Region: EU868
    3 MAC Version: LoRaWAN 1.1.0
    4 Regional Parameters: RP002-1.0.3

    5 Supports OTAA: YES (OTAA tab)

    6 Class: A (turn off Class B and Class C if turned on, see tabs)

4. Create payload decoder (add codec)
  Device Profiles → Profile-Name → Payload codec

  If WioE5 firmware sends payload in this format:
    Byte 0   flags
    Byte 1-2 soil moisture
    Byte 3-4 temperature *100
    Byte 5-6 battery mV
    Byte 7-8 interval minutes

  JS function for decoding payload above: 
    function decodeUplink(input) {
      const b = input.bytes;
      let temp = (b[3] << 8) | b[4];
      if (temp > 32767) temp -= 65536;
      return {
        data: {
          flags: b[0],
          moisture: (b[1] << 8) | b[2],
          temperature: temp / 100.0,
          batteryMilliV: (b[5] << 8) | b[6],
          intervalMinutes: (b[7] << 8) | b[8]
        }
      };
    }

5. Add device
  Tenant → Applications → App-Name → Devices → Add Device
  Example:
    1 Name: Field01Node01 or F01N01
    2 DevEUI: f304cb294d0d3489
    3 Device profile: Tenant -> Profile-Name

  Configure/Generate OTAA keys (copy them to field sensor node's firmware or simulator)
  Device → Configuration / OTAA Keys
  Example:
    JoinEUI: 7e2d336eb3c67da8 (configuration tab)
    Application Key: e9939f8a400e6fb1a45bcbf5c47e5205 (OTAA keys tab)
    Network Key: fff2061727d150b6ea1673b4f00af135 (OTAA keys tab)

6. Create API Token (this token can be used by simulators and API clients)
  Tenant → API Keys → Add API Key
  Name: Token-Name

  Copy and save generated token

7. Gateway packet forwarders should be configured with:
  Server address = ChirpStack server IP
  Port up = 1700 // ChirpStack Gateway Bridge listens on this port
  Port down = 1700


### ChirpStack simulator setup and configuration ###

1. Install Go

2. Download simulator
  git clone https://github.com/brocaar/chirpstack-simulator.git
  cd chirpstack-simulator

3. Build simualator and create config
  make build

  ./build/chirpstack-simulator configfile > simulator.toml

4. Configure simulator

  [general]
  log_level=4

  [chirpstack.api]
  api_key="YOUR_API_KEY"
  server="192.168.1.110:9080"
  insecure=true

  [chirpstack.integration.mqtt]
  server="tcp://192.168.1.110:1883"

  [chirpstack.gateway.backend.mqtt]
  server="tcp://192.168.1.100:1883"

  [[simulator]]

  tenant_id="YOUR_TENANT_ID"

  duration="0s"

  activation_time="30s"

  [simulator.device]

  count=1

  uplink_interval="60s"

  f_port=1

  payload="0004D708CA0F6E003C"

  frequency=868100000

  bandwidth=125000

  spreading_factor=7

  *** Note: payload="0004D708CA0F6E003C" means this encoded data:
        flags          0
        moisture       1239
        temperature    22.50    C
        battery        3950     mV
        interval       60.      min

5. Start simulator
  ./build/chirpstack-simulator simulator.toml

6. Check OTAA (in ChirpStack UI)
  Applications → Device → LoRaWAN Frames

  Expected:
    JoinRequest
    JoinAccept
    UnconfirmedDataUp

7. Verify decoded sensor data
  Applications → Device → Events

  Expected:
    {
      "moisture": 1239,
      "temperature": 22.5,
      "batteryMilliV": 3950,
      "intervalMinutes": 60
    }

8. Verify MQTT messages
  Run on the server:
    mosquitto_sub -v -t '#'
  or
    mosquitto_sub -v -t 'application/+/device/+/event/up'

9. Verify gateway activity
  Gateways → Gateway

  Expected:
    Last Seen: now
    Received Frames: increasing