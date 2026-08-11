#pragma once
#include <Arduino.h>
#include <Wire.h>

// Minimal ES8311 codec driver: playback path only, codec as I2S slave with
// MCLK supplied on the MCLK pin at 256*fs. The register sequence and the
// 256*fs divider row are distilled from Espressif's esp-bsp es8311 component
// (Apache-2.0); registers by number, playback-relevant ones only.
class ES8311 {
 public:
  // sampleRate is informational — the divider setup below is the 256*fs row,
  // valid for any supported rate as long as MCLK = 256 * fs.
  bool begin(uint8_t addr, uint32_t sampleRate) {
    (void)sampleRate;
    addr_ = addr;
    Wire.beginTransmission(addr_);
    if (Wire.endTransmission() != 0) return false;

    // Reset, then power on.
    if (!wr(0x00, 0x1F)) return false;
    delay(20);
    wr(0x00, 0x00);
    wr(0x00, 0x80);

    // Clock manager: all clocks on, internal MCLK from the MCLK pin.
    wr(0x01, 0x3F);
    // Dividers for MCLK = 256*fs: pre_div 1, pre_mult 1x, adc/dac div 1,
    // single speed, osr 16, bclk_div 4 (master-mode only, set to match).
    wr(0x02, 0x00);
    wr(0x03, 0x10);
    wr(0x04, 0x10);
    wr(0x05, 0x00);
    wr(0x06, 0x03);
    wr(0x07, 0x00);
    wr(0x08, 0xFF);

    // Serial port: slave (bit6 of reg00 already 0), I2S standard, 16-bit.
    wr(0x09, 0x0C);
    wr(0x0A, 0x0C);

    // Power up analog circuitry, DAC, and the output driver.
    wr(0x0D, 0x01);
    wr(0x0E, 0x02);
    wr(0x12, 0x00);
    wr(0x13, 0x10);
    wr(0x1C, 0x6A);
    wr(0x37, 0x08);  // bypass DAC equalizer

    mute(false);
    return true;
  }

  // DAC digital volume in dB, -95..+32 in 0.5 dB steps (reg 0xBF = 0 dB).
  // Keep this at 0 and do level control in the software mix — the register
  // scale is logarithmic, so "percent" mappings land wildly off: esp-bsp's
  // curve puts 85% at +12 dB (blasting) while a linear 70% is -29 dB
  // (inaudible).
  bool setVolumeDb(int8_t db) {
    if (db > 32) db = 32;
    if (db < -95) db = -95;
    return wr(0x32, (uint8_t)(0xBF + db * 2));
  }

  bool mute(bool on) {
    uint8_t v = 0;
    if (!rd(0x31, v)) return false;
    if (on) v |= 0x60;
    else v &= ~0x60;
    return wr(0x31, v);
  }

 private:
  bool wr(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr_);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
  }

  bool rd(uint8_t reg, uint8_t& val) {
    Wire.beginTransmission(addr_);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(addr_, (uint8_t)1) != 1) return false;
    val = (uint8_t)Wire.read();
    return true;
  }

  uint8_t addr_ = 0x18;
};
