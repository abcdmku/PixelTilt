// PixelTilt firmware for the Seengreat RGB Matrix HUB75 S3.
//
// The platform layer is deliberately thin: read the BNO08x gravity vector and
// the thumb wheel, call pt::engineTick(), blit pt::framebuffer to the panel.
// Everything game-related lives in core/ and games/, shared byte-for-byte
// with the browser emulator.
#include <Arduino.h>
#include <Wire.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <SparkFun_BNO08x_Arduino_Library.h>

#include "board_config.h"
#include "pca9557.h"
#include "pixeltilt/engine.h"
#include "pixeltilt/gfx.h"
#include "pixeltilt/input.h"
#include "pixeltilt/ptmath.h"

static MatrixPanel_I2S_DMA* panel = nullptr;
static PCA9557 keys;
static BNO08x imu;
static bool imuOk = false;

// Raw gravity from the IMU (m/s^2) and the boot-time "flat" reference that
// gets subtracted so the game zero matches however the device sits at boot.
static float gravX = 0, gravY = 0, gravZ = 0;
static float zeroX = 0, zeroY = 0;
static uint32_t lastMicros = 0;

static void setupPanel() {
  HUB75_I2S_CFG::i2s_pins pins = {
      HUB75_R1, HUB75_G1, HUB75_B1, HUB75_R2, HUB75_G2, HUB75_B2,
      HUB75_A,  HUB75_B,  HUB75_C,  HUB75_D,  HUB75_E,
      HUB75_LAT, HUB75_OE, HUB75_CLK,
  };
  HUB75_I2S_CFG cfg(PANEL_W, PANEL_H, PANEL_CHAIN, pins);
  cfg.clkphase = false;
  panel = new MatrixPanel_I2S_DMA(cfg);
  panel->begin();
  panel->setBrightness8(PANEL_BRIGHTNESS);
  panel->clearScreen();
}

static void enableImuReports() {
  imu.enableGravity(10);  // 100 Hz
}

static bool setupImu() {
  if (imu.begin(BNO08X_ADDR_PRIMARY, Wire)) return true;
  return imu.begin(BNO08X_ADDR_SECONDARY, Wire);
}

static void pollImu() {
  if (!imuOk) return;
  if (imu.wasReset()) enableImuReports();
  // Drain every queued report so we always use the freshest sample.
  while (imu.getSensorEvent()) {
    if (imu.getSensorEventID() == SENSOR_REPORTID_GRAVITY) {
      gravX = imu.getGravityX();
      gravY = imu.getGravityY();
      gravZ = imu.getGravityZ();
    }
  }
}

// Average gravity for a moment at boot so "flat" is wherever the device
// currently rests. Press RESET to re-zero.
static void calibrateZero() {
  if (!imuOk) return;
  float sx = 0, sy = 0;
  int n = 0;
  uint32_t start = millis();
  while (millis() - start < 600) {
    pollImu();
    sx += gravX;
    sy += gravY;
    n++;
    delay(5);
  }
  if (n > 0) {
    zeroX = sx / n;
    zeroY = sy / n;
  }
}

static void readTilt(float& tiltX, float& tiltY) {
  // Full deflection at TILT_FULL_ANGLE_DEG of physical tilt.
  const float fullG = 9.81f * pt::sinf_(TILT_FULL_ANGLE_DEG * pt::PI / 180.0f);
  float x = (gravX - zeroX) / fullG;
  float y = (gravY - zeroY) / fullG;
#if TILT_SWAP_XY
  float t = x; x = y; y = t;
#endif
  tiltX = pt::clampf(TILT_X_SIGN * x, -1.0f, 1.0f);
  tiltY = pt::clampf(TILT_Y_SIGN * y, -1.0f, 1.0f);
}

static uint8_t readButtons() {
  uint8_t raw = keys.readInputs();  // active low
  uint8_t b = 0;
  if (!(raw & (1 << KEY_UP_IO)))    b |= pt::BTN_UP;
  if (!(raw & (1 << KEY_CLICK_IO))) b |= pt::BTN_CLICK;
  if (!(raw & (1 << KEY_DOWN_IO)))  b |= pt::BTN_DOWN;
  return b;
}

static void blit() {
  const uint8_t* fb = pt::framebuffer;
  for (int y = 0; y < PANEL_H; y++) {
    for (int x = 0; x < PANEL_W; x++) {
      panel->drawPixelRGB888(x, y, fb[0], fb[1], fb[2]);
      fb += 3;
    }
  }
}

void setup() {
  Serial.begin(115200);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);

  if (!keys.begin(PCA9557_ADDR)) {
    Serial.println("WARN: PCA9557 key expander not responding");
  }

  imuOk = setupImu();
  if (imuOk) {
    enableImuReports();
    Serial.println("BNO08x online");
  } else {
    Serial.println("WARN: BNO08x not found (0x4A/0x4B) — tilt disabled");
  }

  setupPanel();

  pt::srand_(esp_random());
  pt::engineInit();
  calibrateZero();
  lastMicros = micros();
}

void loop() {
  uint32_t now = micros();
  float dt = (now - lastMicros) * 1e-6f;
  lastMicros = now;

  pollImu();
  float tiltX = 0, tiltY = 0;
  readTilt(tiltX, tiltY);

  pt::engineTick(tiltX, tiltY, readButtons(), dt);
  blit();

  // ~60 fps cap; engineTick+blit typically take well under a frame.
  uint32_t elapsed = micros() - now;
  if (elapsed < 16666) delayMicroseconds(16666 - elapsed);
}
