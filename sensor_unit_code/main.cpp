/*
 * The factory AT firmware must be erased once with an ST-LINK before this code can be flashed!!!
 *
 * Wio-E5 mini + SEN0193 (moisture) + DS18B20 (temperature)
 * LoRaWAN EU868, Class A -> gateway (packet forwarder) -> ChirpStack -> ThingsBoard
 *
 * A3 (PB3) - battery voltage divider (BAT_*)
 * A4 (PB4) - SEN0193 (analog)
 * D9 (PA9) - DS18B20
 * D10 (PB10) - mosfet gate (LOW = sensors ON, HIGH = sensors OFF)
 *
 * Uplink (fPort 1, 9 bytes, big-endian):
 *   [0]    flags  bit0 = temperature error
 *                 bit1 = soil value out of range
 *                 bit2 = low battery
 *   [1..2] soil moisture raw ADC (0..4095, 12 bit)
 *   [3..4] temperature, int16, 0.01 degC (0x8000 = error)
 *   [5..6] battery voltage, mV
 *   [7..8] current measurement interval, minutes
 *
 * Downlink (fPort 10, 2 bytes): new measurement interval in minutes (uint16, BE)
 */

#include <Arduino.h>
#include <stdarg.h>
#include <RadioLib.h>
#include <STM32LowPower.h>
#include <STM32RTC.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "stm32yyxx_ll_adc.h"

// Pins
#define PIN_VBAT            A3
#define PIN_MOISTURE        A4
#define PIN_TEMPERATURE     D9
#define PIN_SENSOR_PWR      D10

// Voltage divider
#define BAT_R1_KOHM         1000.0f // replace with real value get with a multimeter!
#define BAT_R2_KOHM         1000.0f // replace with real value get with a multimeter!
#define LOW_BATT_MV         3700     // Wio-E5 board supply minimum 3.7 V
#define STARTUP_GRACE_MS    5000     // after reset: keep SWD reachable before the first sleep

// Timing
#define DEFAULT_INTERVAL_MIN 60      // one measurement per hour
#define MIN_INTERVAL_MIN     30
#define MAX_INTERVAL_MIN     1440    // 24 h
#define SENSOR_SETTLE_MS     500     // power-up settle time for sensors
#define DS18B20_CONV_MS      800     // 12-bit conversion is max 750 ms

// LoRaWAN
#define FPORT_DATA           1
#define FPORT_CONFIG         10
#define MAX_JOIN_ATTEMPTS    3
#define MAX_TX_ATTEMPTS      3
#define TX_RETRY_DELAY_MS    8000
#define MAX_FAILED_CYCLES    3       // after this many failed cycles -> re-join

// Over-the-Air Activation (OTAA) credentials. For LoRaWAN 1.0.x NWK_KEY = APP_KEY
// REPLACE WITH DEVICE CREDENTIALS THAT MUST BE SAME AS IN CHIRPSTACK!
static uint64_t JOIN_EUI = 0x0000000000000000ULL;
static uint64_t DEV_EUI  = 0x0000000000000000ULL;
static uint8_t  APP_KEY[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static uint8_t  NWK_KEY[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Debug (set to 0 for production!)
#define DEBUG_SERIAL 1

#if DEBUG_SERIAL
static void dbg(const char *fmt, ...) {
  char b[112];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  Serial.println(b);
  Serial.flush();
}
#else
#define dbg(...) do {} while (0)
#endif

// RADIO

// Wio-E5 RF switch: PA4 / PA5.  RX = (1,0), TX (high power path) = (0,1).
// Compare with the RadioLib "STM32WLx" example if Wio-E5 board revision differs
STM32WLx radio = new STM32WLx_Module();

static const uint32_t rfswitch_pins[] = {PA4, PA5, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};
static const Module::RfSwitchMode_t rfswitch_table[] = {
  {STM32WLx::MODE_IDLE,  {LOW,  LOW}},
  {STM32WLx::MODE_RX,    {HIGH, LOW}},
  {STM32WLx::MODE_TX_LP, {LOW,  HIGH}},
  {STM32WLx::MODE_TX_HP, {LOW,  HIGH}},
  END_OF_MODE_TABLE,
};

const LoRaWANBand_t Region = EU868;
LoRaWANNode node(&radio, &Region);

// SENSORS

OneWire           oneWire(PIN_TEMPERATURE);
DallasTemperature tempSensor(&oneWire);

// STATE

// Variables stay in RAM during Stop2 sleep (the MCU does not reset).
static uint16_t intervalMin   = DEFAULT_INTERVAL_MIN;
static bool     joined        = false;
static uint8_t  failedCycles  = 0;

// HELPERS

static void sensorsPower(bool on) {
  // P-MOSFET high-side switch: gate LOW = sensors ON
  digitalWrite(PIN_SENSOR_PWR, on ? LOW : HIGH);
}

static uint32_t readVddMv() {
  // Actual VDD (= ADC reference) from the internal VREFINT channel
#ifdef __LL_ADC_CALC_VREFANALOG_VOLTAGE
  uint32_t raw = 0;
  analogRead(AVREF);
  for (int i = 0; i < 8; i++) raw += analogRead(AVREF);
  raw /= 8;
  if (raw > 0) return __LL_ADC_CALC_VREFANALOG_VOLTAGE(raw, LL_ADC_RESOLUTION_12B);
#endif
  return 3300;
}

static uint16_t readAdcAvg(uint32_t pin, uint8_t n) {
  analogRead(pin);
  uint32_t sum = 0;
  for (uint8_t i = 0; i < n; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  return (uint16_t)(sum / n);
}

static bool readTemperature(float &tC) {
  tempSensor.begin();
  if (tempSensor.getDeviceCount() == 0) return false;
  tempSensor.setWaitForConversion(false);
  for (uint8_t attempt = 0; attempt < 3; attempt++) {
    tempSensor.requestTemperatures();
    delay(DS18B20_CONV_MS);
    float c = tempSensor.getTempCByIndex(0);
    // 85.0 = power-on reset value (conversion not finished), -127 = disconnected
    if (c != DEVICE_DISCONNECTED_C && c != 85.0f) {
      tC = c;
      return true;
    }
    delay(100);
  }
  return false;
}

static uint16_t readBatteryMv(uint32_t vddMv) {
  uint16_t raw = readAdcAvg(PIN_VBAT, 16);
  float pinMv = (float)raw * (float)vddMv / 4095.0f;
  return (uint16_t)(pinMv * (BAT_R1_KOHM + BAT_R2_KOHM) / BAT_R2_KOHM);
}

static size_t composePayload(uint8_t *p, uint16_t soilRaw, bool tempOk, float tC,
                             uint16_t vbatMv) {
  uint8_t flags = 0;
  if (!tempOk)                          flags |= 0x01;
  if (soilRaw < 100 || soilRaw > 4000)  flags |= 0x02;   // sensor unplugged / shorted
  if (vbatMv < LOW_BATT_MV)             flags |= 0x04;

  int16_t t100 = tempOk ? (int16_t)lroundf(tC * 100.0f) : (int16_t)0x8000;

  p[0] = flags;
  p[1] = soilRaw >> 8;            p[2] = soilRaw & 0xFF;
  p[3] = ((uint16_t)t100) >> 8;   p[4] = ((uint16_t)t100) & 0xFF;
  p[5] = vbatMv >> 8;             p[6] = vbatMv & 0xFF;
  p[7] = intervalMin >> 8;        p[8] = intervalMin & 0xFF;
  return 9;
}

static bool joinNetwork() {
  node.beginOTAA(JOIN_EUI, DEV_EUI, NWK_KEY, APP_KEY);
  for (uint8_t i = 0; i < MAX_JOIN_ATTEMPTS; i++) {
    int16_t st = node.activateOTAA();
    if (st == RADIOLIB_LORAWAN_NEW_SESSION || st == RADIOLIB_LORAWAN_SESSION_RESTORED) {
      dbg("Joined (%d)", st);
      return true;
    }
    dbg("Join failed (%d), attempt %u", st, i + 1);
    delay(10000);
  }
  return false;
}

static void handleDownlink(const uint8_t *d, size_t len, uint8_t fPort) {
  if (fPort == FPORT_CONFIG && len >= 2) {
    uint16_t v = ((uint16_t)d[0] << 8) | d[1];
    if (v >= MIN_INTERVAL_MIN && v <= MAX_INTERVAL_MIN) {
      intervalMin = v;
      dbg("New interval: %u min", intervalMin);
    } else {
      dbg("Ignoring interval %u (out of range)", v);
    }
  }
}

// Confirmed uplink; success = ACK received from the network server.
static bool sendUplink(const uint8_t *payload, size_t len) {
  for (uint8_t attempt = 1; attempt <= MAX_TX_ATTEMPTS; attempt++) {
    uint8_t dl[16];
    size_t dlLen = 0;
    LoRaWANEvent_t evUp, evDown;

    int16_t st = node.sendReceive(payload, len, FPORT_DATA, dl, &dlLen, true, &evUp, &evDown);

    if (st > 0) {                             // downlink received in RX1 (1) or RX2 (2)
      if (dlLen > 0) handleDownlink(dl, dlLen, evDown.fPort);
      if (evDown.confirming) {                // our uplink was acknowledged
        dbg("TX OK, ACK in RX%d", st);
        return true;
      }
    }
    dbg("TX attempt %u: no ACK (state %d)", attempt, st);
    if (attempt < MAX_TX_ATTEMPTS) delay(TX_RETRY_DELAY_MS);
  }
  return false;
}

static void sleepSeconds(uint32_t sec) {
  while (sec > 0) {
    uint32_t chunk = (sec > 3600UL) ? 3600UL : sec;
    LowPower.deepSleep(chunk * 1000UL);       // RTC (LSE) alarm wake-up, Stop2 mode
    sec -= chunk;
  }
}

// SETUP

void setup() {
  // Sensors OFF before anything else (write level first, then make it an output)
  digitalWrite(PIN_SENSOR_PWR, HIGH);
  pinMode(PIN_SENSOR_PWR, OUTPUT);

#if DEBUG_SERIAL
  Serial.begin(115200);
#endif

  STM32RTC &rtc = STM32RTC::getInstance();
  rtc.setClockSource(STM32RTC::LSE_CLOCK);   // 32.768 kHz crystal on the Wio-E5
  rtc.begin();
  LowPower.begin();

  radio.setRfSwitchTable(rfswitch_pins, rfswitch_table);
  dbg("Wio-E5 soil node start");

  // The real BOOT0 pin is not exposed on the Wio-E5, so once the firmware sleeps
  // constantly the ST-LINK may fail to connect. This window after every reset keeps
  // the chip awake (you can also hold RESET while connecting).
  delay(STARTUP_GRACE_MS);
}

// LOOP

void loop() {
  // STEP 1: wake up from power-saving mode
  // (first pass = power-on; later passes = return from LowPower.deepSleep())
  dbg("--- wake-up ---");

  // STEP 2: prepare connections (GPIO, ADC, LoRaWAN)
  digitalWrite(PIN_SENSOR_PWR, HIGH);
  pinMode(PIN_SENSOR_PWR, OUTPUT);
  pinMode(PIN_MOISTURE, INPUT_ANALOG);
  pinMode(PIN_VBAT, INPUT_ANALOG);
  analogReadResolution(12);

  // Cold-start the radio after every wake-up (robust after Stop2).
  // Args: freq, bw, sf, cr, syncword(LoRaWAN), power, preamble, TCXO 1.7 V, LDO=false (SMPS/DC-DC)
  int16_t rs = radio.begin(868.1, 125.0, 9, 7, RADIOLIB_LORAWAN_LORA_SYNC_WORD,
                           10, 8, 1.7, false);
  bool radioOk = (rs == RADIOLIB_ERR_NONE);
  if (!radioOk) dbg("Radio init failed: %d", rs);

  // STEP 3: power on the sensors
  sensorsPower(true);

  // STEP 4: wait for the sensors to stabilize
  delay(SENSOR_SETTLE_MS);

  // STEP 5: read soil moisture
  uint16_t soilRaw = readAdcAvg(PIN_MOISTURE, 16);
  dbg("Soil raw: %u", soilRaw);

  // STEP 6: read temperature
  float tC = 0;
  bool tempOk = readTemperature(tC);
  dbg("Temp ok=%d  %d.%02d C", tempOk, (int)tC, abs((int)(tC * 100)) % 100);

  // STEP 7: measure battery voltage
  uint32_t vddMv = readVddMv();
  uint16_t vbatMv = readBatteryMv(vddMv);
  dbg("VDD %lu mV, battery %u mV", (unsigned long)vddMv, vbatMv);

  // STEP 8 power off the sensors
  pinMode(PIN_TEMPERATURE, OUTPUT);
  digitalWrite(PIN_TEMPERATURE, LOW);
  sensorsPower(false);

  // STEP 9: compose the message
  uint8_t payload[16];
  size_t len = composePayload(payload, soilRaw, tempOk, tC, vbatMv);

  // STEP 10: send via LoRaWAN and check the result
  bool txOk = false;
  if (radioOk) {
    if (!joined) joined = joinNetwork();
    if (joined) txOk = sendUplink(payload, len);   // waits for ACK
  }
  if (txOk) {
    failedCycles = 0;
  } else {
    dbg("Transmission FAILED");
    if (++failedCycles >= MAX_FAILED_CYCLES) {     // session probably lost -> re-join next time
      joined = false;
      failedCycles = 0;
    }
  }

  // STEP 12: back to power-saving mode
  radio.sleep();
  dbg("Sleeping %u min", intervalMin);
  sleepSeconds((uint32_t)intervalMin * 60UL);
}