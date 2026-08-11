#include "pixeltilt/gfx.h"
#include "pixeltilt/ptmath.h"

namespace pt {

uint8_t framebuffer[SCREEN_W * SCREEN_H * 3];

namespace {

int rot = 0;

// Logical -> physical framebuffer index, rotated by quarter turns. The screen
// is square, so every rotation is lossless.
inline int fbIndex(int x, int y) {
  int px, py;
  switch (rot) {
    default: px = x;                py = y;                break;
    case 1:  px = SCREEN_W - 1 - y; py = x;                break;
    case 2:  px = SCREEN_W - 1 - x; py = SCREEN_H - 1 - y; break;
    case 3:  px = y;                py = SCREEN_H - 1 - x; break;
  }
  return (py * SCREEN_W + px) * 3;
}

}  // namespace

void setRotation(int quarterTurns) { rot = quarterTurns & 3; }
int  rotation() { return rot; }

Color hsv(float h, float s, float v) {
  h = fmodf_(h, 360.0f);
  if (h < 0) h += 360.0f;
  float c = v * s;
  float hp = h / 60.0f;
  float x = c * (1.0f - fabsf_(fmodf_(hp, 2.0f) - 1.0f));
  float r = 0, g = 0, b = 0;
  if (hp < 1)      { r = c; g = x; }
  else if (hp < 2) { r = x; g = c; }
  else if (hp < 3) { g = c; b = x; }
  else if (hp < 4) { g = x; b = c; }
  else if (hp < 5) { r = x; b = c; }
  else             { r = c; b = x; }
  float m = v - c;
  return {(uint8_t)((r + m) * 255.0f), (uint8_t)((g + m) * 255.0f),
          (uint8_t)((b + m) * 255.0f)};
}

void clear(Color c) {
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
    framebuffer[i * 3 + 0] = c.r;
    framebuffer[i * 3 + 1] = c.g;
    framebuffer[i * 3 + 2] = c.b;
  }
}

void pixel(int x, int y, Color c) {
  if ((unsigned)x >= (unsigned)SCREEN_W || (unsigned)y >= (unsigned)SCREEN_H) return;
  int i = fbIndex(x, y);
  framebuffer[i + 0] = c.r;
  framebuffer[i + 1] = c.g;
  framebuffer[i + 2] = c.b;
}

Color getPixel(int x, int y) {
  if ((unsigned)x >= (unsigned)SCREEN_W || (unsigned)y >= (unsigned)SCREEN_H) return BLACK;
  int i = fbIndex(x, y);
  return {framebuffer[i], framebuffer[i + 1], framebuffer[i + 2]};
}

void hline(int x, int y, int w, Color c) {
  for (int i = 0; i < w; i++) pixel(x + i, y, c);
}

void vline(int x, int y, int h, Color c) {
  for (int i = 0; i < h; i++) pixel(x, y + i, c);
}

void line(int x0, int y0, int x1, int y1, Color c) {
  int dx = absi(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -absi(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    pixel(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void rect(int x, int y, int w, int h, Color c) {
  hline(x, y, w, c);
  hline(x, y + h - 1, w, c);
  vline(x, y + 1, h - 2, c);
  vline(x + w - 1, y + 1, h - 2, c);
}

void fillRect(int x, int y, int w, int h, Color c) {
  for (int j = 0; j < h; j++) hline(x, y + j, w, c);
}

void circle(int cx, int cy, int r, Color c) {
  int x = -r, y = 0, err = 2 - 2 * r;
  do {
    pixel(cx - x, cy + y, c);
    pixel(cx - y, cy - x, c);
    pixel(cx + x, cy - y, c);
    pixel(cx + y, cy + x, c);
    int e = err;
    if (e <= y) err += ++y * 2 + 1;
    if (e > x || err > y) err += ++x * 2 + 1;
  } while (x < 0);
}

void fillCircle(int cx, int cy, int r, Color c) {
  for (int y = -r; y <= r; y++)
    for (int x = -r; x <= r; x++)
      if (x * x + y * y <= r * r) pixel(cx + x, cy + y, c);
}

// ---------------------------------------------------------------------------
// 3x5 font. One uint16_t per glyph: five rows of three bits, top row in the
// most significant bits.
// ---------------------------------------------------------------------------

static constexpr uint16_t G(int r0, int r1, int r2, int r3, int r4) {
  return (uint16_t)((r0 << 12) | (r1 << 9) | (r2 << 6) | (r3 << 3) | r4);
}

static const uint16_t FONT_DIGITS[10] = {
    G(0b111, 0b101, 0b101, 0b101, 0b111),  // 0
    G(0b010, 0b110, 0b010, 0b010, 0b111),  // 1
    G(0b111, 0b001, 0b111, 0b100, 0b111),  // 2
    G(0b111, 0b001, 0b011, 0b001, 0b111),  // 3
    G(0b101, 0b101, 0b111, 0b001, 0b001),  // 4
    G(0b111, 0b100, 0b111, 0b001, 0b111),  // 5
    G(0b111, 0b100, 0b111, 0b101, 0b111),  // 6
    G(0b111, 0b001, 0b001, 0b010, 0b010),  // 7
    G(0b111, 0b101, 0b111, 0b101, 0b111),  // 8
    G(0b111, 0b101, 0b111, 0b001, 0b111),  // 9
};

static const uint16_t FONT_UPPER[26] = {
    G(0b010, 0b101, 0b111, 0b101, 0b101),  // A
    G(0b110, 0b101, 0b110, 0b101, 0b110),  // B
    G(0b011, 0b100, 0b100, 0b100, 0b011),  // C
    G(0b110, 0b101, 0b101, 0b101, 0b110),  // D
    G(0b111, 0b100, 0b111, 0b100, 0b111),  // E
    G(0b111, 0b100, 0b111, 0b100, 0b100),  // F
    G(0b011, 0b100, 0b101, 0b101, 0b011),  // G
    G(0b101, 0b101, 0b111, 0b101, 0b101),  // H
    G(0b111, 0b010, 0b010, 0b010, 0b111),  // I
    G(0b001, 0b001, 0b001, 0b101, 0b010),  // J
    G(0b101, 0b110, 0b100, 0b110, 0b101),  // K
    G(0b100, 0b100, 0b100, 0b100, 0b111),  // L
    G(0b101, 0b111, 0b111, 0b101, 0b101),  // M
    G(0b111, 0b101, 0b101, 0b101, 0b101),  // N
    G(0b010, 0b101, 0b101, 0b101, 0b010),  // O
    G(0b110, 0b101, 0b110, 0b100, 0b100),  // P
    G(0b010, 0b101, 0b101, 0b110, 0b011),  // Q
    G(0b110, 0b101, 0b110, 0b101, 0b101),  // R
    G(0b011, 0b100, 0b010, 0b001, 0b110),  // S
    G(0b111, 0b010, 0b010, 0b010, 0b010),  // T
    G(0b101, 0b101, 0b101, 0b101, 0b111),  // U
    G(0b101, 0b101, 0b101, 0b101, 0b010),  // V
    G(0b101, 0b101, 0b111, 0b111, 0b101),  // W
    G(0b101, 0b101, 0b010, 0b101, 0b101),  // X
    G(0b101, 0b101, 0b010, 0b010, 0b010),  // Y
    G(0b111, 0b001, 0b010, 0b100, 0b111),  // Z
};

static uint16_t glyphFor(char ch) {
  if (ch >= '0' && ch <= '9') return FONT_DIGITS[ch - '0'];
  if (ch >= 'A' && ch <= 'Z') return FONT_UPPER[ch - 'A'];
  if (ch >= 'a' && ch <= 'z') return FONT_UPPER[ch - 'a'];
  switch (ch) {
    case '!': return G(0b010, 0b010, 0b010, 0b000, 0b010);
    case '.': return G(0b000, 0b000, 0b000, 0b000, 0b010);
    case ',': return G(0b000, 0b000, 0b000, 0b010, 0b100);
    case ':': return G(0b000, 0b010, 0b000, 0b010, 0b000);
    case '-': return G(0b000, 0b000, 0b111, 0b000, 0b000);
    case '+': return G(0b000, 0b010, 0b111, 0b010, 0b000);
    case '>': return G(0b100, 0b010, 0b001, 0b010, 0b100);
    case '<': return G(0b001, 0b010, 0b100, 0b010, 0b001);
    case '?': return G(0b110, 0b001, 0b010, 0b000, 0b010);
    case '/': return G(0b001, 0b001, 0b010, 0b100, 0b100);
    case '\'': return G(0b010, 0b010, 0b000, 0b000, 0b000);
    default:  return 0;  // space & unknown
  }
}

void text(int x, int y, const char* s, Color c, int scale) {
  int cx = x;
  for (const char* p = s; *p; p++) {
    if (*p == '\n') {
      cx = x;
      y += 6 * scale;
      continue;
    }
    uint16_t g = glyphFor(*p);
    for (int row = 0; row < 5; row++) {
      int bits = (g >> ((4 - row) * 3)) & 0b111;
      for (int col = 0; col < 3; col++) {
        if (bits & (0b100 >> col)) {
          if (scale == 1) pixel(cx + col, y + row, c);
          else fillRect(cx + col * scale, y + row * scale, scale, scale, c);
        }
      }
    }
    cx += 4 * scale;
  }
}

int textWidth(const char* s, int scale) {
  int n = 0, best = 0;
  for (const char* p = s; *p; p++) {
    if (*p == '\n') { n = 0; continue; }
    n++;
    if (n > best) best = n;
  }
  return best > 0 ? (best * 4 - 1) * scale : 0;
}

void textCentered(int y, const char* s, Color c, int scale) {
  text((SCREEN_W - textWidth(s, scale)) / 2, y, s, c, scale);
}

}  // namespace pt
