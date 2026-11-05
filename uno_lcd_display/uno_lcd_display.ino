/*
 * Arduino Uno R3 - LCD display for the temperature controlled fan
 *
 * Wiring:
 *   LCD (I2C) VCC -> 5V, GND -> GND, SDA -> A4, SCL -> A5
 *   ESP32 GPIO17 (TX2) -> Uno D10   (data from the ESP32)
 *   ESP32 GND -> Uno GND            (must be shared)
 *
 * Library (Arduino Library Manager):
 *   - "LiquidCrystal I2C" by Frank de Brabander
 * Board: "Arduino Uno"
 *
 * The ESP32 sends one line per second, for example:
 *   T:32.5,H:60,F:75,A:0,E:0
 *   T = temperature (C), H = humidity (%), F = fan speed (%),
 *   A = 1 when the high-temperature alarm is on, E = 1 when the DHT11 is not responding.
 *
 * The LCD address (0x27 or 0x3F) is found automatically.
 */

#include <Wire.h>
#include <SoftwareSerial.h>
#include <LiquidCrystal_I2C.h>

// ------------------------- Pins -------------------------
const uint8_t RX_PIN = 10;   // from ESP32 TX2
const uint8_t TX_PIN = 11;   // not wired, only needed to create the serial port

SoftwareSerial espSerial(RX_PIN, TX_PIN);
LiquidCrystal_I2C *lcd = nullptr;

// ------------------- Settings -------------------
const uint32_t LINK_TIMEOUT_MS = 5000;   // no data for this long means link lost
const uint32_t REFRESH_MS      = 500;    // LCD refresh interval

// Degree symbol stored as custom character number 1
byte degreeChar[8] = {
  0b00110, 0b01001, 0b01001, 0b00110, 0b00000, 0b00000, 0b00000, 0b00000
};

// --------------------------- State ---------------------------
char     rxBuf[48];
uint8_t  rxLen = 0;

bool     haveData    = false;
uint32_t lastRxMs    = 0;
float    temperature = 0.0f;
int      humidity    = 0;
int      fanPercent  = 0;
bool     alarmOn     = false;
bool     sensorErr   = false;

uint32_t lastRenderMs = 0;
bool     blinkState   = false;

// -------------------------- LCD helpers --------------------------
byte findLcdAddress() {
  for (byte addr = 0x20; addr <= 0x3F; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) return addr;
  }
  return 0;
}

// Prints text on one row, pads with spaces, and turns '~' into the degree symbol
void lcdRow(uint8_t row, const char *text) {
  char buf[17];
  uint8_t i = 0;
  for (; i < 16 && text[i] != '\0'; i++) {
    buf[i] = (text[i] == '~') ? (char)1 : text[i];
  }
  for (; i < 16; i++) buf[i] = ' ';
  buf[16] = '\0';
  lcd->setCursor(0, row);
  lcd->print(buf);
}

// -------------------------- Parsing --------------------------
bool parseLine(char *line) {
  bool  gotTemp = false;
  float t = 0.0f;
  int   h = 0, f = 0;
  bool  a = false, e = false;

  for (char *tok = strtok(line, ","); tok != NULL; tok = strtok(NULL, ",")) {
    if (tok[0] == '\0' || tok[1] != ':') continue;
    const char *value = tok + 2;
    switch (tok[0]) {
      case 'T': t = atof(value); gotTemp = true; break;
      case 'H': h = atoi(value); break;
      case 'F': f = atoi(value); break;
      case 'A': a = (atoi(value) != 0); break;
      case 'E': e = (atoi(value) != 0); break;
      default: break;
    }
  }

  if (!gotTemp) return false;
  if (t < -40.0f || t > 125.0f) return false;   // reject garbage

  temperature = t;
  humidity    = constrain(h, 0, 100);
  fanPercent  = constrain(f, 0, 100);
  alarmOn     = a;
  sensorErr   = e;
  return true;
}

void readEsp32() {
  while (espSerial.available() > 0) {
    char c = (char)espSerial.read();
    if (c == '\n') {
      rxBuf[rxLen] = '\0';
      if (rxLen > 0 && parseLine(rxBuf)) {
        haveData = true;
        lastRxMs = millis();
      }
      rxLen = 0;
    } else if (c != '\r') {
      if (rxLen < sizeof(rxBuf) - 1) {
        rxBuf[rxLen++] = c;
      } else {
        rxLen = 0;   // line too long, drop it
      }
    }
  }
}

// -------------------------- Display --------------------------
void render() {
  char line[20];

  if (!haveData) {
    lcdRow(0, "Arduino ready");
    lcdRow(1, "Waiting ESP32...");
    return;
  }

  if (millis() - lastRxMs > LINK_TIMEOUT_MS) {
    lcdRow(0, "No ESP32 data");
    lcdRow(1, "Check TX2 to D10");
    return;
  }

  if (sensorErr) {
    lcdRow(0, "SENSOR ERROR");
    lcdRow(1, "Check DHT11");
    return;
  }

  int tenths = (int)(temperature * 10.0f + (temperature >= 0.0f ? 0.5f : -0.5f));
  snprintf(line, sizeof(line), "T:%s%d.%d~C H:%d%%",
           tenths < 0 ? "-" : "", abs(tenths) / 10, abs(tenths) % 10, humidity);
  lcdRow(0, line);

  if (alarmOn) {
    blinkState = !blinkState;
    snprintf(line, sizeof(line), "Fan:%d%% %s", fanPercent, blinkState ? "ALARM!" : "");
  } else if (fanPercent == 0) {
    snprintf(line, sizeof(line), "Fan:OFF  Normal");
  } else {
    snprintf(line, sizeof(line), "Fan:%d%% Cooling", fanPercent);
  }
  lcdRow(1, line);
}

// --------------------------- Arduino ---------------------------
void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);
  Wire.begin();

  byte addr = findLcdAddress();
  if (addr == 0) {
    addr = 0x27;   // fall back to the most common address
    Serial.println(F("LCD not found on I2C. Check SDA to A4 and SCL to A5."));
  } else {
    Serial.print(F("LCD found at 0x"));
    Serial.println(addr, HEX);
  }

  lcd = new LiquidCrystal_I2C(addr, 16, 2);
  lcd->init();
  lcd->backlight();
  lcd->createChar(1, degreeChar);
  lcd->clear();
  render();
}

void loop() {
  readEsp32();

  uint32_t now = millis();
  if (now - lastRenderMs >= REFRESH_MS) {
    lastRenderMs = now;
    render();
  }
}
