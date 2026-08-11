// PixelTilt firmware for the Seengreat RGB Matrix HUB75 S3.
//
// The platform layer is deliberately thin: read the BNO08x tilt (UART-RVC
// stream by default, I2C gravity vector as the alternative — board_config.h)
// and the thumb wheel, call pt::engineTick(), blit pt::framebuffer to the panel.
// Everything game-related lives in core/ and games/, shared byte-for-byte
// with the browser emulator.
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "board_config.h"
#if !IMU_USE_UART_RVC
#include <SparkFun_BNO08x_Arduino_Library.h>
#endif
#include "pca9557.h"
#include "pixeltilt/engine.h"
#include "pixeltilt/gfx.h"
#include "pixeltilt/input.h"
#include "pixeltilt/ptmath.h"
#include "pixeltilt/storage.h"

static MatrixPanel_I2S_DMA* panel = nullptr;
static PCA9557 keys;
#if !IMU_USE_UART_RVC
static BNO08x imu;
#endif
static Preferences prefs;
static bool imuOk = false;
static bool keysOk = false;
static uint32_t frameCount = 0;
static uint8_t lastBrightness = 0;
static uint32_t dirtySince = 0;  // millis of the first unsaved change, 0 = clean

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
  // Library defaults (SHIFTREG driver, default clock phase) — same config as
  // Seengreat's own demo sketch.
  HUB75_I2S_CFG cfg(PANEL_W, PANEL_H, PANEL_CHAIN, pins);
#if PANEL_FM6126A
  cfg.driver = HUB75_I2S_CFG::FM6126A;
#endif
  panel = new MatrixPanel_I2S_DMA(cfg);
  panel->begin();
  panel->setBrightness8(PANEL_BRIGHTNESS);
  panel->clearScreen();

  // Boot test pattern: a quick rainbow sweep proves panel wiring/power
  // before any game logic runs.
  for (int x = 0; x < PANEL_W; x++) {
    pt::Color c = pt::hsv(x * (360.0f / PANEL_W), 1.0f, 1.0f);
    for (int y = 0; y < PANEL_H; y++) panel->drawPixelRGB888(x, y, c.r, c.g, c.b);
    delay(8);
  }
  delay(300);
  panel->clearScreen();
}

#if IMU_USE_UART_RVC

// UART-RVC: the sensor autonomously streams 19-byte frames at 115200 baud,
// 100 Hz — 0xAA 0xAA header, then index, yaw, pitch, roll, accel x/y/z
// (int16 LE), 3 reserved bytes, checksum (sum of the 16 bytes after the
// header). Pitch/roll are fused absolute angles in 0.01 degree units.
static HardwareSerial rvcSerial(1);

static bool rvcParse(const uint8_t* p) {  // p = the 17 bytes after 0xAA 0xAA
  uint8_t sum = 0;
  for (int i = 0; i < 16; i++) sum += p[i];
  if (sum != p[16]) return false;
  float pitch = (int16_t)(p[3] | (p[4] << 8)) * 0.01f * pt::PI / 180.0f;
  float roll  = (int16_t)(p[5] | (p[6] << 8)) * 0.01f * pt::PI / 180.0f;
  // Present the angles as a synthetic gravity vector so the boot zeroing and
  // tilt mapping below are identical for both IMU transports.
  gravX = 9.81f * pt::sinf_(pitch);
  gravY = -9.81f * pt::sinf_(roll);
  gravZ = 9.81f;
  return true;
}

static void pollImu() {
  static uint8_t payload[17];
  static int state = 0;  // 0-1 = hunting the 0xAA 0xAA header
  while (rvcSerial.available()) {
    uint8_t b = (uint8_t)rvcSerial.read();
    if (state < 2) {
      state = (b == 0xAA) ? state + 1 : 0;
    } else {
      payload[state++ - 2] = b;
      if (state == 19) {
        state = 0;
        if (rvcParse(payload)) imuOk = true;
      }
    }
  }
}

static bool setupImu() {
  rvcSerial.begin(115200, SERIAL_8N1, IMU_RVC_RX_PIN, /*tx*/ -1);
  // Frames arrive every 10 ms, so a short listen proves presence.
  uint32_t start = millis();
  while (!imuOk && millis() - start < 400) {
    pollImu();
    delay(2);
  }
  return imuOk;
}

#else  // I2C

static void enableImuReports() {
  imu.enableGravity(10);  // 100 Hz
}

// The SparkFun BNO08x begin() can block indefinitely when nothing answers at
// the address, so ping the bus first and only hand begin() a live device.
static bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static bool setupImu() {
  if (i2cPresent(BNO08X_ADDR_PRIMARY)) return imu.begin(BNO08X_ADDR_PRIMARY, Wire);
  if (i2cPresent(BNO08X_ADDR_SECONDARY)) return imu.begin(BNO08X_ADDR_SECONDARY, Wire);
  return false;
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

#endif  // IMU_USE_UART_RVC

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

// Brightness setting (percent) scales the board's PWM ceiling.
static void applyBrightness() {
  uint8_t pct = pt::settings().brightness;
  if (pct == lastBrightness) return;
  lastBrightness = pct;
  panel->setBrightness8((uint8_t)((PANEL_BRIGHTNESS * pct) / 100));
}

// Settings + high scores live in NVS as one opaque blob (see storage.h).
static void loadSave() {
  prefs.begin("pixeltilt", false);
  if (prefs.getBytesLength("save") == (size_t)pt::saveBlobSize() &&
      prefs.getBytes("save", pt::saveBlob(), pt::saveBlobSize()) &&
      pt::saveBlobLoad()) {
    Serial.println("[pixeltilt] save restored from NVS");
  }
}

// Debounced flash write: settings screens raise the dirty flag on every
// click, so wait for a quiet second before committing.
static void persistSave() {
  if (pt::saveDirty() && dirtySince == 0) dirtySince = millis();
  if (dirtySince != 0 && millis() - dirtySince >= 1000) {
    prefs.putBytes("save", pt::saveBlob(), pt::saveBlobSize());
    pt::clearSaveDirty();
    dirtySince = 0;
  }
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
  Serial.println("[pixeltilt] boot");

  // Panel first: even if every I2C peripheral is missing, the display comes
  // up and shows the boot sweep + menu.
  setupPanel();

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);

  keysOk = keys.begin(PCA9557_ADDR);
  if (!keysOk) {
    Serial.println("WARN: PCA9557 key expander not responding");
  }

  imuOk = setupImu();
  if (imuOk) {
#if !IMU_USE_UART_RVC
    enableImuReports();
#endif
    Serial.println("BNO08x online");
  } else {
#if IMU_USE_UART_RVC
    Serial.println("WARN: no UART-RVC frames on RX0 - tilt disabled");
#else
    Serial.println("WARN: BNO08x not found (0x4A/0x4B) - tilt disabled");
#endif
  }

  pt::srand_(esp_random());
  pt::engineInit();
  loadSave();
  applyBrightness();
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
  applyBrightness();
  persistSave();
  blit();
  frameCount++;

  // Status heartbeat: lets `npm run monitor` (or any late-attached terminal)
  // see what the board thinks is going on.
  static uint32_t lastBeat = 0;
  if (millis() - lastBeat >= 2000) {
    lastBeat = millis();
    Serial.printf("[pixeltilt] up=%lus frames=%lu keys=%s raw=0x%02X imu=%s tilt=%.2f,%.2f game=%d\n",
                  (unsigned long)(millis() / 1000), (unsigned long)frameCount,
                  keysOk ? "ok" : "MISSING", keys.readInputs(),
                  imuOk ? "ok" : "absent", tiltX, tiltY, pt::currentGame());
  }

  // ~60 fps cap; engineTick+blit typically take well under a frame.
  uint32_t elapsed = micros() - now;
  if (elapsed < 16666) delayMicroseconds(16666 - elapsed);
}
