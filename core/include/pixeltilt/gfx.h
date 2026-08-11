#pragma once
#include <stdint.h>

namespace pt {

constexpr int SCREEN_W = 64;
constexpr int SCREEN_H = 64;

struct Color {
  uint8_t r, g, b;
};

constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b) { return {r, g, b}; }

// Hue [0,360), saturation and value [0,1]. Handy for LED-friendly palettes.
Color hsv(float h, float s, float v);

constexpr Color BLACK   = {0, 0, 0};
constexpr Color WHITE   = {255, 255, 255};
constexpr Color RED     = {255, 40, 40};
constexpr Color GREEN   = {40, 255, 60};
constexpr Color BLUE    = {50, 90, 255};
constexpr Color YELLOW  = {255, 210, 40};
constexpr Color CYAN    = {40, 220, 255};
constexpr Color MAGENTA = {255, 60, 200};
constexpr Color ORANGE  = {255, 130, 30};
constexpr Color GRAY    = {110, 110, 110};
constexpr Color DARKGRAY = {40, 40, 40};

// The shared RGB888 framebuffer. Row-major, 3 bytes per pixel. The firmware
// blits it to the HUB75 panel; the emulator reads it straight out of WASM
// memory every animation frame.
extern uint8_t framebuffer[SCREEN_W * SCREEN_H * 3];

// Screen rotation in quarter turns (0..3), applied inside pixel()/getPixel()
// so games and hosts stay rotation-oblivious. Set from the settings screen.
void setRotation(int quarterTurns);
int  rotation();

void clear(Color c = BLACK);
void pixel(int x, int y, Color c);
Color getPixel(int x, int y);

void hline(int x, int y, int w, Color c);
void vline(int x, int y, int h, Color c);
void line(int x0, int y0, int x1, int y1, Color c);
void rect(int x, int y, int w, int h, Color c);
void fillRect(int x, int y, int w, int h, Color c);
void circle(int cx, int cy, int r, Color c);
void fillCircle(int cx, int cy, int r, Color c);

// 3x5 pixel font (uppercase letters, digits, basic punctuation). Each glyph
// advances 4*scale pixels. Lowercase input is drawn as uppercase.
void text(int x, int y, const char* s, Color c, int scale = 1);
int textWidth(const char* s, int scale = 1);
// Centered horizontally on the 64px screen.
void textCentered(int y, const char* s, Color c, int scale = 1);

}  // namespace pt
