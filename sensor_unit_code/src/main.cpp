/*
* ! Before flashing first time: factory AT firmware must be erased and write protection disabled once with an st-link v2 programmer and STM32CubeProgrammer
*
* Wio-E5 mini + SEN0193 (moisture) + DS18B20 (temperature)
*
* A3 (PB3) - battery voltage
* A4 (PB4) - SEN0193 (analog)
* D9 (PA9) - DS18B20
* D10 (PB10) - mosfet gate (LOW = sensors ON, HIGH = sensors OFF)
*
* Uplink (fPort 1, 9 bytes):
*   [0] flags bit0 = temperature error
*             bit1 = moisture value out of range
*             bit2 = low battery
*   [1..2] soil moisture raw ADC (0..4095, 12 bit)
*   [3..4] temperature (int), 0.01 C x 100 (0x8000 = error)
*   [5..6] battery voltage (int), mV
*   [7..8] current measurement interval (int), minutes
*
* Downlink (fPort 10, 2 bytes): new measurement interval in minutes
*/

// CHANGE TIMING FOR STM32WL/Wio-E5 IN LORAWAN PROTOCOL FILE (LoRaWAN.cpp)
// // calculate the timeout of an empty packet plus scanGuard
// RadioLibTime_t timeoutUs = toaMinUs + this->scanGuard * 1000;
// // Experimental minimum RX timeout for STM32WL/Wio-E5 (fix timing issues)
// if(timeoutUs < 150000UL) {
//   timeoutUs = 150000UL;
// }

#include <Arduino.h>
#include "TypeDef.h"
#include <EEPROM.h>
#include <cstdint>
#include <stdarg.h>
#include <cstring>
#include <STM32LowPower.h>
#include <STM32RTC.h>
#include <stm32yyxx_ll_adc.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "protocols/PhysicalLayer/PhysicalLayer.h" // Lora Physical Layer (RadioLib)
#include <loraConfig.h>

// Pins
#define PIN_BATTERY      A3  // PB3
#define PIN_MOISTURE     A4  // PB4
#define PIN_TEMPERATURE  D9  // PA9
#define PIN_MOSFET       D10 // PB10

// Time
#define DEFAULT_INTERVAL  60 // measurement interval, minutes
#define MIN_INTERVAL      2 // ! minimum duty cycle is 1% (max 36 sec transmission per hour) regulated by Traficom
#define MAX_INTERVAL      1440
#define SENSOR_SETTLE_MS  500 // wait for sensors settle, milliseconds
#define DS18B20_DELAY_MS  800 // delay for temp sensor has time to converse (750 ms is max for 12 bit ADC), milliseconds

// Voltage divider
#define DIV_R1_KOHM       963.0f // kOhm
#define DIV_R2_KOHM       1005.0f // kOhm
#define LOW_BATT_MV       3700 // min voltage for Wio, millivolts
#define STARTUP_DELAY_MS  5000 // keeps Wio awake after restart otherwize cant establish connection via st-link, milliseconds

// LORA
#define FPORT_DATA         1
#define FPORT_CONFIG       10
#define MAX_ACTIVATE_ATTEMPTS  1
#define MAX_TX_ATTEMPTS    1 
#define ACTIVATE_RETRY_DELAY_MS  5000 // milliseconds
#define TX_RETRY_DELAY_MS  5000 // milliseconds

// Temperature sensor (one wire data)
OneWire oneWire(PIN_TEMPERATURE);
DallasTemperature tempSensor(&oneWire);

// Settings (measurement interval, devnonces etc)
// uint8_t savedSession[RADIOLIB_LORAWAN_SESSION_BUF_SIZE]; // Buffer for session keys & counters
struct DeviceSettings {
  uint16_t flag; // flag to know settings have already written to memory
  uint16_t measInterval;
  // lorawan settings
  bool hasSavedNonces; // Flag to check if we can skip fresh OTAA
  uint8_t savedNonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE]; // Buffer for DevNonce and JoinNonce
};
DeviceSettings settings;
const uint16_t EEPROM_FLAG = 0xABCD; // dummy flag (ID)
const int EEPROM_START_ADDR = 0;

// Flags, counters
static uint8_t failedCycles = 0;
bool radioOk = false;
bool otaaOk = false;

// Custom debug
#define DEBUG_ENABLE 1 // on/off debug to serial monitor

#if DEBUG_ENABLE
  static void debug(const char *fmt, ...) {
    char b[112];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    Serial.println(b);
    Serial.flush();
  }
#else
#define debug(...) do {} while (0)
#endif

// functions
static void sensorPower(bool power);
static uint32_t readVdd();
static uint16_t readBattery(uint32_t vdd);
static uint16_t readAdcAvg(uint32_t pin, uint8_t n);
static bool readTemperature(float &tC);
static size_t composePayload(uint8_t *p, uint16_t moistureRaw, bool tempOk, float tempC, uint16_t battVoltage);
static bool activateOtaa();
static void saveNonces();
static bool sendUplink(const uint8_t *payload, size_t len);
static void handleDownlink(const uint8_t *d, size_t len, uint8_t fPort);
static void sleepSeconds(uint32_t sec);


void setup() {
  // Sensors off on load (p-channel high = off)
  digitalWrite(PIN_MOSFET, HIGH);
  pinMode(PIN_MOSFET, OUTPUT);

  // Debug
  #if DEBUG_ENABLE
    Serial.begin(115200);
    while (!Serial) {
      delay(100);
    }
  #endif

  // Init radio
  ConfigLoRa_t config;
  config.frequency = 868; // The frequency here does not matter, as it will get changed by LoRaWAN anyway
  radio.setRfSwitchTable(rfswitch_pins, rfswitch_table);
  int16_t state = radio.begin(config);
  if (state == RADIOLIB_ERR_NONE) {
    radioOk = true;
    debug("Radio is ready");
  } else {
    loraDebug(true, F("Radio init failed"), state, true);
  }
  // Setup the OTAA session information
  state = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
  if (state == RADIOLIB_ERR_NONE) {
    otaaOk = true;
    debug("OTAA init successful");
  } else {
    loraDebug(true, F("OTAA init failed"), state, true);
  }

  // Load settings from emulated EEPROM (write if first boot)
  EEPROM.get(EEPROM_START_ADDR, settings);
  if (settings.flag != EEPROM_FLAG) {
    settings.flag = EEPROM_FLAG;
    settings.measInterval = DEFAULT_INTERVAL;
    settings.hasSavedNonces = false;
    EEPROM.put(EEPROM_START_ADDR, settings);
    debug("Settings saved to memory");
  } else {
    // validate and restore interval
    if (settings.measInterval < MIN_INTERVAL || settings.measInterval > MAX_INTERVAL) {
      settings.measInterval = DEFAULT_INTERVAL;
      EEPROM.put(EEPROM_START_ADDR, settings);
    }
    debug("Settings loaded from memory. Measurement interval: %d", settings.measInterval);
  }

  // Restore lora nonces if available
  if (settings.hasSavedNonces) {
    int16_t state = node.setBufferNonces(settings.savedNonces);
    loraDebug(state != RADIOLIB_ERR_NONE, F("Restoring nonces buffer failed"), state, false);
  }
  
  // Clock
  STM32RTC &rtc = STM32RTC::getInstance();
  rtc.setClockSource(STM32RTC::LSE_CLOCK); // external low power 32.768 kHz crystal on the Wio
  rtc.begin();

  // Energy management
  LowPower.begin();

  // ADC resolution set to 12 (default is 10 bit for Arduino framework)
  analogReadResolution(12);
  
  debug("Node setup successful");
  delay(STARTUP_DELAY_MS); // give time for stlink to connect board before sleep
}

void loop() {
  debug("Main loop iteration...");

  // 1 - prepare pins and connections
  digitalWrite(PIN_MOSFET, HIGH); // power off sensors
  pinMode(PIN_MOSFET, OUTPUT);
  pinMode(PIN_MOISTURE, INPUT_ANALOG);
  pinMode(PIN_BATTERY, INPUT_ANALOG);
  
  // 2 - power on sensors
  sensorPower(true);
  delay(SENSOR_SETTLE_MS);

  // 3 - measurements
  // 3.1 - soil moisture
  uint16_t moistureRaw = readAdcAvg(PIN_MOISTURE, 16);
  debug("Soil moisture raw: %u", moistureRaw);
  // 3.2 - temperature
  float tempC = 0;
  bool tempOk = readTemperature(tempC);
  debug("Temp ok=%d %d,%02d C", tempOk, (int)tempC, abs((int)(tempC * 100)) % 100);
  // 3.1 - battery
  uint32_t vdd = readVdd();
  uint16_t battVoltage = readBattery(vdd);
  debug("VDD %lu mV, battery %u mV", (unsigned long)vdd, battVoltage);

  // 4 - power off sensors
  digitalWrite(PIN_TEMPERATURE, LOW);
  pinMode(PIN_TEMPERATURE, OUTPUT);
  sensorPower(false);

  // 5 - prepare data
  uint8_t payload[16];
  size_t len = composePayload(payload, moistureRaw, tempOk, tempC, battVoltage);

  // 6 - send uplink and handle downlink
  bool txOk = false;
  bool activated = false;
  if (radioOk && otaaOk) {
    activated = activateOtaa();
    if (activated) txOk = sendUplink(payload, len); // tries to send max_attempts times and waits for ACK
  }
  if (txOk) {
    failedCycles = 0;
  } else {
    failedCycles++;
    debug("Transmission failed %d time(s)", failedCycles);
  }

  // 8 - sleep again
  radio.sleep();
  debug("MCU goes to sleep for %u minutes", settings.measInterval);
  sleepSeconds((uint32_t)settings.measInterval * 60UL);
}

// FUNCTIONS

static void sensorPower(bool power) {
  digitalWrite(PIN_MOSFET, power ? LOW : HIGH); // p-channel mosfet: low = sensors power on
}

static uint32_t readVdd() {
  // Read MCU VDD that can be used as ADC reference for battery voltage measurement
  #if defined (__LL_ADC_CALC_VREFANALOG_VOLTAGE) && defined(AVREF)
    analogRead(AVREF); // stabilize ADC,  AVREF - internal channel
    uint32_t raw = 0;
    for (uint8_t i = 0; i < 8; i++) raw += analogRead(AVREF); 
    raw /= 8; // avg
    if (raw > 0) return __LL_ADC_CALC_VREFANALOG_VOLTAGE(raw, LL_ADC_RESOLUTION_12B);
  #else
    debug("__LL_ADC_CALC_VREFANALOG_VOLTAGE or AVREF not defined");
  #endif
  return 3300; // return default 3.3V if __LL_ADC_CALC_VREFANALOG_VOLTAGE or AVREF is not applicable
}

static uint16_t readAdcAvg(uint32_t pin, uint8_t n) {
  analogRead(pin); // stabilize ADC for pin
  uint32_t sum = 0;
  for (uint8_t i = 0; i < n; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  return (uint16_t)(sum / n);
}

static uint16_t readBattery(uint32_t vRef) {
  uint16_t adcRaw = readAdcAvg(PIN_BATTERY, 16);
  float pinVoltage = (float)adcRaw * (float)vRef / 4095.0f; // millivolts, 4095 - 12 bit ADC
  return (uint16_t)(pinVoltage * (DIV_R1_KOHM + DIV_R2_KOHM) / DIV_R2_KOHM); // calculate Vin in millivolts
}

static bool readTemperature(float &tempC) {
  tempSensor.begin();
  if (tempSensor.getDeviceCount() == 0) return false; // sensor not connected
  tempSensor.setWaitForConversion(false);
  for (uint8_t i = 0; i < 3; i++) {
    tempSensor.requestTemperatures();
    delay(DS18B20_DELAY_MS);
    float c = tempSensor.getTempCByIndex(0);
    if (c != DEVICE_DISCONNECTED_C && c != 85.0f) { // 85.0 = conversion not finished, -127 = disconnected
      tempC = c;
      return true;
    }
    delay(100);
  }
  return false;
}

static size_t composePayload(uint8_t *p, uint16_t moistureRaw, bool tempOk, float tempC, uint16_t battVoltage) {
  // flags
  uint8_t flags = 0;
  if (!tempOk) flags |= 0x01;
  if (moistureRaw < 100 || moistureRaw > 4000) flags |= 0x02; // moisture sensor unplugged or shorted
  if (battVoltage < LOW_BATT_MV) flags |= 0x04;
  // temperature
  int16_t temp = tempOk ? (int16_t)lroundf(tempC * 100.0f) : (int16_t)0x8000; // 0x8000 means error
  // payload (16 bit values splitted into 8+8 Big-Endian)
  p[0] = flags;
  p[1] = moistureRaw >> 8; // bitwize right shift extracts upper (most significant) 8 bits    
  p[2] = moistureRaw & 0xFF; // bitwize AND extracts lower (less significant) 8 bits
  p[3] = ((uint16_t)temp) >> 8;   
  p[4] = ((uint16_t)temp) & 0xFF;
  p[5] = battVoltage >> 8;             
  p[6] = battVoltage & 0xFF;
  p[7] = settings.measInterval >> 8;        
  p[8] = settings.measInterval & 0xFF;
  return 9; // number of bytes in payload
}

static bool activateOtaa() {
  if (node.isActivated()) return true; // already joined
  // try to join
  int16_t state = RADIOLIB_ERR_NETWORK_NOT_JOINED;
  for (uint8_t i = 0; i < MAX_ACTIVATE_ATTEMPTS; i++) {
    state = node.activateOTAA();
    if (state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
      debug("Session restored");
      return true;
    }
    if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
      debug("New OTAA session created");
      saveNonces(); // save Nonces buffer to EEPROM (if changed)
      // memcpy(savedSession, node.getBufferSession(), sizeof(savedSession)); // save copy of session buffer to RAM
      return true;
    }
    debug("Attempt %u: failed to activate OTAA", i+1);
    loraDebug(true, F("[LoRaWAN Error]"), state, false);
    if (i < MAX_ACTIVATE_ATTEMPTS - 1) delay(ACTIVATE_RETRY_DELAY_MS);
  }
  return false;
}

static void saveNonces() {
  uint8_t newNonces[sizeof(settings.savedNonces)];
  memcpy(newNonces, node.getBufferNonces(), sizeof(newNonces)); // get pointer to Nonces buffer and copy it
  bool noncesChanged = memcmp(settings.savedNonces, newNonces, sizeof(newNonces)) != 0; // compare current buffer with saved
  if (!noncesChanged) {
    debug("Nonces buffer unchanged, memory write skipped");
    return;
  } else {
    memcpy(settings.savedNonces, newNonces, sizeof(newNonces));
    settings.hasSavedNonces = true;
    EEPROM.put(EEPROM_START_ADDR, settings);
    debug("Updated Nonces buffer saved to memory");
  }
}

static bool sendUplink(const uint8_t *payload, size_t len) {
  for (uint8_t i = 0; i < MAX_TX_ATTEMPTS; i++) {
    uint8_t downlink[16];
    size_t downlinkLen = 0;
    LoRaWANEvent_t evUp, evDown;
    // send uplink and handle downlink
    int16_t state = node.sendReceive(payload, len, FPORT_DATA, downlink, &downlinkLen, true, &evUp, &evDown);
    // validation (state 0 = no downlink, state 1 or 2 = downlink in window Rx1 or Rx2)
    if (state < 0) {
      debug("Uplink transmission attempt %u failed", i+1);
      loraDebug(true, F("[LoRaWAN Error]"), state, false);
    } else {
      if (downlinkLen > 0) handleDownlink(downlink, downlinkLen, evDown.fPort);
      if (evDown.confirming) {
        debug("Confirmed uplink ACK received in RX%d", state);
        return true;
      }
      debug("Uplink sent, but ACK not received");
    }
    // memcpy(savedSession, node.getBufferSession(), sizeof(savedSession));; // save copy of session to RAM (updated frame counters, MAC state)
    if (i < MAX_TX_ATTEMPTS - 1) delay(TX_RETRY_DELAY_MS);
  }
  return false;
}

static void handleDownlink(const uint8_t *d, size_t len, uint8_t fPort) {
  if (fPort == FPORT_CONFIG && len >= 2) {
    uint16_t v = ((uint16_t)d[0] << 8) | d[1]; // unites 2 separate bytes from downlink back into 16 bit integer
    if (v == settings.measInterval) {
      debug("Same interval already saved: %u minutes", v);
    } else if (v >= MIN_INTERVAL && v <= MAX_INTERVAL) {
      settings.measInterval = v;
      EEPROM.put(EEPROM_START_ADDR, settings); // save to memory
      debug("New interval saved: %u minutes", settings.measInterval);
    } else {
      debug("Ignoring interval %u (out of range)", v);
    }
  }
}

static void sleepSeconds(uint32_t sec) {
  while (sec > 0) {
    uint32_t chunk = (sec > 3600UL) ? 3600UL : sec; // break sleep time into periods of max 1 hour
    LowPower.deepSleep(chunk * 1000UL); // RTC alarm wake-up in milliseconds
    sec -= chunk;
  }
}