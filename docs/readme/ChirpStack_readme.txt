### ChirpStack Configuration using UI (OTAA authorization) ###

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

  Paste two JS functions decodeUplink(input) and encodeDownlink(input) from codec.js file

5. Add device
  Tenant → Applications → App-Name → Devices → Add Device
  Example:
    1 Name: Field01Node01 or F01N01
    2 DevEUI: f304cb294d0d3489
    3 Device profile: Tenant -> Profile-Name

6. Configure/Generate OTAA keys (copy them to field sensor node's firmware or simulator)
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

8. Create gateway
  Gateways → Add Gateway
  Example:
    Name: TestGateway
    Gateway ID: dea72cabaa3c116a
    Tenant: Tenant-Name



### ChirpStack Configuration using UI (ABP authorization instead OTAA) ###
(tested in simulation mode, test successful)

0. Create Tenant and Application if needed (see instructions from first section above (OTAA configuration))

1. Create the device profile
  Tenant → Device Profiles → Add device profile
    General tab:
      Name: e.g. Wio-E5-ABP
      Region: EU868
      MAC version: LoRaWAN 1.0.3 (must match what firmware/simulator uses)
      Regional parameters revision: B (or whatever matches MAC version)
      Leave ADR algorithm at default
    OTAA/ABP tab:
      Uncheck "Device supports OTAA" — that's makes it ABP
      Fill in the ABP RX parameters that appear:
        RX1 delay: 1 (second)
        RX1 data rate offset: 0
        RX2 data rate: 0
        RX2 frequency: 869525000 (Hz, EU868 default)
    Factory-preset frequencies: leave default/empty unless device profile template requires specific ones
    Codec tab: select Custom JavaScript functions and paste two JS functions decodeUplink(input) and encodeDownlink(input) from codec.js file

  Click Submit.

2. Create the device using that profile
  Application → Add device.
    Fill in or generate a Device EUI (any 16 hex chars) and select created above ABP device profile
    
  Click Submit.

3. Activate device (ABP session keys)
  Open the device's Activation tab
    Enter (or generate) the DevAddr, Network session key (NwkSKey), and Application session key (AppSKey).
    Click (Re)Activate device.

    These keys must be used for simulation (for example, copy values into DEV_ADDR, NWK_SKEY, APP_SKEY in simulator_crypto_abp.py)

4. Add the gateway
  Gateways → Add gateway 
    Set own or generate the Gateway ID 
    Region: EU868
  
  ID must be used for simulation (for example, paste as GATEWAY_EUI to simulator_crypto_abp.py)

Once all four IDs/keys match between ChirpStack and the script, we can run the simulator and uplinks should appear under the device's Events/Live LoRaWAN frames tab



### Simulate field node data and gateway in ABP mode ###
(tested!)
1. Run simulator_crypto_abp.py
2. Check device events and frames in UI (gateway is offline, because it does not simulate stat)



### Simulate gateway statistics ###
1. Run simulator_gateway_stat
2. Check log for packets
  sudo docker logs -f lw-gateway-bridge
3. Check UI:
    Gateways → Gateway-Name (must be online)

### Test with python code (simulate Semtech UDP) ###
1. Run simulator_simple_udp
2. Check log for packets
  sudo docker logs -f lw-gateway-bridge
3. Check UI for data



### ChirpStack simulator setup and configuration ###
(TEST SIMULATION FAILED for unknown reason, simulator does not create gateway and payload does not get to MQTT)

1. Install Go

2. Download simulator
  git clone https://github.com/brocaar/chirpstack-simulator.git
  cd chirpstack-simulator

3. Build simualator and create config
  make build

  ./build/chirpstack-simulator configfile > simulator.toml

4. Configure simulator 
  cp simulator.toml simulator-default-copy.toml
  :> simulator.toml     (clear file text)
  nano simulator.toml

  Paste this and edit api_key, tenant_id, server ip and ports

  [general]
  log_level=5

  [chirpstack.api]
  api_key="YOUR_API_KEY"
  server="192.168.1.110:9080"
  insecure=true

  [chirpstack.integration.mqtt]
  server="tcp://192.168.1.110:1883"

  [chirpstack.gateway.backend.mqtt]
  server="tcp://192.168.1.110:1883"

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
  ./build/chirpstack-simulator --config simulator.toml 

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