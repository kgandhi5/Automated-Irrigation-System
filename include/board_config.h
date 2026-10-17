// Pin map and build-time options. Both boxes run the same image; the box ID
// is a runtime setting (`set id <n>` on the console) stored in EEPROM.
#pragma once

#include <Arduino.h>

#define FW_VERSION "1.0.0"
#define SERIAL_BAUD 115200

// ---- Soil moisture sensors (analog outputs) --------------------------------
#define PIN_MOISTURE_1 A0
#define PIN_MOISTURE_2 A1
#define PIN_MOISTURE_3 A2
#define PIN_MOISTURE_4 A3

// ---- Pump relays (4-channel relay module, IN1..IN4) ------------------------
#define PIN_PUMP_1 6
#define PIN_PUMP_2 7
#define PIN_PUMP_3 8
#define PIN_PUMP_4 9
// Most blue 4-channel opto-isolated relay boards energise the coil when IN is
// pulled LOW. Set to 0 for boards that switch on a HIGH input.
#define RELAY_ACTIVE_LOW 1

// ---- Status LED (green, front panel) ---------------------------------------
#define PIN_STATUS_LED 10

// ---- 16x2 HD44780 LCD ------------------------------------------------------
// Default: parallel 4-bit wiring (Arduino LiquidCrystal "Hello World" pins).
// Build the `uno_i2c_lcd` environment instead for a PCF8574 I2C backpack.
#ifndef LCD_I2C
#define LCD_I2C 0
#endif
#define LCD_PIN_RS 12
#define LCD_PIN_EN 11
#define LCD_PIN_D4 5
#define LCD_PIN_D5 4
#define LCD_PIN_D6 3
#define LCD_PIN_D7 2
#define LCD_I2C_ADDR 0x27

// ---- Optional sensor power gating ------------------------------------------
// Powering the probes from a GPIO only while sampling stops resistive probes
// from electro-corroding. Needs the sensor VCC wires moved to this pin
// (capacitive probes draw ~5 mA each, well inside the 20 mA pin rating).
#ifndef SENSOR_POWER_GATING
#define SENSOR_POWER_GATING 0
#endif
#define PIN_SENSOR_POWER 13
#define SENSOR_SETTLE_MS 20
