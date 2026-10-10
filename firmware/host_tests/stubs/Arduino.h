// Minimal Arduino core stand-in so the OLED firmware sources can be COMPILED AND RUN
// on a PC (no ESP32 toolchain needed). Test-only; never part of a firmware build.
//
// It models:
//   - String / Print / PROGMEM / pin calls (an interface-compatible subset)
//   - a virtual clock: millis()/micros() only move when a test advances them, when
//     delay() runs, or when the fake I2C bus "transfers" bytes. That makes the timing
//     assertions in test_display.cpp deterministic.
//   - GPIO inputs backed by a settable pin array (default HIGH = pull-up idle).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>

#define PROGMEM
#define PGM_P const char*
#define F(x) (x)
#define ICACHE_RAM_ATTR
#define IRAM_ATTR

#define HIGH 0x1
#define LOW 0x0
#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2

// ---- virtual time -------------------------------------------------------------
uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);              // advances the virtual clock
void delayMicroseconds(uint32_t us);  // advances the virtual clock
inline void yield() {}

void stubAdvanceClockUs(uint64_t us);
void stubResetClock();

// ---- GPIO ---------------------------------------------------------------------
void stubSetPin(int pin, int level);
void stubResetPins();

void pinMode(int, int);
void digitalWrite(int pin, int value);
int digitalRead(int pin);
int analogRead(int);

inline uint8_t pgm_read_byte(const void* addr) { return *static_cast<const uint8_t*>(addr); }
inline uint16_t pgm_read_word(const void* addr) { return *static_cast<const uint16_t*>(addr); }

// ---- one-call reset for every stub in this folder -----------------------------
void stubResetI2c();
void stubResetArduino();   // clock + pins + both filesystem volumes + Wire

#include "Print.h"
