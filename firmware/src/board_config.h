#pragma once
// Seengreat RGB Matrix HUB75 S3 (ESP32-S3-WROOM-1-N16R8).
// Pin numbers verified against Seengreat's wiki and official demo code
// (https://seengreat.com/wiki/214/rgb-matrix-hub75-s3).

// --- HUB75 interface -------------------------------------------------------
#define HUB75_R1 5
#define HUB75_G1 4
#define HUB75_B1 6
#define HUB75_R2 15
#define HUB75_G2 7
#define HUB75_B2 17
#define HUB75_A  8
#define HUB75_B  18
#define HUB75_C  10
#define HUB75_D  9
#define HUB75_E  16
#define HUB75_CLK 12
#define HUB75_LAT 11
#define HUB75_OE  13

#define PANEL_W 64
#define PANEL_H 64
#define PANEL_CHAIN 1
#define PANEL_BRIGHTNESS 160  // 0-255

// Panel driver chip. 0 = generic shift-register (most panels, and what
// Seengreat's demo uses). If the firmware is clearly alive (serial heartbeat
// at 60 fps) but the panel stays completely black with power and ribbon
// verified, your panel likely uses an FM6126A driver that needs an init
// sequence — set this to 1 and reflash.
#define PANEL_FM6126A 0

// --- I2C bus (expansion header + onboard peripherals) ----------------------
// The 4-pin 1mm I2C header is where the GY-BNO08x plugs in. The bus is shared
// with the PCA9557 key expander (0x19), the PCF85063A RTC and the audio
// codecs — no address conflicts with a BNO08x at 0x4A/0x4B.
#define I2C_SDA_PIN 1
#define I2C_SCL_PIN 2
#define I2C_FREQ_HZ 400000

// --- Thumb wheel (via PCA9557 I2C expander, active-low) --------------------
// The board's only user input is a 3-way thumb wheel: roll up, roll down,
// press in. K1/K2/K3 sit on PCA9557 IOs (K1->IO1, K2->IO3, K3->IO2 — yes,
// K2 and K3 are swapped relative to what you'd guess; this matches both the
// wiki and the shipped demo code). If up/down feel reversed on your unit,
// swap KEY_UP_IO and KEY_DOWN_IO.
#define PCA9557_ADDR 0x19
#define KEY_UP_IO    1  // K1
#define KEY_CLICK_IO 3  // K2 (wheel press)
#define KEY_DOWN_IO  2  // K3

// --- BNO08x IMU ------------------------------------------------------------
// Transport. 1 = UART-RVC (default): strap PS0->3V3 and PS1->GND, then wire
// the sensor's SDA/MISO/TX pin to RX0 (GPIO44) on the bottom header. The
// BNO08x streams pitch/roll at 100 Hz with no driver library — and no I2C
// clock stretching, which the BNO08x is known to handle badly against
// ESP32-family I2C controllers. RX0 is free for this because Serial logging
// goes over native USB (ARDUINO_USB_CDC_ON_BOOT in platformio.ini).
// 0 = I2C on the 4-pin 1mm header (PS0 and PS1 both to GND).
#define IMU_USE_UART_RVC 1
#define IMU_RVC_RX_PIN   44  // RX0

// I2C mode only: GY-BNO08x boards usually strap to 0x4A; SparkFun/Adafruit
// default to 0x4B. The firmware probes both, primary first.
#define BNO08X_ADDR_PRIMARY   0x4A
#define BNO08X_ADDR_SECONDARY 0x4B

// Tilt mapping: angle (degrees) of physical tilt that maps to full deflection
// (+-1.0) in games. Flip the signs if your IMU is mounted differently.
#define TILT_FULL_ANGLE_DEG 30.0f
#define TILT_X_SIGN (+1.0f)
#define TILT_Y_SIGN (+1.0f)
// Set to 1 if your IMU is rotated 90 degrees relative to the panel.
#define TILT_SWAP_XY 0
