// RAVE is an audio-reactive light toy with 20 scenes. The host analyses the
// real music bus, so built-in tracks and PTA replacements drive the same
// level, bass, mid, high, and beat signals. UP/DOWN changes scenes.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int STYLE_COUNT = 20;
constexpr int PARTICLE_COUNT = 34;
constexpr int SONG_COUNT = 3;

const char* const STYLE_NAMES[STYLE_COUNT] = {
    "KALEIDO", "WORMHOLE", "SCOPE", "EQ CITY", "LASER GRID",
    "ORBITAL", "STAR BURST", "BUBBLES", "DNA", "FIREWORK",
    "CHECKER", "METERS", "METEORS", "MANDALA", "ACID RAIN",
    "HYPERCUBE", "PULSE BOX", "SPIRAL", "GLITCH", "BLACK HOLE",
};

const MusicTrack RAVE_TRACKS[SONG_COUNT] = {
    MUS_RAVE, MUS_RAVE_ACID, MUS_RAVE_DODGEMS,
};

const char* const SONG_NAMES[SONG_COUNT] = {
    "MECHANICAL", "TECHNO STYLE", "DODGEMS",
};

struct AudioFrame {
  float level, bass, mid, high;
  float bassHit, midHit, highHit, stab;
};

struct Particle {
  float x, y;
  float vx, vy;
  float life;
  float hue;
};

Particle particles[PARTICLE_COUNT];
int styleIndex;
int songIndex;
float styleBanner;
float songBanner;
float clickHeld;
float twist;
float motionPhase;
float musicDrive;
float tunnelPhase;
float centerX, centerY;
float shoveX, shoveY;
float zoomKick;
float beatPulse;
float hueBase;
float previousLevel, previousBass, previousMid, previousHigh, previousHostBeat;
float bassFloor, midFloor, highFloor;
float beatCooldown, stabCooldown;
float bassKick, midKick, highKick, stabKick;
float shockwave;
float eventAngle;
float paletteKick;
int sceneVariant;
int stabVariant;
int beatCount;
int sliceY;
int turnSign;

float signedNoise() { return randf() * 2.0f - 1.0f; }

float energyAt(const AudioFrame& a, int i) {
  switch (i & 3) {
    case 0: return a.bass;
    case 1: return a.mid;
    case 2: return a.high;
    default: return a.level;
  }
}

Color raveColor(float offset, float value, float saturation = 1.0f) {
  return hsv(hueBase + paletteKick + offset, saturation, clampf(value, 0, 1));
}

void spawnParticle(Particle& p, float force) {
  float angle = randf() * TWO_PI;
  float speed = (12.0f + randf() * 26.0f) * force;
  p.x = centerX + signedNoise() * 2.0f;
  p.y = centerY + signedNoise() * 2.0f;
  p.vx = cosf_(angle) * speed;
  p.vy = sinf_(angle) * speed;
  p.life = 0.45f + randf() * 0.75f;
  p.hue = randf() * 150.0f;
}

void resetParticles() {
  for (int i = 0; i < PARTICLE_COUNT; i++) {
    spawnParticle(particles[i], 0.8f + randf() * 0.6f);
    particles[i].life *= randf();
  }
}

void changeStyle(int direction) {
  styleIndex = (styleIndex + direction + STYLE_COUNT) % STYLE_COUNT;
  styleBanner = 0.85f;
  tunnelPhase = 0;
  hueBase = fmodf_(styleIndex * 71.0f + 265.0f, 360.0f);
  sceneVariant = styleIndex & 3;
  stabVariant = styleIndex & 3;
  shockwave = -1;
  bassKick = midKick = highKick = stabKick = 0;
  resetParticles();
  sfx(SFX_BLIP, 0.75f + styleIndex * 0.035f);
}

void changeSong() {
  songIndex = (songIndex + 1) % SONG_COUNT;
  music(RAVE_TRACKS[songIndex]);
  songBanner = 1.1f;
  previousLevel = previousBass = previousMid = previousHigh = previousHostBeat = 0;
  bassFloor = midFloor = highFloor = 0;
  sfx(SFX_SELECT, 0.9f + songIndex * 0.12f);
}

void init() {
  setSfxStyle(STYLE_CHIP);
  songIndex = 0;
  music(RAVE_TRACKS[songIndex]);
  styleIndex = 0;
  styleBanner = 1.1f;
  songBanner = 1.1f;
  clickHeld = 0;
  twist = 0;
  motionPhase = 0;
  musicDrive = 1;
  tunnelPhase = 0;
  centerX = centerY = 31.5f;
  shoveX = shoveY = 0;
  zoomKick = 0;
  beatPulse = 0;
  hueBase = 285.0f;
  previousLevel = previousBass = previousMid = previousHigh = previousHostBeat = 0;
  bassFloor = midFloor = highFloor = 0;
  beatCooldown = stabCooldown = 0;
  bassKick = midKick = highKick = stabKick = 0;
  shockwave = -1;
  eventAngle = 0;
  paletteKick = 0;
  sceneVariant = 0;
  stabVariant = 0;
  beatCount = 0;
  sliceY = 32;
  turnSign = 1;
  resetParticles();
  clear();
}

void triggerBeat(const AudioFrame& a) {
  beatCount++;
  beatPulse = 1;
  bassKick = fmaxf_(bassKick, 0.72f + a.bassHit * 0.28f);
  shockwave = 0;
  beatCooldown = 0.085f;
  eventAngle = ((beatCount + styleIndex) & 7) * TWO_PI / 8.0f;

  // A scene holds its visual grammar for a four-beat phrase. The next phrase
  // changes one designed layout parameter and every second phrase reverses
  // the motion. This reads as choreography instead of random redecoration.
  if ((beatCount & 3) == 0) {
    sceneVariant = (sceneVariant + 1) & 3;
    paletteKick = fmodf_(paletteKick + 45.0f + styleIndex * 3.0f, 360.0f);
  }
  if ((beatCount & 7) == 0) turnSign = -turnSign;
  zoomKick = clampf(zoomKick + 0.24f + a.bassHit * 0.20f, -0.75f, 1.0f);
  shoveX += cosf_(eventAngle) * (0.85f + a.bass * 1.35f);
  shoveY += sinf_(eventAngle) * (0.85f + a.bass * 1.35f);

  bool particleScene = styleIndex == 7 || styleIndex == 9 ||
                       styleIndex == 12 || styleIndex == 14;
  if (particleScene) {
    int phase = beatCount % 3;
    for (int i = phase; i < PARTICLE_COUNT; i += 3) {
      spawnParticle(particles[i], 0.95f + a.bass * 1.1f);
    }
  }
}

void triggerStab(const AudioFrame& a) {
  stabKick = 1;
  midKick = fmaxf_(midKick, a.midHit);
  highKick = fmaxf_(highKick, a.highHit);
  stabCooldown = 0.055f;
  stabVariant = (stabVariant + 1) & 3;
  eventAngle = ((stabVariant * 2 + styleIndex) & 7) * TWO_PI / 8.0f;
  sliceY = 10 + ((beatCount * 13 + stabVariant * 11 + styleIndex * 7) % 44);
  twist += turnSign * (0.09f + a.stab * 0.18f);
  shoveX += cosf_(eventAngle) * (0.80f + a.stab * 1.80f);
  shoveY += sinf_(eventAngle) * (0.80f + a.stab * 1.80f);
}

void analyseEvents(AudioFrame& a, float hostBeat, float dt) {
  float bassRise = fmaxf_(0, a.bass - previousBass);
  float midRise = fmaxf_(0, a.mid - previousMid);
  float highRise = fmaxf_(0, a.high - previousHigh);
  float levelRise = fmaxf_(0, a.level - previousLevel);

  a.bassHit = clampf(bassRise * 4.8f + fmaxf_(0, a.bass - bassFloor - 0.11f) * 2.2f, 0, 1);
  a.midHit = clampf(midRise * 5.8f + fmaxf_(0, a.mid - midFloor - 0.13f) * 1.8f, 0, 1);
  a.highHit = clampf(highRise * 6.6f + fmaxf_(0, a.high - highFloor - 0.12f) * 1.8f, 0, 1);
  a.stab = clampf(fmaxf_(a.midHit, a.highHit) + levelRise * 2.0f, 0, 1);

  float floorUp = clampf(dt * 1.25f, 0, 1);
  float floorDown = clampf(dt * 0.38f, 0, 1);
  bassFloor = lerpf(bassFloor, a.bass, a.bass > bassFloor ? floorUp : floorDown);
  midFloor = lerpf(midFloor, a.mid, a.mid > midFloor ? floorUp : floorDown);
  highFloor = lerpf(highFloor, a.high, a.high > highFloor ? floorUp : floorDown);

  beatCooldown = fmaxf_(0, beatCooldown - dt);
  stabCooldown = fmaxf_(0, stabCooldown - dt);
  bool hostOnset = hostBeat > 0.68f && previousHostBeat <= 0.68f;
  bool bassOnset = a.bassHit > 0.68f && bassRise > 0.025f;
  bool sharpRise = midRise > 0.028f || highRise > 0.025f || levelRise > 0.045f;
  if (beatCooldown <= 0 && (hostOnset || bassOnset)) triggerBeat(a);
  if (stabCooldown <= 0 && sharpRise && a.stab > 0.56f && a.bassHit < 0.92f) {
    triggerStab(a);
  }

  previousLevel = a.level;
  previousBass = a.bass;
  previousMid = a.mid;
  previousHigh = a.high;
  previousHostBeat = hostBeat;
}

void drawPolygon(int sides, float radius, float angle, Color color) {
  int firstX = 0, firstY = 0, lastX = 0, lastY = 0;
  for (int i = 0; i < sides; i++) {
    float a = angle + i * TWO_PI / sides;
    int x = (int)(centerX + cosf_(a) * radius);
    int y = (int)(centerY + sinf_(a) * radius);
    if (i == 0) firstX = x, firstY = y;
    else line(lastX, lastY, x, y, color);
    lastX = x;
    lastY = y;
  }
  line(lastX, lastY, firstX, firstY, color);
}

void drawTunnel(const AudioFrame& a) {
  int rings = 6 + (sceneVariant & 3);
  for (int i = 0; i < rings; i++) {
    float phase = fmodf_(tunnelPhase + i / (float)rings, 1.0f);
    float perspective = phase * phase;
    int radius = 2 + (int)(perspective * (42.0f + zoomKick * 7.0f + bassKick * 9.0f));
    float drift = perspective * 5.0f;
    int x = (int)(centerX + sinf_(twist + i * (0.63f + sceneVariant * 0.04f)) *
                               (drift + stabKick * 3.0f) + shoveX * perspective);
    int y = (int)(centerY + cosf_(twist * 0.73f + i * 0.67f + eventAngle * stabKick) *
                               drift + shoveY * perspective);
    float value = 0.28f + (1.0f - phase) * 0.55f + a.bass * 0.30f;
    Color c = raveColor(phase * 170.0f + a.high * 80.0f + sceneVariant * 27.0f, value);
    if ((sceneVariant & 2) && (i & 1)) {
      rect(x - radius, y - radius, radius * 2, radius * 2, c);
    } else {
      circle(x, y, radius, c);
    }
  }
}

void drawKaleido(const AudioFrame& a) {
  const int blades = 8 + (sceneVariant % 5) * 2;
  float inner = 3.0f + a.bass * 5.0f;
  float outer = 23.0f + a.high * 10.0f + beatPulse * 8.0f + bassKick * 7.0f;
  for (int i = 0; i < blades; i++) {
    float angle = twist + i * TWO_PI / blades + eventAngle * stabKick * 0.18f;
    float wobble = sinf_(motionPhase * (2.2f + a.high * 2.0f) + i *
                         (1.21f + sceneVariant * 0.17f));
    float reach = outer + wobble * (2.0f + a.mid * 5.0f + midKick * 5.0f);
    int x0 = (int)(centerX + cosf_(angle) * inner);
    int y0 = (int)(centerY + sinf_(angle) * inner);
    int x1 = (int)(centerX + cosf_(angle + wobble * 0.055f) * reach);
    int y1 = (int)(centerY + sinf_(angle + wobble * 0.055f) * reach);
    line(x0, y0, x1, y1,
         raveColor(i * 30.0f + a.high * 95.0f,
                   0.40f + a.mid * 0.45f + beatPulse * 0.35f));
    float fork = angle - wobble * (0.08f + (sceneVariant & 3) * 0.045f);
    int fx = (int)(centerX + cosf_(fork) * reach * 0.72f);
    int fy = (int)(centerY + sinf_(fork) * reach * 0.72f);
    line(x1, y1, fx, fy, raveColor(180.0f + i * 22.0f, 0.25f + a.high * 0.60f));
  }
  int innerRays = 10 + sceneVariant * 2;
  for (int i = 0; i < innerRays; i++) {
    float energy = energyAt(a, i);
    float angle = -twist * 0.65f + i * TWO_PI / innerRays;
    float r0 = 8.0f + beatPulse * 3.0f;
    float r1 = r0 + 2.0f + energy * 14.0f;
    line((int)(centerX + cosf_(angle) * r0), (int)(centerY + sinf_(angle) * r0),
         (int)(centerX + cosf_(angle) * r1), (int)(centerY + sinf_(angle) * r1),
         raveColor(i * 15.0f, 0.35f + energy * 0.75f));
  }
  fillCircle((int)centerX, (int)centerY,
             clampi(2 + (int)(a.bass * 4 + beatPulse * 2), 1, 7),
             raveColor(60, 0.5f + a.level * 0.5f, 0.75f));
}

void drawWormhole(const AudioFrame& a) {
  drawTunnel(a);
  int spokes = 10 + sceneVariant * 2;
  float corkscrew = twist * (1.25f + sceneVariant * 0.11f);
  for (int i = 0; i < spokes; i++) {
    float angle = corkscrew + i * TWO_PI / spokes;
    float energy = energyAt(a, i);
    float nearRadius = 3.0f + energy * 4.0f + midKick * ((i & 1) ? 5.0f : 0);
    float farRadius = 41.0f + zoomKick * 6.0f + bassKick * 9.0f;
    line((int)(centerX + cosf_(angle) * nearRadius),
         (int)(centerY + sinf_(angle) * nearRadius),
         (int)(centerX + cosf_(angle + tunnelPhase * 1.8f) * farRadius),
         (int)(centerY + sinf_(angle + tunnelPhase * 1.8f) * farRadius),
         raveColor(i * 20.0f, 0.35f + energy * 0.65f));
  }
  fillCircle((int)centerX, (int)centerY, 2 + (int)(a.bass * 3), BLACK);
  pixel((int)centerX, (int)centerY, WHITE);
}

void drawScope(const AudioFrame& a) {
  int xShift = (int)(centerX - 31.5f);
  float scale = 1.0f + zoomKick * 0.25f;
  for (int trace = 0; trace < 4; trace++) {
    float energy = energyAt(a, trace);
    float baseY = centerY - 18.0f + trace * 12.0f;
    int lastX = xShift;
    int lastY = (int)baseY;
    for (int x = 1; x < 64; x++) {
      float phase = x * (0.12f + trace * 0.075f + sceneVariant * 0.012f) +
                    twist * (trace + 1) + motionPhase * (1.4f + trace * 0.85f);
      float wave = sinf_(phase);
      if ((sceneVariant & 3) == 1) wave = sinf_(phase) >= 0 ? 0.85f : -0.85f;
      if ((sceneVariant & 3) == 2) wave = sinf_(phase) + sinf_(phase * 2.73f) * 0.42f;
      if ((sceneVariant & 3) == 3) wave *= fabsf_(sinf_(phase * 0.5f)) * 1.5f;
      float chop = (trace == 3 || trace == stabVariant) ?
          sinf_(x * (1.2f + stabVariant * 0.11f) + motionPhase * 6.0f) *
              (a.high + highKick) : 0;
      int y = (int)(baseY + (wave * (2.0f + energy * 7.0f + midKick * 4.0f) +
                              chop * 3.0f) * scale);
      line(lastX, lastY, x + xShift, y,
           raveColor(trace * 82.0f, 0.35f + energy * 0.65f));
      lastX = x + xShift;
      lastY = y;
    }
  }
  vline((int)centerX, 0, 64, raveColor(180, 0.35f + beatPulse * 0.5f));
}

void drawEqCity(const AudioFrame& a) {
  int horizon = clampi((int)centerY, 23, 46);
  int shift = (int)(centerX - 31.5f);
  int columns = 12 + (sceneVariant & 3) * 2;
  int width = 64 / columns;
  for (int i = 0; i < columns; i++) {
    float energy = energyAt(a, i);
    float flicker = 0.55f + 0.45f *
        sinf_(motionPhase * (2.2f + a.high * 3.5f) + i * 2.1f + twist);
    float hit = (i & 1) ? highKick : midKick;
    int height = 3 + (int)((energy * flicker + hit * 0.55f) *
                           (22.0f + zoomKick * 5.0f + bassKick * 7.0f));
    int x = i * width + shift;
    Color c = raveColor(i * 19.0f, 0.30f + energy * 0.70f);
    fillRect(x, horizon - height, width - 1, height, c);
    if ((sceneVariant & 1) && (i & 1)) fillRect(x, horizon, width - 1, height / 2, c);
    int reflection = height / 2;
    for (int y = 0; y < reflection; y += 3) hline(x, horizon + 2 + y, width - 1, c);
  }
  hline(0, horizon, 64, raveColor(190, 0.4f + a.bass * 0.6f));
}

void drawLaserGrid(const AudioFrame& a) {
  int horizon = clampi((int)centerY + ((sceneVariant & 1) ? -7 : 5), 10, 52);
  Color grid = raveColor(190, 0.25f + a.mid * 0.55f);
  int spacing = 8 + (sceneVariant % 4) * 3;
  for (int x = -32; x <= 96; x += spacing) {
    float lean = sinf_(twist + eventAngle * stabKick) * (14.0f + midKick * 12.0f);
    line((int)centerX, horizon, x + (int)lean, 63, grid);
    line((int)centerX, horizon, x - (int)lean, 0, raveColor(280, 0.2f + a.high * 0.5f));
  }
  for (int i = 1; i < 10; i++) {
    float p = i / 10.0f;
    int gap = (int)(p * p * 38.0f * (1.0f + zoomKick * 0.18f));
    hline(0, horizon + gap, 64, grid);
    hline(0, horizon - gap, 64, raveColor(280, 0.2f + a.high * 0.5f));
  }
  int strikes = 2 + (sceneVariant & 1) + (int)(a.high * 2 + highKick * 2);
  for (int i = 0; i < strikes; i++) {
    float angle = twist * 2.0f + i * (0.83f + stabVariant * 0.11f) +
                  eventAngle * stabKick;
    line((int)centerX, horizon,
         (int)(centerX + cosf_(angle) * 46),
         (int)(horizon + sinf_(angle) * 46),
         raveColor(i * 67.0f, 0.55f + beatPulse * 0.45f));
  }
}

void orbitPoint(float radius, float squash, float angle, float turn, int& x, int& y) {
  float ox = cosf_(angle) * radius;
  float oy = sinf_(angle) * radius * squash;
  x = (int)(centerX + ox * cosf_(turn) - oy * sinf_(turn));
  y = (int)(centerY + ox * sinf_(turn) + oy * cosf_(turn));
}

void drawOrbital(const AudioFrame& a) {
  int rings = 5 + (sceneVariant % 4);
  for (int ring = 0; ring < rings; ring++) {
    float radius = 7.0f + ring * 4.0f + zoomKick * 2.0f;
    float turn = twist * (ring & 1 ? -0.55f : 0.72f) + ring * 0.43f +
                 stabKick * eventAngle;
    Color c = raveColor(ring * 48.0f, 0.25f + energyAt(a, ring) * 0.7f);
    for (int j = 0; j < 36; j++) {
      int x, y;
      orbitPoint(radius, 0.35f + ring * 0.045f, j * TWO_PI / 36, turn, x, y);
      pixel(x, y, c);
    }
    float bodyAngle = motionPhase * (0.7f + ring * 0.19f) + ring + twist;
    int bx, by;
    orbitPoint(radius, 0.35f + ring * 0.045f, bodyAngle, turn, bx, by);
    fillCircle(bx, by, 1 + (int)(energyAt(a, ring) * 2), c);
    if (highKick > 0.2f) {
      int tx, ty;
      orbitPoint(radius, 0.35f + ring * 0.045f, bodyAngle + 0.42f, turn, tx, ty);
      line(bx, by, tx, ty, raveColor(ring * 48.0f + 180.0f, highKick));
    }
  }
  fillCircle((int)centerX, (int)centerY, 3 + (int)(a.bass * 4),
             raveColor(55, 0.55f + a.bass * 0.45f));
}

void drawStarBurst(const AudioFrame& a) {
  int rays = 14 + sceneVariant * 4;
  for (int i = 0; i < rays; i++) {
    float energy = energyAt(a, i);
    float angle = twist + i * TWO_PI / rays + eventAngle * stabKick * 0.4f;
    float inner = 2.0f + (i & 1) * (3.0f + (sceneVariant & 3));
    float outer = 15.0f + energy * 25.0f + beatPulse * 9.0f + zoomKick * 5.0f +
                  ((i + stabVariant) % 3 == 0 ? highKick * 12.0f : 0);
    line((int)(centerX + cosf_(angle) * inner),
         (int)(centerY + sinf_(angle) * inner),
         (int)(centerX + cosf_(angle) * outer),
         (int)(centerY + sinf_(angle) * outer),
         raveColor(i * 25.0f, 0.32f + energy * 0.68f));
  }
  fillCircle((int)centerX, (int)centerY, 2 + (int)(beatPulse * 5), WHITE);
}

void drawBubbles(const AudioFrame& a) {
  int count = 10 + sceneVariant;
  for (int i = 0; i < count; i++) {
    float energy = energyAt(a, i);
    float phase = motionPhase * (0.55f + i * 0.04f) +
                  i * (1.51f + sceneVariant * 0.08f) + twist + eventAngle * stabKick;
    float radiusFromCenter = 5.0f + (i % 5) * 7.0f;
    int x = (int)(centerX + cosf_(phase) * radiusFromCenter);
    int y = (int)(centerY + sinf_(phase * 1.37f) * radiusFromCenter);
    int radius = 2 + i % 4 + (int)(energy * 4.0f + zoomKick * 1.5f);
    Color c = raveColor(i * 39.0f, 0.32f + energy * 0.65f, 0.78f);
    if ((sceneVariant & 2) && (i % 3 == 0)) fillCircle(x, y, clampi(radius, 1, 9), c);
    else circle(x, y, clampi(radius, 1, 9), c);
    pixel(x - radius / 3, y - radius / 3, WHITE);
  }
}

void drawDna(const AudioFrame& a) {
  float amplitude = 9.0f + a.bass * 9.0f + zoomKick * 3.0f + bassKick * 6.0f;
  int xShift = (int)(centerX - 31.5f);
  for (int x = 0; x < 64; x++) {
    float phase = x * (0.18f + sceneVariant * 0.025f) +
                  motionPhase * (1.5f + a.high * 2.5f) + twist;
    int y1 = (int)(centerY + sinf_(phase) * amplitude);
    int y2 = (int)(centerY - sinf_(phase) * amplitude);
    Color c1 = raveColor(x * 5.0f, 0.35f + a.mid * 0.65f);
    Color c2 = raveColor(180.0f + x * 5.0f, 0.35f + a.high * 0.65f);
    int px1 = x + xShift, py1 = y1, px2 = x + xShift, py2 = y2;
    if (sceneVariant & 1) {
      px1 = (int)(centerX + sinf_(phase) * amplitude);
      px2 = (int)(centerX - sinf_(phase) * amplitude);
      py1 = py2 = x + (int)(centerY - 31.5f);
    }
    pixel(px1, py1, c1);
    pixel(px2, py2, c2);
    if ((x & 3) == 0) line(px1, py1, px2, py2,
                           raveColor(90, 0.25f + a.bass * 0.5f + midKick * 0.35f));
  }
}

void updateParticles(float dt, const AudioFrame& a) {
  float spinForce = input.spin * 0.42f;
  for (int i = 0; i < PARTICLE_COUNT; i++) {
    Particle& p = particles[i];
    float oldX = p.x, oldY = p.y;
    float oldVx = p.vx;
    p.vx += (-p.vy * spinForce - input.accelX * 42.0f) * dt;
    p.vy += (oldVx * spinForce - input.accelY * 42.0f) * dt;
    float thrust = 1.0f + a.level * 0.9f + beatPulse * 0.8f + bassKick * 1.3f +
                   stabKick * 0.55f + input.accelZ * 0.18f;
    p.x += p.vx * dt * thrust;
    p.y += p.vy * dt * thrust;
    p.life -= dt * (0.72f + a.high * 0.55f);
    if (p.life <= 0 || p.x < -8 || p.x > 72 || p.y < -8 || p.y > 72) {
      spawnParticle(p, 0.65f + a.level * 1.25f + beatPulse * 0.65f);
      continue;
    }
    Color c = raveColor(p.hue + a.high * 120.0f, clampf(p.life * 2.2f, 0.25f, 1));
    line((int)oldX, (int)oldY, (int)p.x, (int)p.y, c);
    if (a.high > 0.48f) pixel((int)p.x + 1, (int)p.y, c);
  }
}

void drawFirework(const AudioFrame& a, float dt) {
  updateParticles(dt, a);
  int bursts = 2 + (sceneVariant & 1);
  for (int burst = 0; burst < bursts; burst++) {
    float phase = motionPhase * (0.6f + burst * 0.13f) + burst * 1.8f + twist;
    int bx = (int)(centerX + cosf_(phase) * (8 + burst * 4));
    int by = (int)(centerY + sinf_(phase * 1.3f) * (7 + burst * 3));
    int radius = 3 + (int)(energyAt(a, burst) * 10 + beatPulse * 5 + zoomKick * 2 +
                           ((burst + stabVariant) & 1 ? highKick * 8.0f : bassKick * 5.0f));
    circle(bx, by, clampi(radius, 1, 16),
           raveColor(burst * 88.0f, 0.4f + energyAt(a, burst) * 0.6f));
  }
}

void drawChecker(const AudioFrame& a) {
  int xShift = (int)((centerX - 31.5f) * 0.45f);
  int yShift = (int)((centerY - 31.5f) * 0.45f);
  int cell = clampi(4 + (sceneVariant % 5) + (int)(zoomKick * 2), 4, 10);
  for (int gy = -1; gy < 17; gy++) {
    int warp = (int)(sinf_(gy * 0.82f + twist + motionPhase * a.mid * 2.0f) *
                     (3.0f + a.bass * 5.0f));
    for (int gx = -1; gx < 17; gx++) {
      float energy = energyAt(a, gx + gy);
      int pattern = sceneVariant & 3;
      bool lit = pattern == 0 ? ((gx + gy + beatCount) & 1) == 0 :
                 pattern == 1 ? ((gx ^ gy ^ beatCount) & 1) == 0 :
                 pattern == 2 ? ((gx * gy + beatCount) % 3) != 0 :
                                ((gx + gy + (int)(motionPhase * (1 + a.high * 2))) & 2) == 0;
      if (lit) {
        int punch = ((gx + gy + stabVariant) % 4 == 0) ? (int)(stabKick * 3) : 0;
        fillRect(gx * cell + xShift + warp - punch, gy * cell + yShift - punch,
                 cell - 1 + punch * 2, cell - 1 + punch * 2,
                 raveColor((gx - gy) * 31.0f, 0.32f + energy * 0.58f));
      }
    }
  }
}

void drawMeters(const AudioFrame& a) {
  const char* labels[4] = {"L", "B", "M", "H"};
  float values[4] = {a.level, a.bass, a.mid, a.high};
  int xShift = (int)((centerX - 31.5f) * 0.35f);
  int yShift = (int)((centerY - 31.5f) * 0.2f);
  for (int i = 0; i < 4; i++) {
    int y = 5 + i * 14 + yShift;
    float hit = i == 1 ? bassKick : i == 2 ? midKick : i == 3 ? highKick : stabKick;
    int width = (int)(fmaxf_(values[i], hit) * (47.0f + zoomKick * 5.0f));
    text(2 + xShift, y, labels[i], raveColor(i * 90.0f, 0.8f));
    rect(8 + xShift, y, 50, 7, raveColor(i * 90.0f, 0.25f + values[i] * 0.5f));
    int fillWidth = clampi(width, 0, 46);
    int fillX = (sceneVariant & 1) ? 56 + xShift - fillWidth : 10 + xShift;
    fillRect(fillX, y + 2, fillWidth, 3,
             raveColor(i * 90.0f + twist * 18.0f, 0.45f + fmaxf_(values[i], hit) * 0.55f));
    int peak = 10 + xShift + clampi(width + (int)(sinf_(twist + i) * 2), 0, 46);
    vline(peak, y + 1, 5, WHITE);
  }
}

uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  return x ^ (x >> 16);
}

void drawMeteors(const AudioFrame& a) {
  float angle = -1.2f + sceneVariant * 0.37f + sinf_(twist) * 0.65f +
                eventAngle * stabKick * 0.35f;
  float dx = cosf_(angle), dy = sinf_(angle);
  int shiftX = (int)(centerX - 31.5f);
  int shiftY = (int)(centerY - 31.5f);
  for (int i = 0; i < 25; i++) {
    uint32_t h = hash32(i * 733u + 19u + beatCount * 1229u + stabVariant * 313u);
    float speed = 10.0f + (h & 15) + a.high * 35.0f;
    float travel = fmodf_(motionPhase * speed * 0.55f + ((h >> 8) & 127) + 184.0f,
                          92.0f) - 14.0f;
    float lane = ((h >> 16) & 127) - 32.0f;
    int x = (int)(lane + dx * travel + shiftX);
    int y = (int)(lane * 0.43f + dy * travel + shiftY + 18);
    float energy = energyAt(a, i);
    int tail = 2 + (int)(energy * 11 + beatPulse * 4 + zoomKick * 2 + highKick * 9);
    line(x, y, x - (int)(dx * tail), y - (int)(dy * tail),
         raveColor(i * 47.0f, 0.3f + energy * 0.7f));
    pixel(x, y, WHITE);
  }
}

void drawMandala(const AudioFrame& a) {
  int rings = 7 + (sceneVariant % 4);
  for (int ring = 0; ring < rings; ring++) {
    int sides = 3 + ((ring + sceneVariant) % 7);
    float energy = energyAt(a, ring);
    float radius = 3.0f + ring * 3.7f + energy * 4.0f + zoomKick * 2.0f +
                   ((ring + beatCount) & 1 ? midKick * 5.0f : bassKick * 4.0f);
    float angle = twist * (ring & 1 ? -0.65f : 0.8f) + ring * 0.31f +
                  eventAngle * stabKick;
    drawPolygon(sides, radius, angle,
                raveColor(ring * 43.0f, 0.28f + energy * 0.68f));
  }
  fillCircle((int)centerX, (int)centerY, 1 + (int)(a.bass * 3), WHITE);
}

void drawRain(const AudioFrame& a) {
  int shiftX = (int)(centerX - 31.5f);
  int shiftY = (int)(centerY - 31.5f);
  int slant = (int)(sinf_(twist + sceneVariant * 0.72f) *
                    (5.0f + midKick * 8.0f));
  for (int i = 0; i < 22; i++) {
    uint32_t h = hash32(i * 991u + 7u);
    int x = (int)((h & 63) + shiftX + motionPhase * slant * 0.7f) & 63;
    float speed = 13.0f + ((h >> 8) & 15) + a.high * 40.0f;
    int y = (int)fmodf_(motionPhase * speed * 0.65f + ((h >> 16) & 63) +
                        shiftY + 304, 76) - 6;
    float energy = energyAt(a, i);
    int length = 2 + (int)(energy * 7 + zoomKick * 2 + highKick * 7);
    line(x, y, x - slant / 2, y - length,
         raveColor(120.0f + i * 9.0f, 0.3f + energy * 0.68f));
  }
  for (int i = 0; i < 4; i++) {
    int radius = 4 + i * 7 + (int)(a.bass * 4);
    circle((int)centerX, 63 + (int)(centerY - 31.5f), radius,
           raveColor(180 + i * 25.0f, 0.28f + a.bass * 0.65f));
  }
}

void drawCube(const AudioFrame& a) {
  int px[8], py[8];
  float size = 17.0f + a.bass * 8.0f + beatPulse * 4.0f + zoomKick * 5.0f +
               bassKick * 7.0f;
  float ay = twist + eventAngle * stabKick;
  float ax = motionPhase * (0.35f + a.mid) + input.tiltY * 0.8f +
             midKick * 0.7f;
  for (int i = 0; i < 8; i++) {
    float x = (i & 1) ? 1.0f : -1.0f;
    float y = (i & 2) ? 1.0f : -1.0f;
    float z = (i & 4) ? 1.0f : -1.0f;
    float x1 = x * cosf_(ay) - z * sinf_(ay);
    float z1 = x * sinf_(ay) + z * cosf_(ay);
    float y1 = y * cosf_(ax) - z1 * sinf_(ax);
    float z2 = y * sinf_(ax) + z1 * cosf_(ax);
    float perspective = size / (3.2f + z2 * 0.55f);
    px[i] = (int)(centerX + x1 * perspective);
    py[i] = (int)(centerY + y1 * perspective);
  }
  for (int i = 0; i < 8; i++) {
    for (int axis = 0; axis < 3; axis++) {
      int j = i ^ (1 << axis);
      if (i < j) line(px[i], py[i], px[j], py[j],
                      raveColor(i * 41.0f + axis * 70.0f,
                                0.35f + energyAt(a, i + axis) * 0.65f));
    }
    fillCircle(px[i], py[i], 1 + (int)(a.high * 1.5f), WHITE);
  }
  if (sceneVariant & 1) {
    for (int i = 0; i < 4; i++) line(px[i], py[i], px[i ^ 7], py[i ^ 7],
                                     raveColor(120.0f + i * 45.0f,
                                               0.25f + highKick * 0.75f));
  }
  if (sceneVariant & 2) {
    int sides = 3 + sceneVariant % 5;
    drawPolygon(sides, 5.0f + stabKick * 8.0f, -ay * 1.7f, WHITE);
  }
}

void drawPulseBox(const AudioFrame& a) {
  int layers = 8 + (sceneVariant % 5);
  for (int i = 0; i < layers; i++) {
    float energy = energyAt(a, i);
    float pulse = 0.7f + 0.3f *
        sinf_(motionPhase * (1.6f + a.bass * 2.8f) + i * 0.7f + twist);
    int size = 4 + (int)(i * 3.2f * pulse + energy * 4.0f + zoomKick * 3.0f +
                         ((i + beatCount) & 1 ? midKick * 5.0f : bassKick * 4.0f));
    int wobbleX = (int)(cosf_(twist + i) * energy * 3.0f);
    int wobbleY = (int)(sinf_(twist * 1.3f + i) * energy * 3.0f);
    Color c = raveColor(i * 37.0f, 0.25f + energy * 0.72f);
    if (sceneVariant & 1) circle((int)centerX + wobbleX, (int)centerY + wobbleY, size, c);
    else rect((int)centerX - size + wobbleX, (int)centerY - size + wobbleY,
              size * 2, size * 2, c);
  }
  line((int)centerX - 31, (int)centerY, (int)centerX + 31, (int)centerY,
       raveColor(170, 0.3f + a.mid * 0.6f));
  line((int)centerX, (int)centerY - 31, (int)centerX, (int)centerY + 31,
       raveColor(260, 0.3f + a.high * 0.6f));
}

void drawSpiral(const AudioFrame& a) {
  int arms = 1 + (sceneVariant & 1);
  for (int arm = 0; arm < arms; arm++) {
    int lastX = (int)centerX, lastY = (int)centerY;
    for (int i = 1; i < 112; i++) {
      float p = i / 111.0f;
      float energy = energyAt(a, i + arm);
      float angle = twist + arm * TWO_PI / arms +
                    p * (10.0f + sceneVariant + a.high * 12.0f + highKick * 8.0f) +
                    motionPhase * a.mid;
      float radius = p * (38.0f + zoomKick * 6.0f + bassKick * 6.0f) +
                     sinf_(p * 18 + motionPhase * 2.2f) * (a.bass + midKick) * 3;
      int x = (int)(centerX + cosf_(angle) * radius);
      int y = (int)(centerY + sinf_(angle) * radius);
      line(lastX, lastY, x, y,
           raveColor(p * 330.0f + arm * 90.0f, 0.3f + energy * 0.65f));
      lastX = x;
      lastY = y;
    }
  }
}

void drawGlitch(const AudioFrame& a) {
  int frame = (int)(motionPhase * (10.0f + a.high * 18.0f + highKick * 32.0f)) +
              beatCount * 37 + stabVariant * 101;
  int xShift = (int)(centerX - 31.5f);
  int yShift = (int)(centerY - 31.5f);
  for (int row = 0; row < 22; row++) {
    uint32_t h = hash32(frame * 131u + row * 977u);
    float energy = energyAt(a, row);
    int y = (row * 3 + yShift + (int)(sinf_(twist + row) * 2)) & 63;
    int width = 3 + (h & 15) + (int)(energy * 31 + zoomKick * 4 + stabKick * 25);
    int x = ((h >> 8) & 63) - width / 2 + xShift;
    fillRect(x, y, width, 1 + ((h >> 15) & 1),
             raveColor((h >> 17) & 255, 0.28f + energy * 0.7f));
    if ((beatPulse > 0.45f || highKick > 0.35f) &&
        (row % (3 + sceneVariant % 4)) == 0) hline(0, y, 64, WHITE);
  }
  int tear = ((int)(motionPhase * 18) + (int)(twist * 7)) & 63;
  fillRect(0, tear, 64, 2, raveColor(180, 0.5f + a.bass * 0.5f));
}

void drawBlackHole(const AudioFrame& a) {
  int rings = 6 + (sceneVariant & 1);
  for (int ring = 0; ring < rings; ring++) {
    float radius = 7.0f + ring * 3.2f + a.bass * 3.0f + zoomKick * 3.0f +
                   ((ring + beatCount) & 1 ? bassKick * 5.0f : midKick * 3.0f);
    float turn = twist + ring * (0.13f + sceneVariant * 0.025f) +
                 eventAngle * stabKick;
    Color c = raveColor(20.0f + ring * 26.0f, 0.28f + energyAt(a, ring) * 0.68f);
    for (int j = 0; j < 40; j++) {
      if (((j + ring) % 5) == 0 && a.high < 0.45f) continue;
      int x, y;
      orbitPoint(radius, 0.16f + sceneVariant * 0.025f + ring * 0.018f,
                 j * TWO_PI / 40, turn, x, y);
      pixel(x, y, c);
    }
  }
  for (int i = 0; i < 12; i++) {
    float angle = -twist * 1.4f + i * TWO_PI / 12 + eventAngle * stabKick;
    float outer = 27.0f + energyAt(a, i) * 14.0f + highKick * ((i % 3) * 4.0f);
    line((int)(centerX + cosf_(angle + 0.5f) * outer),
         (int)(centerY + sinf_(angle + 0.5f) * outer),
         (int)(centerX + cosf_(angle) * 7),
         (int)(centerY + sinf_(angle) * 7),
         raveColor(i * 20.0f, 0.25f + energyAt(a, i) * 0.65f));
  }
  fillCircle((int)centerX, (int)centerY, 5 + (int)(beatPulse * 3), BLACK);
  circle((int)centerX, (int)centerY, 6 + (int)(beatPulse * 3), WHITE);
}

void drawReactiveFx(const AudioFrame& a) {
  float beatFade = shockwave >= 0 && shockwave < 1 ? 1.0f - shockwave : 0;
  float stab = fmaxf_(midKick, highKick);
  int radius = 3 + (int)((shockwave < 0 ? 0 : shockwave * shockwave) * 43.0f);
  Color beatColor = raveColor(35.0f + sceneVariant * 45.0f,
                              0.22f + beatFade * 0.62f);
  Color stabColor = raveColor(150.0f + stabVariant * 38.0f,
                              0.28f + stab * 0.62f);

  // Each scene gets one accent that belongs to its design. The scene itself
  // handles every other response through scale, phase, speed, or layout.
  switch (styleIndex) {
    case 0:  // kaleidoscope echo
      if (beatFade > 0.08f) drawPolygon(5 + sceneVariant, radius * 0.72f,
                                        twist, beatColor);
      break;
    case 1:  // tunnel mouth
    case 5:  // orbital wave
    case 19: // event horizon
      if (beatFade > 0.08f) circle((int)centerX, (int)centerY, radius, beatColor);
      break;
    case 2:  // scope trigger marker
      if (stab > 0.12f) {
        int x = 8 + ((sliceY + stabVariant * 13) % 48);
        vline(x, 3, 58, stabColor);
      }
      break;
    case 3:  // city horizon sweep
      if (stab > 0.12f) hline(0, clampi(sliceY, 8, 56), 64, stabColor);
      break;
    case 4:  // one deliberate laser strike
      if (highKick > 0.12f) {
        line((int)centerX, (int)centerY,
             (int)(centerX + cosf_(eventAngle) * 48),
             (int)(centerY + sinf_(eventAngle) * 48), stabColor);
      }
      break;
    case 6:  // star core ring
      if (beatFade > 0.08f) circle((int)centerX, (int)centerY,
                                    4 + (int)(radius * 0.55f), beatColor);
      break;
    case 8:  // a single DNA rung
      if (stab > 0.12f) {
        if (sceneVariant & 1) hline(4, sliceY, 56, stabColor);
        else vline(sliceY, 4, 56, stabColor);
      }
      break;
    case 10: // checker seam
      if (stab > 0.12f) {
        int d = (int)(stab * 5.0f) * turnSign;
        line(0, clampi(sliceY - d, 0, 63), 63, clampi(sliceY + d, 0, 63), stabColor);
      }
      break;
    case 13: // mandala echo
      if (beatFade > 0.08f) drawPolygon(3 + sceneVariant * 2, radius * 0.72f,
                                        eventAngle, beatColor);
      break;
    case 14: // one rain slash
      if (highKick > 0.15f) {
        int x = 8 + ((sliceY + styleIndex * 7) % 48);
        line(x, 0, x - turnSign * (5 + stabVariant * 2), 24, stabColor);
      }
      break;
    case 15: // projected cube echo
      if (beatFade > 0.08f) {
        int r = 3 + (int)(radius * 0.58f);
        rect((int)centerX - r, (int)centerY - r, r * 2, r * 2, beatColor);
      }
      break;
    case 16: // pulse-box outer cadence
      if (beatFade > 0.08f) {
        if (sceneVariant & 1) circle((int)centerX, (int)centerY, radius, beatColor);
        else rect((int)centerX - radius, (int)centerY - radius,
                  radius * 2, radius * 2, beatColor);
      }
      break;
    case 18: // one clean glitch tear
      if (stab > 0.12f) {
        int y = clampi(sliceY + (int)(stabKick * 4) * turnSign, 1, 62);
        fillRect(0, y, 64, 2, BLACK);
        hline(0, y + turnSign, 64, stabColor);
      }
      break;
    default:
      break;
  }
}

void drawStyle(const AudioFrame& a, float dt) {
  switch (styleIndex) {
    case 0: drawKaleido(a); break;
    case 1: drawWormhole(a); break;
    case 2: drawScope(a); break;
    case 3: drawEqCity(a); break;
    case 4: drawLaserGrid(a); break;
    case 5: drawOrbital(a); break;
    case 6: drawStarBurst(a); break;
    case 7: drawBubbles(a); break;
    case 8: drawDna(a); break;
    case 9: drawFirework(a, dt); break;
    case 10: drawChecker(a); break;
    case 11: drawMeters(a); break;
    case 12: drawMeteors(a); break;
    case 13: drawMandala(a); break;
    case 14: drawRain(a); break;
    case 15: drawCube(a); break;
    case 16: drawPulseBox(a); break;
    case 17: drawSpiral(a); break;
    case 18: drawGlitch(a); break;
    default: drawBlackHole(a); break;
  }
}

void update(float dt) {
  if (input.justDown(BTN_UP)) changeStyle(-1);
  if (input.justDown(BTN_DOWN)) changeStyle(1);
  if (input.justDown(BTN_CLICK)) clickHeld = 0;
  if (input.held(BTN_CLICK)) clickHeld += dt;
  if (input.justUp(BTN_CLICK)) {
    if (clickHeld < 0.5f) changeSong();
    clickHeld = 0;
  }

  AudioFrame a = {
      clampf(musicAnalysis.level, 0, 1),
      clampf(musicAnalysis.bass, 0, 1),
      clampf(musicAnalysis.mid, 0, 1),
      clampf(musicAnalysis.high, 0, 1),
      0, 0, 0, 0,
  };
  float beat = clampf(musicAnalysis.beat, 0, 1);

  beatPulse *= clampf(1.0f - 5.2f * dt, 0, 1);
  bassKick *= clampf(1.0f - 4.0f * dt, 0, 1);
  midKick *= clampf(1.0f - 7.5f * dt, 0, 1);
  highKick *= clampf(1.0f - 10.5f * dt, 0, 1);
  stabKick *= clampf(1.0f - 8.5f * dt, 0, 1);
  analyseEvents(a, beat, dt);
  beatPulse = fmaxf_(beatPulse, beat);
  if (shockwave >= 0) {
    shockwave += dt * (1.25f + a.bass * 0.8f);
    if (shockwave >= 1) shockwave = -1;
  }
  float driveTarget = clampf(0.80f + a.level * 1.05f + a.bass * 0.35f +
                             a.high * 0.45f, 0.80f, 2.65f);
  float driveRate = driveTarget > musicDrive ? 7.0f : 2.4f;
  musicDrive = lerpf(musicDrive, driveTarget, clampf(dt * driveRate, 0, 1));
  a.level = fmaxf_(a.level, 0.055f);
  zoomKick += input.accelZ * 2.8f * dt;
  zoomKick *= 1.0f - 4.0f * dt;
  zoomKick = clampf(zoomKick, -0.75f, 0.9f);
  shoveX += input.accelX * 34.0f * dt;
  shoveY += input.accelY * 34.0f * dt;
  shoveX *= 1.0f - 3.2f * dt;
  shoveY *= 1.0f - 3.2f * dt;
  float targetX = 31.5f + input.tiltX * 11.0f + shoveX;
  float targetY = 31.5f + input.tiltY * 11.0f + shoveY;
  centerX = lerpf(centerX, targetX, clampf(dt * 8.0f, 0, 1));
  centerY = lerpf(centerY, targetY, clampf(dt * 8.0f, 0, 1));
  float attackDrive = 1.0f + beatPulse * 0.72f + stabKick * 0.30f;
  motionPhase += dt * turnSign * (0.72f + a.mid * 1.65f + midKick * 2.7f) *
                 (0.68f + musicDrive * 0.56f) * attackDrive;
  twist += dt * (turnSign * (0.42f + a.high * 3.1f + highKick * 5.8f +
                             bassKick * 1.25f) * (0.78f + musicDrive * 0.28f) +
                 input.spin * 0.82f);
  float tunnelStep = dt * turnSign * (0.14f + a.bass * 0.92f +
                                      beatPulse * 0.68f + bassKick * 0.85f +
                                      zoomKick * 0.10f) * (0.74f + musicDrive * 0.34f);
  tunnelPhase = fmodf_(tunnelPhase + tunnelStep + 1.0f, 1.0f);
  hueBase = fmodf_(hueBase + dt * (34.0f + a.high * 165.0f + musicDrive * 28.0f) +
                   highKick * 175.0f * dt + input.spin * dt * 28.0f, 360.0f);

  clear((sceneVariant & 2) ? rgb(0, 2, 7) :
        (styleIndex == 10 ? rgb(2, 0, 9) : rgb(1, 0, 5)));
  drawStyle(a, dt);
  drawReactiveFx(a);

  float slam = fabsf_(input.accelZ);
  bool phraseMarker = beatPulse > 0.86f && (beatCount & 3) == 0;
  if (phraseMarker || slam > 0.45f) {
    Color flash = raveColor(180, beatPulse + slam * 0.45f, 0.7f);
    rect(0, 0, SCREEN_W, SCREEN_H, flash);
    if (slam > 0.8f) rect(2, 2, SCREEN_W - 4, SCREEN_H - 4, WHITE);
  }

  styleBanner = fmaxf_(0, styleBanner - dt);
  songBanner = fmaxf_(0, songBanner - dt);
  if (styleBanner > 0) {
    fillRect(0, 0, SCREEN_W, 7, BLACK);
    textCentered(1, STYLE_NAMES[styleIndex], WHITE);
  }
  if (songBanner > 0) {
    fillRect(0, SCREEN_H - 7, SCREEN_W, 7, BLACK);
    textCentered(SCREEN_H - 6, SONG_NAMES[songIndex], raveColor(55, 1));
  }
}

}  // namespace

PT_GAME_UNSCORED(rave, "RAVE", init, update)
