// ChirpStack v4 device-profile codec (JavaScript) for the field sensor node.
// Paste into: Device profile -> Codec -> "JavaScript functions".

// Sensor node's firmware sends payload in this format:
//    Byte 0   flags
//    Byte 1-2 soil moisture
//    Byte 3-4 temperature *100
//    Byte 5-6 battery mV
//    Byte 7-8 interval minutes

// --- CALIBRATE THESE for moisture sensor (raw ADC values, 12 bit, 3.3 V) ---
var SOIL_RAW_AIR   = 3000;  // sensor in dry air     -> 0 %
var SOIL_RAW_WATER = 1500;  // sensor in water       -> 100 %

// Uplink, fPort 1: [flags, moisture_hi, moisture_lo, temp_hi, temp_lo, battery_hi, battery_lo, interval_hi, interval_lo]
function decodeUplink(input) {
  var b = input.bytes;
  if (input.fPort !== 1 || b.length < 9) {
    return { errors: ["unexpected fPort or payload length"] };
  }
  var flags = b[0];
  var moisture  = (b[1] << 8) | b[2];
  var temp  = (b[3] << 8) | b[4];
  if (temp & 0x8000) temp -= 0x10000;
  var battery  = (b[5] << 8) | b[6];
  var interval = (b[7] << 8) | b[8];

  var pct = (SOIL_RAW_AIR - moisture) * 100 / (SOIL_RAW_AIR - SOIL_RAW_WATER);
  pct = Math.max(0, Math.min(100, pct));

  var data = {
    moisture_raw: soil,
    moisture_pct: Math.round(pct * 10) / 10,
    battery_v: battery / 1000,
    interval_min: interval,
    temp_error: (flags & 0x01) !== 0,
    moisture_error: (flags & 0x02) !== 0,
    low_battery: (flags & 0x04) !== 0
  };
  if (temp !== -32768) {
    data.temperature = temp / 100;
  }
  return { data: data };
}

// Downlink: {"interval_min": 30} -> 2 bytes (sent on fPort 10)
function encodeDownlink(input) {
  var m = input.data.interval_min;
  if (typeof m !== "number" || m < 1 || m > 1440 || Math.floor(m) !== m) {
    return { errors: ["interval_min must be an integer 1..1440"] };
  }
  return { bytes: [(m >> 8) & 0xFF, m & 0xFF] };
}