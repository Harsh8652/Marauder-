// Implementations for the host stubs (Arduino, Wire with a bus-time model, SPIFFS, SD).
#include "Arduino.h"
#include "Wire.h"
#include "FS.h"
#include "SPIFFS.h"
#include "SD.h"

#include <vector>

namespace {

// One counter for everything so display throttling and I2C cost share the same timeline.
uint64_t g_now_us = 0;
int g_pins[64];
bool g_i2c_saw_activity = false;
bool g_i2c_nack_all = false;
uint32_t g_i2c_bus_us = 0;

}  // namespace

// ---- time ---------------------------------------------------------------------
uint32_t millis() { return static_cast<uint32_t>(g_now_us / 1000u); }
uint32_t micros() { return static_cast<uint32_t>(g_now_us); }
void delay(uint32_t ms) { g_now_us += static_cast<uint64_t>(ms) * 1000u; }
void delayMicroseconds(uint32_t us) { g_now_us += us; }
void stubAdvanceClockUs(uint64_t us) { g_now_us += us; }
void stubResetClock() { g_now_us = 0; }

// ---- GPIO ---------------------------------------------------------------------
void stubSetPin(int pin, int level) { g_pins[pin & 63] = level ? 1 : 0; }
void stubResetPins() {
  for (int& p : g_pins) p = 1;   // INPUT_PULLUP idles high
}
void pinMode(int, int) {}
void digitalWrite(int pin, int value) { g_pins[pin & 63] = value ? 1 : 0; }
int digitalRead(int pin) { return g_pins[pin & 63]; }
int analogRead(int) { return 0; }

// ---- filesystems --------------------------------------------------------------
namespace stubfs {
Volume& spiffs() {
  static Volume v;
  return v;
}
Volume& sd() {
  static Volume v;
  return v;
}
void resetAll() {
  spiffs() = Volume{};
  sd() = Volume{};
  spiffs().capacity = 960u * 1024u;   // the SPIFFS partition in partitions.csv
}
}  // namespace stubfs

SpiiffsFs SPIFFS;
SdFs SD;

// ---- I2C ----------------------------------------------------------------------
std::vector<StubI2cTransaction> stub_i2c_log;
TwoWire Wire;

void stubResetI2c() {
  stub_i2c_log.clear();
  g_i2c_saw_activity = false;
  g_i2c_nack_all = false;
  g_i2c_bus_us = 0;
}
void stubWireSetNackAll(bool nack) { g_i2c_nack_all = nack; }
bool stubWireSawActivity() { return g_i2c_saw_activity; }
uint32_t stubWireBusMicros() { return g_i2c_bus_us; }
void stubWireResetBusTime() { g_i2c_bus_us = 0; }

namespace {
// Every byte on I2C costs 8 data bits + 1 ACK bit; each start/stop costs about a bit.
// Charging that to the shared virtual clock is what makes the frame-rate measurement
// in test_display.cpp meaningful.
void stubI2cCharge(size_t bytes) {
  uint32_t kbits = 0;
  if (Wire.frequency() == 0) return;   // begin() never ran: no bus model
  kbits = static_cast<uint32_t>(bytes) * 9u + 2u;
  const uint32_t us = (kbits * 1000000u + Wire.frequency() / 2) / Wire.frequency();
  g_i2c_bus_us += us;
  g_now_us += us;
}
}  // namespace

bool TwoWire::begin(int sda, int scl, uint32_t frequency) {
  sda_ = sda;
  scl_ = scl;
  freq_ = frequency;
  return true;
}

void TwoWire::beginTransmission(uint8_t address) {
  tx_addr_ = address;
  tx_buf_.clear();
  tx_active_ = true;
}

size_t TwoWire::write(uint8_t b) {
  tx_buf_.push_back(b);
  return 1;
}

size_t TwoWire::write(const uint8_t* data, size_t len) {
  tx_buf_.insert(tx_buf_.end(), data, data + len);
  return len;
}

uint8_t TwoWire::endTransmission(bool) {
  tx_active_ = false;
  if (g_i2c_nack_all) {
    g_i2c_saw_activity = true;
    stubI2cCharge(1);   // the address byte is still clocked out before the NACK
    return 2;           // ESP32: 2 = NACK on address
  }
  stub_i2c_log.push_back(StubI2cTransaction{tx_addr_, tx_buf_});
  g_i2c_saw_activity = true;
  stubI2cCharge(tx_buf_.size() + 1);
  tx_buf_.clear();
  return 0;
}

// ---- global reset ----------------------------------------------------------------
void stubResetArduino() {
  stubResetClock();
  stubResetPins();
  stubResetI2c();
  stubfs::resetAll();
}
