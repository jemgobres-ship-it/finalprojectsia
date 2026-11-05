/*
 * ESP32 - Automatic temperature controlled fan
 *
 * Wiring (matches the breadboard diagram):
 *   DHT11 DATA  -> GPIO4   (VCC -> 3V3, GND -> GND)
 *   Buzzer (+)  -> GPIO19 through 220 ohm (other leg -> GND)
 *   Fan MOSFET  -> GPIO18 -> 220 ohm -> IRLZ44N gate (10k gate-to-GND)
 *   To Uno      -> GPIO17 (TX2) -> Uno D10, plus a shared GND
 *
 * Libraries (Arduino Library Manager):
 *   - "DHT sensor library" by Adafruit
 *   - "Adafruit Unified Sensor" by Adafruit
 * Board: "ESP32 Dev Module"  (works with ESP32 Arduino core 2.x and 3.x)
 *
 * Behaviour:
 *   below 28 C            fan off
 *   28 C to 35 C          fan speed rises from 40% to 100%
 *   35 C and above        fan 100%
 *   40 C and above        buzzer alarm (fast beeps) until it cools to 38 C
 *   sensor not responding fan 100% and slow beeps (fail-safe), "E:1" sent to Uno
 */

#include <Arduino.h>
#include <DHT.h>

// ------------------------- Pins -------------------------
const uint8_t DHT_PIN     = 4;
const uint8_t BUZZER_PIN  = 19;
const uint8_t FAN_PIN     = 18;
const int8_t  UART_RX_PIN = 16;   // not wired, only needed to open Serial2
const int8_t  UART_TX_PIN = 17;   // wired to Uno D10 (use 25 if your board is an ESP32-WROVER)

#define DHT_TYPE DHT11

// ------------------- Settings you can change -------------------
const float    FAN_ON_TEMP       = 28.0f;  // fan starts at this temperature (C)
const float    FAN_FULL_TEMP     = 35.0f;  // fan at 100% from here (C)
const float    FAN_HYSTERESIS    = 1.0f;   // fan stops at FAN_ON_TEMP minus this
const uint8_t  FAN_MIN_PERCENT   = 40;     // lowest speed once the fan is running
const uint16_t FAN_KICK_MS       = 400;    // full power burst so the fan starts spinning
const float    ALARM_TEMP        = 40.0f;  // buzzer alarm starts here (C)
const float    ALARM_HYSTERESIS  = 2.0f;   // alarm stops at ALARM_TEMP minus this
const uint8_t  MAX_SENSOR_FAILS  = 5;      // failed reads in a row before fail-safe mode

const uint32_t READ_INTERVAL_MS  = 2000;   // DHT11 needs at least 1 second between reads
const uint32_t SEND_INTERVAL_MS  = 1000;   // how often the Uno gets an update
const uint16_t ALARM_BEEP_MS     = 250;    // fast beeps for high temperature
const uint16_t ERROR_BEEP_MS     = 800;    // slow beeps for sensor error

// Set to 1 to test fan, buzzer and LCD without the sensor (temperature sweeps 20 to 45 C)
#define TEST_MODE 0

// ----------------------- PWM settings -----------------------
const uint32_t PWM_FREQ = 25000;           // 25 kHz, above hearing range
const uint8_t  PWM_BITS = 8;               // duty 0 to 255

// --------------------------- State ---------------------------
DHT dht(DHT_PIN, DHT_TYPE);

float    lastTemp      = 0.0f;
float    lastHum       = 0.0f;
bool     haveReading   = false;
uint8_t  failCount     = 0;
bool     sensorError   = false;
bool     alarmActive   = false;
bool     fanRunning    = false;
uint8_t  fanPercent    = 0;
uint32_t kickUntilMs   = 0;

uint32_t lastReadMs    = 0;
uint32_t lastSendMs    = 0;
uint32_t lastBeepMs    = 0;
bool     buzzerState   = false;

// ------------------- PWM (works on core 2.x and 3.x) -------------------
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
void fanPwmBegin()              { ledcAttach(FAN_PIN, PWM_FREQ, PWM_BITS); }
void fanPwmWrite(uint8_t duty)  { ledcWrite(FAN_PIN, duty); }
#else
const uint8_t FAN_PWM_CHANNEL = 0;
void fanPwmBegin() {
  ledcSetup(FAN_PWM_CHANNEL, PWM_FREQ, PWM_BITS);
  ledcAttachPin(FAN_PIN, FAN_PWM_CHANNEL);
}
void fanPwmWrite(uint8_t duty)  { ledcWrite(FAN_PWM_CHANNEL, duty); }
#endif

// --------------------------- Fan ---------------------------
uint8_t computeFanPercent(float t) {
  if (!fanRunning && t >= FAN_ON_TEMP) {
    fanRunning = true;
  } else if (fanRunning && t <= (FAN_ON_TEMP - FAN_HYSTERESIS)) {
    fanRunning = false;
  }
  if (!fanRunning) return 0;
  if (t >= FAN_FULL_TEMP) return 100;

  float ratio = (t - FAN_ON_TEMP) / (FAN_FULL_TEMP - FAN_ON_TEMP);
  if (ratio < 0.0f) ratio = 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;
  return (uint8_t)(FAN_MIN_PERCENT + ratio * (100 - FAN_MIN_PERCENT) + 0.5f);
}

void setFanPercent(uint8_t percent) {
  if (percent > 100) percent = 100;
  if (percent > 0 && fanPercent == 0) {
    kickUntilMs = millis() + FAN_KICK_MS;   // start with a full power burst
  }
  fanPercent = percent;
}

void applyFan() {
  uint8_t duty;
  if (fanPercent == 0) {
    duty = 0;
  } else if ((int32_t)(kickUntilMs - millis()) > 0) {
    duty = 255;
  } else {
    duty = (uint8_t)(((uint16_t)fanPercent * 255U) / 100U);
  }
  fanPwmWrite(duty);
}

// ------------------------- Alarm / buzzer -------------------------
void updateAlarm(float t) {
  if (!alarmActive && t >= ALARM_TEMP) {
    alarmActive = true;
  } else if (alarmActive && t <= (ALARM_TEMP - ALARM_HYSTERESIS)) {
    alarmActive = false;
  }
}

void updateBuzzer() {
  uint16_t interval = 0;
  if (sensorError)       interval = ERROR_BEEP_MS;
  else if (alarmActive)  interval = ALARM_BEEP_MS;

  if (interval == 0) {
    buzzerState = false;
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }
  uint32_t now = millis();
  if (now - lastBeepMs >= interval) {
    lastBeepMs = now;
    buzzerState = !buzzerState;
    digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
  }
}

// --------------------------- Sensor ---------------------------
void readSensor() {
#if TEST_MODE
  uint32_t s = (millis() / 1000UL) % 50UL;
  lastTemp = 20.0f + (float)(s < 25 ? s : 50 - s);
  lastHum = 50.0f;
  haveReading = true;
  failCount = 0;
  sensorError = false;
  return;
#endif
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    if (failCount < 255) failCount++;
    if (failCount >= MAX_SENSOR_FAILS) sensorError = true;
    Serial.println("DHT11 read failed");
  } else {
    failCount = 0;
    sensorError = false;
    lastTemp = t;
    lastHum = h;
    haveReading = true;
  }
}

void updateControl() {
  if (sensorError) {
    setFanPercent(100);          // fail-safe: cool as much as possible
    alarmActive = false;         // sensor error beeps are handled separately
  } else if (haveReading) {
    setFanPercent(computeFanPercent(lastTemp));
    updateAlarm(lastTemp);
  }
}

// --------------------------- Link to Uno ---------------------------
void sendToUno() {
  if (!haveReading && !sensorError) return;   // nothing useful to send yet

  char line[64];
  snprintf(line, sizeof(line), "T:%.1f,H:%d,F:%u,A:%d,E:%d",
           lastTemp,
           (int)(lastHum + 0.5f),
           (unsigned)fanPercent,
           alarmActive ? 1 : 0,
           sensorError ? 1 : 0);

  Serial2.println(line);
  Serial.println(line);
}

// --------------------------- Arduino ---------------------------
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  fanPwmBegin();
  fanPwmWrite(0);

  dht.begin();

  // short startup beep
  digitalWrite(BUZZER_PIN, HIGH);
  delay(100);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println("ESP32 fan controller started");
}

void loop() {
  uint32_t now = millis();

  if (now - lastReadMs >= READ_INTERVAL_MS) {
    lastReadMs = now;
    readSensor();
    updateControl();
  }

  if (now - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = now;
    sendToUno();
  }

  applyFan();
  updateBuzzer();
}
