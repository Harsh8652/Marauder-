// Host stub of Arduino's Wire (I2C) with a *time model*: every transferred byte is
// charged to the virtual clock at the frequency the driver requested. That lets a test
// measure how long a real 128x64 frame costs on the bus instead of trusting a comment.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

struct StubI2cTransaction {
  uint8_t address = 0;
  std::vector<uint8_t> bytes;   // payload (without the address byte)
};

// Bus log, cleared by stubResetI2c(). Tests decode it to check commands and pixel data.
extern std::vector<StubI2cTransaction> stub_i2c_log;
void stubResetI2c();

// Simulate a dead / absent panel: every start gets a NACK and the payload is dropped.
void stubWireSetNackAll(bool nack);
// True if any transaction was actually clocked onto the bus since the last reset.
bool stubWireSawActivity();
// Bus time charged to the virtual clock so far, in microseconds.
uint32_t stubWireBusMicros();
void stubWireResetBusTime();

class TwoWire {
 public:
  bool begin(int sda = -1, int scl = -1, uint32_t frequency = 0);
  void beginTransmission(uint8_t address);
  size_t write(uint8_t b);
  size_t write(const uint8_t* data, size_t len);
  uint8_t endTransmission(bool sendStop = true);
  size_t requestFrom(uint8_t, size_t) { return 0; }
  int available() { return 0; }
  int read() { return -1; }

  // Test inspection
  int sdaPin() const { return sda_; }
  int sclPin() const { return scl_; }
  uint32_t frequency() const { return freq_; }

 private:
  int sda_ = -1;
  int scl_ = -1;
  uint32_t freq_ = 0;
  bool tx_active_ = false;
  uint8_t tx_addr_ = 0;
  std::vector<uint8_t> tx_buf_;
};

extern TwoWire Wire;
