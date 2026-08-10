#pragma once
#include <Arduino.h>
#include <Wire.h>

// Minimal PCA9557 I2C I/O expander driver — just enough to read the Hub S3's
// three thumb-wheel keys (active low).
class PCA9557 {
 public:
  bool begin(uint8_t addr, TwoWire& wire = Wire) {
    _addr = addr;
    _wire = &wire;
    // Reg 3 = pin config (1 = input), reg 2 = polarity inversion (0 = none).
    return writeReg(3, 0xFF) && writeReg(2, 0x00);
  }

  // Returns the raw input register, or 0xFF (all released) on bus error.
  uint8_t readInputs() {
    _wire->beginTransmission(_addr);
    _wire->write((uint8_t)0);  // input port register
    if (_wire->endTransmission(false) != 0) return 0xFF;
    if (_wire->requestFrom(_addr, (uint8_t)1) != 1) return 0xFF;
    return _wire->read();
  }

  bool pressed(uint8_t io) { return !(readInputs() & (1 << io)); }

 private:
  bool writeReg(uint8_t reg, uint8_t val) {
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    _wire->write(val);
    return _wire->endTransmission() == 0;
  }

  uint8_t _addr = 0x19;
  TwoWire* _wire = nullptr;
};
