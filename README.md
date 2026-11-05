# Smart Fan Controller (ESP32)

A temperature-controlled fan with a WiFi control panel, buzzer alarm and live data for an Arduino Uno LCD display.

## Overview

An ESP32 reads room temperature and humidity from a DHT11 sensor and drives a fan through a MOSFET using PWM. The fan speed changes in three levels as the room gets hotter, a buzzer sounds when the temperature gets too high, and the fan switches itself off once the room has cooled down. The ESP32 also hosts its own WiFi network and a phone-friendly web page where you can watch the readings, test the levels, and control the fan by hand. The same readings are sent over a wire to an Arduino Uno for display.

## Features

- Three automatic fan levels with automatic cooldown
- High-temperature buzzer alarm with hysteresis
- Fail-safe: if the sensor stops responding, the fan runs at 100% and the buzzer beeps slowly
- Built-in WiFi access point, with optional connection to your router
- Web control panel: automatic modes, instant temperature tester, manual fan speeds, buzzer mute
- Serial data link to the Arduino Uno
- Quiet 25 kHz PWM fan drive with a short full-power kick so the fan always starts

## Hardware

- ESP32 development board (ESP32 Dev Module)
- DHT11 temperature and humidity sensor
- Fan, switched by an IRLZ44N logic-level MOSFET
- Buzzer (driven on and off by the ESP32)
- Two 220 ohm resistors and one 10 k ohm resistor
- Arduino Uno with LCD (receives the data)
- A power supply that suits your fan, with all grounds shared

## Wiring

| Part | ESP32 pin | Notes |
|---|---|---|
| DHT11 DATA | GPIO4 | VCC to 3V3, GND to GND |
| Buzzer (+) | GPIO19 | Through 220 ohm, other leg to GND |
| Fan MOSFET gate | GPIO18 | Through 220 ohm, with 10 k ohm from gate to GND |
| Link to Uno | GPIO17 (TX2) | To Uno D10, plus a shared GND |

GPIO16 is opened as RX2 in the code but is not wired. If your board is an ESP32-WROVER, change `UART_TX_PIN` to 25.

## Software setup

1. Install the Arduino IDE and add ESP32 board support (core 2.x or 3.x both work).
2. Install these libraries from the Library Manager: **DHT sensor library** (Adafruit) and **Adafruit Unified Sensor**.
3. Select the board **ESP32 Dev Module** and the correct COM port.
4. Open `esp32_fan_controller.ino` and upload it.
5. Open the Serial Monitor at **115200 baud** to see the status messages.

The WebServer and WiFi libraries are included with the ESP32 board package.

## Temperature behavior

| Temperature | Behavior |
|---|---|
| Below 28.0 °C | Fan off |
| Level 1: 28.0 °C | Fan at 40% |
| Level 2: 30.0 °C | Fan at 70% |
| Level 3: 33.0 °C | Fan at 100% |
| 37.0 °C and above | Buzzer alarm (fast beeps), stops when the temperature falls to 35.0 °C |
| Automatic cooldown | Fan switches off when the temperature falls to 27.5 °C |
| Sensor not responding | Fan at 100%, slow beeps, error flag sent to the Uno |

Once the fan has started, it keeps running until the temperature reaches 27.5 °C. This gap between 28.0 and 27.5 prevents the fan from switching on and off repeatedly around the threshold. The sensor is declared faulty after 5 failed reads in a row.

## Web control panel

1. On your phone or laptop, connect to the WiFi network **ESP32-FanController** (password **12345678**).
2. Open **http://192.168.4.1** in a browser.

To also use your home network, enter your router's name and password in `WIFI_SSID` and `WIFI_PASS` near the top of the sketch (2.4 GHz only). The address on the router is printed in the Serial Monitor.

The top of the page shows the temperature, humidity, connection badge, current state, fan speed and the thresholds. Below that:

1. **Automatic modes**
   - *Auto Sensor Mode*: normal operation using the live DHT11 temperature.
   - *Auto-Shift Loop*: cycles Level 1, 2 and 3 every 4 seconds using simulated temperatures, handy for demonstrations.
2. **Instant temp shift tester**: *Test Cool* (26 °C), *Test Level 1*, *Test Level 2*, *Test Level 3* and *Test Overheat* (37 °C alarm) simulate a temperature immediately. *Reset to Live Room Temp* returns to the real sensor. The LCD on the Uno shows the simulated value during a test.
3. **Manual fan control**: Off, Speed 1 (40%), Speed 2 (70%), Speed 3 (100%).
4. **Buzzer**: tap to mute or unmute.

The active mode or speed is outlined in white, and a status bar explains special situations such as an alarm, test mode or manual control.

In manual mode the high-temperature alarm still watches the real temperature, but the automatic sensor-failure fan override does not apply. Test mode, manual speed and mute are kept in memory only, so they reset when the ESP32 restarts.

## HTTP endpoints

The page uses these endpoints, which you can also call directly. Each one returns the current state as JSON.

| Endpoint | Purpose |
|---|---|
| `/` | Control panel page |
| `/data` | Current state as JSON |
| `/api/mode?m=auto`, `shift`, `reset` | Choose automatic mode, start the shift loop, return to live temperature |
| `/api/test?t=cool`, `l1`, `l2`, `l3`, `alarm` | Simulate a temperature |
| `/api/fan?s=0` to `3` | Manual fan speed |
| `/api/buzzer` | Toggle buzzer mute |

## Link to the Arduino Uno

The ESP32 sends one text line per second at **9600 baud** from GPIO17 to the Uno, in this form:

`T:30.0,H:52,F:70,A:0,E:0`

| Field | Meaning |
|---|---|
| T | Temperature in °C (simulated value during tests) |
| H | Humidity in percent |
| F | Fan speed in percent |
| A | 1 when the high-temperature alarm is active |
| E | 1 when the sensor is not responding |

The Uno sketch should read these lines (for example with SoftwareSerial on D10) and show them on the LCD. The Uno sketch was not part of this project's files, so check that it parses this format.

## Customizing

All settings are constants near the top of `esp32_fan_controller.ino`:

- `LEVEL1_TEMP`, `LEVEL2_TEMP`, `LEVEL3_TEMP`: level start temperatures
- `LEVEL1_PERCENT`, `LEVEL2_PERCENT`, `LEVEL3_PERCENT`: fan speed at each level
- `COOLDOWN_TEMP`: temperature at which the fan switches off
- `ALARM_TEMP` and `ALARM_HYSTERESIS`: alarm start and release
- `FAN_KICK_MS`: length of the full-power start burst
- `WIFI_SSID`, `WIFI_PASS`, `AP_SSID`, `AP_PASS`: WiFi settings
- `TEST_MODE`: set to 1 to sweep the temperature from 20 to 45 °C without a sensor

The thresholds shown on the web page come from these same constants, so the page stays correct after you change them.

## Troubleshooting

| Problem | What to check |
|---|---|
| WiFi network not visible | Re-upload the sketch with the board set to ESP32 Dev Module, press reset, and look for the access point message in the Serial Monitor |
| Cannot join the router | The ESP32 supports 2.4 GHz only; check the name and password |
| ESP32 keeps resetting | Use a stronger USB port or supply; WiFi draws current in bursts and the fan may share the supply |
| Sensor error shown | Check the DHT11 wiring and 3V3 power; the DHT11 needs at least 1 second between reads |
| Fan does not start | Check the MOSFET orientation, the gate resistor and that the fan supply ground is shared with the ESP32 |
| Nothing on the Uno LCD | Check the shared GND, the GPIO17 to D10 wire and that both sides use 9600 baud |
| Page says NO CONNECTION | Your device lost the ESP32 WiFi; reconnect and refresh |

## Safety notes

- Share a common ground between the ESP32, the fan supply and the Uno.
- Use a flyback diode across the fan if it is a motor-type fan.
- Do not power a fan directly from an ESP32 pin; always use the MOSFET.
- This is a hobby project and not a certified safety device. Do not rely on it alone to protect equipment or people from overheating.
