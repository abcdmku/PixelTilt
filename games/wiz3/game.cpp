// Wiz3 — the original 19-level Java game, adapted to PixelTilt's 64x64
// framebuffer and tilt / button controls. The level records, tile art, sprite
// sheets, and sound bank are generated from the original runtime JAR.
#include "pixeltilt/pixeltilt.h"

#include "assets.h"

using namespace pt;

namespace {

constexpr int MAP_W = 256;
constexpr int MAP_H = 16;
constexpr int TILE = 16;
constexpr int MAP_BYTES_PER_CELL = 9;
constexpr int MAX_ENTITIES = 128;
constexpr float WORLD_W = MAP_W * TILE;
constexpr float VIEW_W = SCREEN_W * 4.0f;
constexpr float PLAYER_W = 16.0f;
constexpr float PLAYER_H = 24.0f;
constexpr float ORIGINAL_FPS = 1000.0f / 30.0f;

// Collision rectangles from the original Bob subclasses. The generated
// sprite sheets are rounded to the 4-pixel LED scale, but collision stays in
// the Java game's native 480x256 world units.
const uint8_t SPRITE_WORLD_W[23] = {
  16, 16, 16, 32, 16, 16, 32, 32, 16, 32, 16, 16,
  32, 16, 16, 16, 16, 32, 16, 16, 64, 16, 64,
};
const uint8_t SPRITE_WORLD_H[23] = {
  24, 21, 44, 42, 7, 16, 12, 9, 16, 16, 16, 24,
  32, 40, 64, 16, 16, 16, 24, 28, 64, 24, 32,
};

struct Cell {
  uint8_t back = 0;
  uint8_t fore = 0;
  uint8_t sprite = 0;
  uint8_t bonus = 0;
  uint8_t block = 0;
  int32_t flag = 0;
};

struct Entity {
  bool alive = false;
  bool transient = false;
  bool hazard = false;
  uint8_t type = 0;
  int frame = 0;
  float x = 0;
  float y = 0;
  float homeX = 0;
  float homeY = 0;
  float vx = 0;
  float vy = 0;
  float t = 0;
  float ttl = 0;
  float soundTimer = 0;
  float lastDx = 0;
  float lastDy = 0;
  float sim = 0;
  float timer = 0;
  int sourceFlag = 0;
  int counter = 0;
  int dirX = 1;
  int dirY = 1;
  bool platformOn = false;
};

Cell level[MAP_W * MAP_H];
Entity entities[MAX_ENTITIES];

int currentLevel = 0;
int lives = 3;
int score = 0;
int scoreRank = -1;
int potionGauge = 124;
int powerupStage = 0;
int maxJumps = 1;
int jumps = 0;
float highJump = -540.0f;
bool hasKey = false;
bool invisibleSpell = false;
bool asbestos = false;
bool restoreOnDeath = false;
float invincible = 0;

float px = 0;
float py = 0;
float vx = 0;
float vy = 0;
float cameraX = 0;
float playerLeftBound = 16;
float playerRightBound = WORLD_W - PLAYER_W;
float walkTimer = 0;
float deathTimer = 0;
float levelTimer = 0;
float gameOverTimer = 0;
bool grounded = false;
bool dead = false;
bool gameOver = false;
bool levelClear = false;
bool won = false;
bool restartLevel = false;
bool facingRight = true;
int checkpointFlag = 1;
int checkpointSearchX = 0;
int ridingPlatform = -1;
bool switchFlag = false;
float bossStartX = 0;
float bossStartY = 0;
float boss2StartX = 0;
float boss2StartY = 0;
float boss2EndX = 0;
float boss2EndY = 0;

int floorTile(float v) { return floori(v); }

void formatNumber(char* buf, int value) {
  char reverse[16];
  int n = 0;
  int r = 0;
  if (value == 0) reverse[r++] = '0';
  if (value < 0) {
    reverse[r++] = '-';
    value = -value;
  }
  while (value > 0 && r < (int)sizeof(reverse)) {
    reverse[r++] = (char)('0' + value % 10);
    value /= 10;
  }
  while (r > 0) buf[n++] = reverse[--r];
  buf[n] = 0;
}

void drawNumber(int x, int y, int value, Color color) {
  char buf[16];
  formatNumber(buf, value);
  text(x, y, buf, color);
}

// --- Layer tone -------------------------------------------------------------
// At 480x256 the original separated its backdrop, platforms and characters with
// sheer detail. A 16x16-tile viewport has no detail to spare, so the layers are
// separated by value instead: the backdrop sits back and cool, the platforms
// read at full strength, and anything that moves is the brightest thing on the
// panel. The hues are the original palette throughout.
constexpr float BACK_DIM = 0.92f;
constexpr float BACK_SAT = 0.86f;
constexpr float FORE_SAT = 1.14f;
constexpr float SPRITE_SAT = 1.30f;
constexpr float SPRITE_LIFT = 0.55f;
constexpr float RIM_DIM = 0.50f;

uint8_t chan(float v) { return (uint8_t)clampi((int)(v + 0.5f), 0, 255); }

Color scaleColor(Color c, float k) { return rgb(chan(c.r * k), chan(c.g * k), chan(c.b * k)); }

float luma(Color c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }

Color saturateColor(Color c, float k) {
  float l = luma(c);
  return rgb(chan(l + (c.r - l) * k), chan(l + (c.g - l) * k), chan(l + (c.b - l) * k));
}

// Lift the shadows without blowing the highlights, so a navy robe stays navy
// but stops reading as the same black as its own outline. This is the shape of
// a gamma curve; the core is freestanding, so no powf to reach for.
uint8_t liftChannel(uint8_t v) { return chan(v * (1.0f + SPRITE_LIFT * (1.0f - v / 255.0f))); }

Color liftColor(Color c) { return rgb(liftChannel(c.r), liftChannel(c.g), liftChannel(c.b)); }

// Distance haze. The backdrop darkens towards the top of the panel and lifts
// towards the horizon, which gives the flat sky tiles some depth for free.
float backRamp(int sy) { return 0.78f + 0.32f * (float)clampi(sy, 0, SCREEN_H - 1) / (SCREEN_H - 1); }

Color rawFrom565(uint16_t value) {
  uint8_t r = (uint8_t)((((value >> 11) & 31) * 255) / 31);
  uint8_t g = (uint8_t)((((value >> 5) & 63) * 255) / 63);
  uint8_t b = (uint8_t)(((value & 31) * 255) / 31);
  return rgb(r, g, b);
}

Color backColor(uint16_t value, int sy) {
  if (value == 1) return rgb(2, 3, 8);  // generated assets reserve 1 for visible black
  return scaleColor(saturateColor(rawFrom565(value), BACK_SAT), BACK_DIM * backRamp(sy));
}

Color foreColor(uint16_t value) {
  if (value == 1) return rgb(6, 5, 10);
  return saturateColor(rawFrom565(value), FORE_SAT);
}

// Characters, pickups and projectiles. The lift keeps the wizard's near-black
// robe shading from collapsing into his own outline at this size.
Color spriteColor(uint16_t value) {
  if (value == 1) return rgb(4, 4, 8);
  return liftColor(saturateColor(rawFrom565(value), SPRITE_SAT));
}

// --- Silhouettes ------------------------------------------------------------
// Everything that is not scenery gets a one-pixel rim of darkened backdrop
// before anything is filled in. A 4x6 wizard standing on a brown platform is
// otherwise just a few brown-ish pixels; the rim guarantees a readable
// silhouette against any tile without repainting a pixel of the original art.
enum SpritePass { PASS_RIM, PASS_FILL };

uint8_t rimMask[SCREEN_W * SCREEN_H / 8];

void clearRim() {
  for (unsigned i = 0; i < sizeof(rimMask); i++) rimMask[i] = 0;
}

void rimPixel(int x, int y) {
  if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
  int bit = y * SCREEN_W + x;
  uint8_t mask = (uint8_t)(1 << (bit & 7));
  if (rimMask[bit >> 3] & mask) return;  // darken each pixel at most once
  rimMask[bit >> 3] |= mask;
  pixel(x, y, scaleColor(getPixel(x, y), RIM_DIM));
}

void rimNeighbours(int x, int y) {
  rimPixel(x - 1, y);
  rimPixel(x + 1, y);
  rimPixel(x, y - 1);
  rimPixel(x, y + 1);
}

void drawTile(uint8_t tile, int sx, int sy, bool foreground) {
  const uint16_t* pixels = foreground ? wiz3_assets::FORE_TILES : wiz3_assets::BACK_TILES;
  int base = (int)tile * 16;
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      uint16_t value = pixels[base + y * 4 + x];
      if (!value) continue;
      pixel(sx + x, sy + y, foreground ? foreColor(value) : backColor(value, sy + y));
    }
  }
}

void drawBonus(uint8_t bonus, int sx, int sy, SpritePass pass) {
  if (bonus == 0 || bonus >= 16) return;
  int base = (int)bonus * 16;
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      uint16_t value = wiz3_assets::BONUS_ICONS[base + y * 4 + x];
      if (!value) continue;
      if (pass == PASS_RIM) {
        rimNeighbours(sx + x, sy + y);
      } else {
        pixel(sx + x, sy + y, spriteColor(value));
      }
    }
  }
}

int spriteWidth(uint8_t type) {
  return type < 23 ? SPRITE_WORLD_W[type] : 16;
}

int spriteHeight(uint8_t type) {
  return type < 23 ? SPRITE_WORLD_H[type] : 16;
}

Entity* newEntity() {
  for (int i = 0; i < MAX_ENTITIES; i++) {
    if (!entities[i].alive) {
      entities[i] = Entity{};
      entities[i].alive = true;
      return &entities[i];
    }
  }
  return nullptr;
}

int touchType(uint8_t type, int sourceFlag) {
  switch (type) {
    case 1: case 2: case 3: case 4: case 11: case 15: case 18: case 19:
    case 20: case 21: return sourceFlag > 0 && type == 20 ? 0 : 1;
    case 22: return 0;  // BobBoss2 inherits the original friendly/default touch.
    case 9: case 13: case 14: return 2;
    case 6: case 7: case 17: return 4;
    default: return 0;
  }
}

Entity* addEntityAt(uint8_t type, float x, float yInput, int sourceFlag) {
  if (type == 0 || type >= 23) return nullptr;
  Entity* e = newEntity();
  if (!e) return nullptr;
  e->type = type;
  e->sourceFlag = sourceFlag;
  e->hazard = touchType(type, sourceFlag) != 0 && touchType(type, sourceFlag) != 4;
  e->x = x;
  e->y = yInput - spriteHeight(type);
  if (type == 20) e->y += 16;
  if (type == 15) e->y = yInput - 256;
  if (type == 18) e->y = 320;
  if (type == 16) {
    e->y = -16;
    e->hazard = false;
  }
  e->homeX = e->x;
  e->homeY = e->y;
  e->vx = -33.333f;
  if (type == 4) e->vx = sourceFlag ? 133.333f : -133.333f;
  if (type == 3) e->vy = -16;
  if (type == 13) {
    e->vx = -2;
    e->vy = (float)sourceFlag;
  }
  if (type == 18) e->vy = -16;
  if (type == 19) e->sim = 48;
  if (type == 6 || type == 7 || type == 17) {
    e->timer = absi(sourceFlag) / 2.0f;
    e->sourceFlag = absi(sourceFlag) / 2;
    e->dirX = sourceFlag < 0 ? -1 : 1;
    if (type == 6) e->vy = (float)(sourceFlag < 0 ? -2 : 2);
    if (type == 7) {
      e->vx = (float)(sourceFlag < 0 ? -2 : 2);
      e->dirX = e->vx < 0 ? -1 : 1;
    }
    if (type == 17) e->vx = 0;
  }
  if (type == 11) e->vx = -33.333f;
  if (type == 20) {
    e->vx = 8;
    e->vy = 16;
  }
  if (type == 22) {
    e->vx = -24;
    e->vy = 0;
    e->dirX = -1;
    e->dirY = 1;
  }
  if (type == 20) {
    if (sourceFlag == 0) {
      bossStartX = e->x;
      bossStartY = e->y;
    } else {
      e->hazard = false;
    }
  }
  if (type == 22 && sourceFlag == 0) {
    boss2StartX = e->x;
    boss2StartY = e->y;
  }
  e->frame = 0;
  return e;
}

void addMapEntity(uint8_t type, int tx, int ty, int flag) {
  Entity* e = addEntityAt(type, (float)(tx * TILE), (float)((ty + 1) * TILE), flag);
  if (!e) return;
  if (type == 20 && flag < 8) {
    for (int n = 1; n <= 8; n++) addEntityAt(type, (float)(tx * TILE), (float)((ty + 1) * TILE), n);
  } else if (type == 22 && flag < 8) {
    for (int n = 1; n <= 8; n++) {
      addEntityAt(type, (float)(tx * TILE), (float)((ty + 1) * TILE - n * TILE), n);
    }
    Entity* tail = &entities[0];
    for (int i = 0; i < MAX_ENTITIES; i++) {
      if (entities[i].alive && entities[i].type == 22 && entities[i].sourceFlag == 8) tail = &entities[i];
    }
    boss2EndX = tail->x;
    boss2EndY = tail->y;
  }
}

void addEffect(uint8_t type, float x, float y) {
  Entity* e = newEntity();
  if (!e || type >= 23) return;
  e->transient = true;
  e->hazard = false;
  e->type = type;
  e->x = x - (spriteWidth(type) - TILE) * 0.5f;
  e->y = y - spriteHeight(type) + TILE;
  e->homeX = e->x;
  e->homeY = e->y;
  e->ttl = 0.7f;
}

Cell& cellAt(int tx, int ty) { return level[ty * MAP_W + tx]; }

// The original uses three different masks: bit 1 for walls/ceilings, bits
// 1|2 for landing, and bit 4 only as a door/switch marker. In particular, a
// block with value 2 is a one-way platform: the wizard can pass upward and
// through its sides, but lands on it while falling.
bool blockAt(int tx, int ty, uint8_t mask) {
  if (tx < 0 || tx >= MAP_W) return true;
  if (ty < 0) return false;
  if (ty >= MAP_H) return false;
  return (cellAt(tx, ty).block & mask) != 0;
}

bool sideBlockAt(int tx, int ty) { return blockAt(tx, ty, 1); }
bool landingBlockAt(int tx, int ty) { return blockAt(tx, ty, 3); }

bool overlapRect(float ax, float ay, float aw, float ah,
                 float bx, float by, float bw, float bh) {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

void openDoorArea(int xs, int ys) {
  if (xs < 0 || xs >= MAP_W || ys < 0 || ys >= MAP_H || cellAt(xs, ys).bonus != 7) return;
  for (int x = maxi(xs - 1, 0); x < mini(xs + 2, MAP_W); x++) {
    for (int y = maxi(ys - 1, 0); y < mini(ys + 2, MAP_H); y++) {
      if (cellAt(x, y).bonus == 7) {
        cellAt(x, y).flag &= ~4;
        cellAt(x, y).fore = 0;
      }
    }
  }
}

bool findStart(int checkpoint, int searchX, int& outX, int& outY) {
  for (int i = 0; i < MAP_W; i++) {
    int x = (searchX + i) & 0xFF;
    for (int y = MAP_H - 1; y >= 0; y--) {
      const Cell& c = cellAt(x, y);
      if ((c.flag & ~4) == checkpoint && c.sprite == 0) {
        outX = x;
        outY = y;
        return true;
      }
    }
  }
  return false;
}

void computeBounds(int startX, int& left, int& right) {
  left = 0;
  right = (MAP_W - 1) * TILE;
  for (int x = startX; x > 0; x--) {
    bool marker = false;
    for (int y = MAP_H - 1; y >= 0; y--) marker |= cellAt(x, y).flag == 30;
    if (marker) {
      left = x * TILE - TILE;
      break;
    }
  }
  for (int x = startX; x < MAP_W; x++) {
    bool marker = false;
    for (int y = 0; y < MAP_H; y++) marker |= cellAt(x, y).flag == 30;
    if (marker) {
      // This is LandScape.getRightBound() translated back into the player's
      // native limit: rightBound + the original 480px viewport = marker-16.
      right = x * TILE - TILE;
      break;
    }
  }
  if (right < left + TILE) right = (MAP_W - 1) * TILE;
}

void loadLevel(int number) {
  currentLevel = clampi(number, 0, wiz3_assets::LEVEL_COUNT - 1);
  const uint8_t* raw = wiz3_assets::LEVEL_DATA[currentLevel];
  for (int i = 0; i < MAP_W * MAP_H; i++) {
    int o = i * MAP_BYTES_PER_CELL;
    uint32_t flag = ((uint32_t)raw[o + 2] << 24) |
                    ((uint32_t)raw[o + 3] << 16) |
                    ((uint32_t)raw[o + 4] << 8) |
                    (uint32_t)raw[o + 5];
    level[i].back = raw[o];
    level[i].fore = raw[o + 1];
    level[i].flag = (int32_t)flag;
    level[i].sprite = raw[o + 6];
    level[i].bonus = raw[o + 7];
    level[i].block = raw[o + 8];
  }

  for (int i = 0; i < MAX_ENTITIES; i++) entities[i] = Entity{};

  for (int y = 0; y < MAP_H; y++) {
    for (int x = 0; x < MAP_W; x++) {
      Cell& c = cellAt(x, y);
      if (c.sprite) addMapEntity(c.sprite, x, y, c.flag);
    }
  }

  int startX = 2;
  int startY = 14;
  if (!findStart(checkpointFlag, checkpointSearchX, startX, startY)) {
    findStart(1, 0, startX, startY);
  }
  openDoorArea(startX, startY);
  int leftBound = 0;
  int rightBound = (MAP_W - 1) * TILE;
  computeBounds(startX, leftBound, rightBound);

  px = (float)(startX * TILE);
  py = fmaxf_(0.0f, (float)(startY * TILE) - 8.0f);
  vx = 0;
  vy = 0;
  grounded = false;
  jumps = 0;
  dead = false;
  levelClear = false;
  levelTimer = 0;
  playerLeftBound = (float)leftBound + TILE;
  playerRightBound = (float)rightBound;
  px = clampf(px, playerLeftBound, playerRightBound);
  cameraX = clampf(px - VIEW_W * 0.5f, (float)leftBound,
                  fmaxf_((float)leftBound, (float)rightBound - VIEW_W));
  ridingPlatform = -1;
  switchFlag = false;
}

void unlockPowerup() {
  powerupStage++;
  if (powerupStage > 6) {
    powerupStage = 6;
    lives++;
    sfxSample(wiz3_assets::SAMPLE_EXTRA, sizeof(wiz3_assets::SAMPLE_EXTRA), 0.8f);
    return;
  }
  if (powerupStage >= 1) highJump = -600.0f;
  if (powerupStage >= 2) invisibleSpell = true;
  if (powerupStage >= 3) maxJumps = 2;
  if (powerupStage >= 4) asbestos = true;
  if (powerupStage == 5) {
    Entity* shot = addEntityAt(16, px + 8, py + PLAYER_H, 0);
    if (shot) {
      shot->x = px + 8;
      shot->y = -16;
    }
  }
  if (powerupStage == 6) restoreOnDeath = true;
  sfxSample(wiz3_assets::SAMPLE_POWERUP, sizeof(wiz3_assets::SAMPLE_POWERUP));
}

void resetPowerupsAfterDeath() {
  powerupStage = restoreOnDeath ? 5 : 0;
  maxJumps = restoreOnDeath ? 2 : 1;
  highJump = restoreOnDeath ? -600.0f : -540.0f;
  invisibleSpell = restoreOnDeath;
  asbestos = restoreOnDeath;
  restoreOnDeath = false;
}

void beginDeath(bool hit) {
  if (dead) return;
  dead = true;
  deathTimer = 0;
  lives--;
  hasKey = false;
  resetPowerupsAfterDeath();
  if (hit) sfxSample(wiz3_assets::SAMPLE_HIT, sizeof(wiz3_assets::SAMPLE_HIT));
  else sfxSample(wiz3_assets::SAMPLE_DIE, sizeof(wiz3_assets::SAMPLE_DIE));
  if (lives <= 0) sfxSample(wiz3_assets::SAMPLE_DIE, sizeof(wiz3_assets::SAMPLE_DIE));
}

void collectBonus(Cell& c, int tx, int ty) {
  uint8_t bonus = c.bonus;
  if (bonus == 0) return;
  c.bonus = 0;
  c.fore = 0;
  switch (bonus) {
    case 1:
      score += 1000;
      addEffect(5, tx * TILE + 8.0f, ty * TILE + 8.0f);
      sfxSample(wiz3_assets::SAMPLE_STAR, sizeof(wiz3_assets::SAMPLE_STAR));
      break;
    case 2:
      score += 100;
      potionGauge--;
      addEffect(8, tx * TILE + 8.0f, ty * TILE + 8.0f);
      sfxSample(wiz3_assets::SAMPLE_BOTTLE, sizeof(wiz3_assets::SAMPLE_BOTTLE));
      if (potionGauge <= 24) {
        potionGauge = 124;
        unlockPowerup();
      }
      break;
    case 3:
      lives++;
      addEffect(10, tx * TILE + 8.0f, ty * TILE + 8.0f);
      sfxSample(wiz3_assets::SAMPLE_EXTRA, sizeof(wiz3_assets::SAMPLE_EXTRA));
      break;
    case 4:
      score += 1000;
      hasKey = true;
      addEffect(10, tx * TILE + 8.0f, ty * TILE + 8.0f);
      sfxSample(wiz3_assets::SAMPLE_KEY, sizeof(wiz3_assets::SAMPLE_KEY));
      break;
    case 5:
      checkpointFlag = 1;
      checkpointSearchX = tx;
      for (int i = 0; i < MAP_W * MAP_H; i++) {
        if (level[i].flag == 1 && level[i].sprite == 0) level[i].flag = 0;
      }
      c.flag = checkpointFlag;
      sfxSample(wiz3_assets::SAMPLE_EXTRA, sizeof(wiz3_assets::SAMPLE_EXTRA), 0.75f);
      break;
    case 6:
      invincible = 1600.0f / 60.0f;
      addEffect(3, tx * TILE + 8.0f, ty * TILE + 8.0f);
      sfxSample(wiz3_assets::SAMPLE_POWERUP, sizeof(wiz3_assets::SAMPLE_POWERUP), 0.85f);
      break;
    default:
      break;
  }
}

void finishLevel() {
  if (levelClear || won) return;
  levelClear = true;
  levelTimer = 0;
  sfxSample(wiz3_assets::SAMPLE_DOOR, sizeof(wiz3_assets::SAMPLE_DOOR));
}

void handleInteraction() {
  int tx = clampi((int)((px + 8) / TILE), 0, MAP_W - 1);
  int ty = clampi((int)((py + 7) / TILE), 0, MAP_H - 1);
  Cell& c = cellAt(tx, ty);
  if (c.bonus == 7) {
    if ((c.flag & 4) && !hasKey) {
      sfxSample(wiz3_assets::SAMPLE_NOKEY, sizeof(wiz3_assets::SAMPLE_NOKEY));
    } else {
      if (c.flag & 4) hasKey = false;
      openDoorArea(tx, ty);
      checkpointFlag = c.flag;
      checkpointSearchX = tx + 2;
      restartLevel = true;
      sfxSample(wiz3_assets::SAMPLE_DOOR, sizeof(wiz3_assets::SAMPLE_DOOR));
    }
  } else if (c.bonus == 8 && !switchFlag) {
    switchFlag = true;
    for (int i = 0; i < MAP_W * MAP_H; i++) {
      if (level[i].flag == 16 || level[i].bonus == 8) {
        if (level[i].fore != 0) level[i].fore ^= 1;
        if (level[i].block != 0) level[i].block ^= 5;
      }
    }
    sfxSample(wiz3_assets::SAMPLE_LEVER, sizeof(wiz3_assets::SAMPLE_LEVER));
  }
}

void spawnArrow(float x, float y, int direction) {
  Entity* arrow = addEntityAt(4, x, y, direction > 0 ? 1 : 0);
  if (arrow) arrow->frame = direction > 0 ? 4 : 0;
}

void updateEntities(float dt) {
  const float frames = dt * ORIGINAL_FPS;
  for (int i = 0; i < MAX_ENTITIES; i++) {
    Entity& e = entities[i];
    if (!e.alive) continue;
    const float oldX = e.x;
    const float oldY = e.y;
    e.lastDx = 0;
    e.lastDy = 0;
    e.t += dt;
    if (e.type != 17) e.sim += frames;
    e.soundTimer -= dt;

    if (e.transient) {
      e.ttl -= dt;
      if (e.ttl <= 0) e.alive = false;
      else if (e.type < 23 && wiz3_assets::SPRITES[e.type].frames > 0)
        e.frame = ((int)e.sim) % wiz3_assets::SPRITES[e.type].frames;
      continue;
    }

    switch (e.type) {
      case 1: {  // BobGuard: gravity plus a one-pixel patrol with wall turns.
        const int dir = e.vx < 0 ? -1 : 1;
        const int foot = floorTile((e.y + spriteHeight(e.type)) / TILE);
        const int probe = floorTile((e.x + 8 - dir * 4) / TILE);
        if (!landingBlockAt(probe, foot)) {
          e.vy = fminf_(e.vy + frames, 16.0f);
          e.y += e.vy * 0.25f * frames;
        } else {
          e.y = floorTile((e.y + spriteHeight(e.type)) / TILE) * TILE - spriteHeight(e.type);
          e.vy = 0;
          e.x += dir * frames;
        }
        if (sideBlockAt(floorTile((e.x + 8 + dir * 8) / TILE),
                        floorTile((e.y + spriteHeight(e.type) - 1) / TILE))) {
          e.vx = -e.vx;
        }
        e.frame = (((int)e.sim & 4) >> 2) | (dir < 0 ? 2 : 0);
        break;
      }
      case 2:  // BobKnight is stationary; only its two-frame animation runs.
        e.frame = (((int)e.sim) & 24) / 24;
        break;
      case 3: {  // BobDragon: stationary flying enemy with timed hops.
        if (landingBlockAt(floorTile(e.x / TILE),
                           floorTile((e.y + spriteHeight(e.type)) / TILE))) e.vy = 0;
        int now = (int)e.sim;
        if (now != e.counter) {
          if (now > 0 && now % 50 == 0) e.vy = -19;
          if (now >= 200) {
            e.sim = 0;
            e.counter = 0;
            e.vy = -25;
          } else {
            e.counter = now;
          }
        }
        if (e.vy != 0) e.vy += 2.0f * frames;
        e.y += e.vy * 0.5f * frames;
        e.frame = ((int)e.sim & 12) >> 2;
        break;
      }
      case 4: {  // BobArrow: horizontal until a wall, then harmless falling.
        const int dir = e.vx < 0 ? -1 : 1;
        if (!e.platformOn && sideBlockAt(floorTile((e.x + 8 + dir * 4) / TILE),
                                          floorTile(e.y / TILE))) {
          e.platformOn = true;
          e.hazard = false;
          e.vy = -4 * ORIGINAL_FPS;
        }
        if (!e.platformOn) e.x += e.vx * dt;
        else {
          e.x -= e.vx * dt * 0.5f;
          e.y += e.vy * dt;
          e.vy += ORIGINAL_FPS * dt;
        }
        e.frame = ((int)e.sim & 3) | (dir > 0 ? 4 : 0);
        break;
      }
      case 5:
        e.frame = ((int)e.sim) & 7;
        break;
      case 6: {  // BobVPlatform.
        e.timer -= frames;
        if (e.timer < 0) {
          e.vy = -e.vy;
          e.timer = (float)e.sourceFlag;
        }
        if ((int)e.y == 256) {
          e.y = -16;
          e.timer = (float)e.sourceFlag;
        }
        e.y += e.vy * frames;
        e.frame = 0;
        break;
      }
      case 7: {  // BobHPlatform: acceleration is capped at eight native units.
        e.timer -= frames;
        if (e.timer < 0 ||
            sideBlockAt(floorTile((e.x - 16) / TILE), floorTile(e.y / TILE)) ||
            sideBlockAt(floorTile((e.x + 32 + 16) / TILE), floorTile(e.y / TILE))) {
          e.dirX = -e.dirX;
          e.timer = (float)e.sourceFlag;
        }
        if ((e.vx > -8 && e.dirX < 0) || (e.vx < 8 && e.dirX > 0)) e.vx += e.dirX * frames;
        e.x += e.vx * 0.25f * frames;
        e.frame = 0;
        break;
      }
      case 8:
        e.frame = ((int)e.sim) & 7;
        break;
      case 9:
        e.frame = ((int)e.sim) & 3;
        break;
      case 10:
        e.frame = ((int)e.sim) & 7;
        if (e.sourceFlag > 0) e.y -= frames;
        break;
      case 11: {  // BobSentry: patrols until its next step would leave ground.
        int dir = e.vx < 0 ? -1 : 1;
        int probe = floorTile((e.x + 8 + dir * 8) / TILE);
        int foot = floorTile((e.y + spriteHeight(e.type)) / TILE);
        if (!landingBlockAt(probe, foot) ||
            sideBlockAt(probe, floorTile((e.y + spriteHeight(e.type) - 1) / TILE))) {
          dir = -dir;
          e.vx = dir * 33.333f;
        }
        e.x += dir * frames;
        e.frame = (((int)e.sim & 4) >> 2) | (dir < 0 ? 2 : 0);
        break;
      }
      case 12:
        e.frame = ((int)e.sim / 2) & 3;
        break;
      case 13: {  // BobFireball: gravity, floor bounce, and wall reversal.
        int foot = floorTile((e.y + spriteHeight(e.type)) / TILE);
        if (!landingBlockAt(floorTile((e.x + 8) / TILE), foot)) {
          if (e.vy < 14) e.vy += 2.0f * frames;
          e.y += e.vy * 0.25f * frames;
        } else {
          e.y = floorTile((e.y + spriteHeight(e.type)) / TILE) * TILE - spriteHeight(e.type);
          e.vy = 1;
        }
        if (sideBlockAt(floorTile((e.x + 8 + e.vx * 4) / TILE),
                        floorTile((e.y + spriteHeight(e.type) - e.vy) / TILE))) e.vx = -e.vx;
        e.x += e.vx * frames;
        e.frame = ((int)e.sim) & 3;
        break;
      }
      case 14:
        e.frame = (((int)e.sim & 0x20) / 16) + ((int)e.sim & 1);
        break;
      case 15: {  // BobBoulder: a rolling hazard that drops from above.
        int foot = floorTile((e.y + spriteHeight(e.type)) / TILE);
        if (landingBlockAt(floorTile(e.x / TILE), foot) &&
            !landingBlockAt(floorTile(e.x / TILE), foot - 1) && e.y > e.counter) {
          e.y = floorTile(e.y / TILE) * TILE;
          e.counter = (int)e.y + TILE;
          e.vy = -16;
        }
        if (e.vy < 16) e.vy += 2.0f * frames;
        e.y += e.vy * 0.25f * frames;
        if (e.y > 256) {
          e.y = -64;
          e.counter = 0;
        }
        e.frame = ((int)e.sim & 6) >> 1;
        break;
      }
      case 16: {  // BobShot: the fifth powerup's helper hunts fatal bobs.
        int target = -1;
        float best = 1e9f;
        for (int j = 0; j < MAX_ENTITIES; j++) {
          if (j == i || !entities[j].alive || !entities[j].hazard) continue;
          float dx = entities[j].x - e.x;
          float dy = entities[j].y - e.y;
          float d = dx * dx + dy * dy;
          if (d < best) {
            best = d;
            target = j;
          }
        }
        float targetX = target >= 0 ? entities[target].x : px;
        float targetY = target >= 0 ? entities[target].y : py;
        if (e.x < targetX && e.vx < 16) e.vx += frames;
        else if (e.vx > -16) e.vx -= frames;
        if (e.y < targetY - 4 && e.vy < 12) e.vy += frames;
        else if (e.vy > -12) e.vy -= frames;
        e.x += e.vx * 0.25f * frames;
        e.y += e.vy * 0.25f * frames;
        if (target >= 0 && overlapRect(e.x, e.y, 16, 16,
                                       entities[target].x, entities[target].y,
                                       spriteWidth(entities[target].type),
                                       spriteHeight(entities[target].type))) {
          entities[target].alive = false;
          entities[target].hazard = false;
          score += 500;
          sfxSample(wiz3_assets::SAMPLE_DIE, sizeof(wiz3_assets::SAMPLE_DIE), 0.7f);
          e.x = px + 8;
          e.y = -16;
        }
        e.frame = ((int)e.sim) & 3;
        break;
      }
      case 17:  // BobFPlatform moves only after the wizard lands on it.
        if (e.platformOn) {
          e.sim += frames;
          e.lastDy = (e.sim / 8.0f) * frames;
          e.y += e.lastDy;
          if ((int)e.sim == 8) sfxSample(wiz3_assets::SAMPLE_JUMP, sizeof(wiz3_assets::SAMPLE_JUMP), 0.7f);
        }
        e.frame = 0;
        break;
      case 18:  // BobFish leaps vertically from below the waterline.
        e.y += e.vy * 0.5f * frames;
        e.vy += frames;
        if (e.y > 320) e.vy = -28;
        e.frame = (e.vy > 0 ? 2 : 0) + ((int)e.vy & 1);
        break;
      case 19: {  // BobArcher fires a directed arrow every 64 native frames.
        if (e.sim >= 56 && e.sim < 64) e.frame = px > e.x ? 3 : 1;
        if (e.sim >= 64) {
          e.frame &= ~1;
          spawnArrow(e.x + ((e.frame & 2) ? 8 : -4), e.y + 12, (e.frame & 2) ? 1 : -1);
          e.sim = 0;
        }
        break;
      }
      case 20: {  // BobBoss's head moves toward its original anchor and drops fireballs.
        if (e.sourceFlag == 0) {
          e.timer += frames;
          if (e.timer >= 8) {
            e.dirX = e.x > e.homeX ? -1 : 1;
            e.dirY = e.y > e.homeY - 48 ? -1 : 1;
            if (e.y > 224 && e.timer > 14) {
              addEntityAt(13, e.x, e.y + 16, -32);
              e.timer = 8;
            }
            e.vx += e.dirX;
            e.vy += e.dirY;
            e.x += e.vx * 0.25f * frames;
            e.y += e.vy * 0.25f * frames;
            bossStartX = e.x;
            bossStartY = e.y;
          }
        } else {
          e.x = bossStartX + (e.homeX - bossStartX) * e.sourceFlag / 8.0f;
          e.y = bossStartY + (e.homeY - bossStartY) * e.sourceFlag / 8.0f;
        }
        e.frame = e.sourceFlag > 0 ? 1 : 0;
        break;
      }
      case 21: {  // BobGhost homes in on the wizard without respecting blocks.
        if (e.y < py && e.vy < 16) {
          e.vy += frames;
          if (e.x < px && e.vx < 16) e.vx += frames;
          else if (e.vx > -16) e.vx -= frames;
        } else if (e.vy > -16) {
          e.vy -= frames;
          e.vx = e.vx < 0 ? -8 : 8;
        }
        e.x += e.vx * 0.125f * frames;
        e.y += e.vy * 0.125f * frames;
        e.frame = e.vx < 0 ? ((int)e.sim / 4) & 1 : 2 + (((int)e.sim / 4) & 1);
        break;
      }
      case 22: {  // BobBoss2's head oscillates; its eight trailing segments lerp.
        if (e.sourceFlag == 0) {
          e.timer += frames;
          if (e.timer >= 8) {
            if (e.vx == 24) e.dirX = -1;
            if (e.vx == -24) e.dirX = 1;
            if (e.vy == 12) e.dirY = -1;
            if (e.vy == -12) e.dirY = 1;
            e.vx += e.dirX;
            e.vy += e.dirY;
            e.x += e.vx * 0.25f * frames;
            e.y += e.vy * 0.25f * frames;
            boss2StartX = e.x;
            boss2StartY = e.y;
          }
        } else {
          e.x = boss2StartX + (boss2EndX - boss2StartX) * e.sourceFlag / 8.0f;
          e.y = boss2StartY + (boss2EndY - boss2StartY) * e.sourceFlag / 8.0f;
        }
        e.frame = e.sourceFlag > 0 ? 1 : 0;
        break;
      }
      default:
        break;
    }

    e.lastDx = e.x - oldX;
    e.lastDy = e.y - oldY;
    if ((e.type == 9 || e.type == 13) && e.soundTimer <= 0 &&
        e.x > cameraX - 32 && e.x < cameraX + VIEW_W + 32) {
      e.soundTimer = 1.5f;
      sfxSample(wiz3_assets::SAMPLE_FIRE, sizeof(wiz3_assets::SAMPLE_FIRE), 0.35f);
    }
  }
}

void movePlayer(float dt) {
  if (ridingPlatform >= 0 && ridingPlatform < MAX_ENTITIES) {
    Entity& platform = entities[ridingPlatform];
    if (platform.alive && (platform.type == 6 || platform.type == 7 || platform.type == 17) &&
        px + PLAYER_W > platform.x && px < platform.x + spriteWidth(platform.type)) {
      px += platform.lastDx;
      py += platform.lastDy;
    } else {
      ridingPlatform = -1;
    }
  }

  float axis = tiltCurve(input.tiltX, 0.05f);
  vx = axis * 145.0f;
  if (fabsf_(axis) > 0.02f) {
    facingRight = axis > 0;
    walkTimer -= dt;
    if (grounded && walkTimer <= 0) {
      walkTimer = 0.18f;
      sfxSample(wiz3_assets::SAMPLE_WALK, sizeof(wiz3_assets::SAMPLE_WALK), 0.35f);
    }
  } else {
    walkTimer = 0;
  }

  if (input.justDown(BTN_CLICK) && (grounded || jumps < maxJumps)) {
    vy = highJump;
    grounded = false;
    jumps++;
    sfxSample(wiz3_assets::SAMPLE_JUMP, sizeof(wiz3_assets::SAMPLE_JUMP));
  }

  float dx = vx * dt;
  float nextX = px + dx;
  if (dx > 0) {
    int tr = floorTile((nextX + 11) / TILE);
    int tt = maxi(floorTile(py / TILE), 0);
    int tm = maxi(floorTile((py + 16) / TILE), 0);
    int tb = maxi(floorTile((py + 23) / TILE), 0);
    if (sideBlockAt(tr, tt) || sideBlockAt(tr, tm) || sideBlockAt(tr, tb)) {
      nextX = (float)(tr * TILE + 4);
      vx = 3.0f * ORIGINAL_FPS;
    }
  } else if (dx < 0) {
    int tl = floorTile((nextX + 4) / TILE);
    int tt = maxi(floorTile(py / TILE), 0);
    int tm = maxi(floorTile((py + 16) / TILE), 0);
    int tb = maxi(floorTile((py + 23) / TILE), 0);
    if (sideBlockAt(tl, tt) || sideBlockAt(tl, tm) || sideBlockAt(tl, tb)) {
      nextX = (float)(tl * TILE + 12);
      vx = -3.0f * ORIGINAL_FPS;
    }
  }
  px = clampf(nextX, playerLeftBound, playerRightBound);

  vy += 1800.0f * dt;
  vy = fminf_(vy, 780.0f);
  const float oldY = py;
  const int oldBottom = floorTile((py + 23) / TILE);
  float dy = vy * dt;
  grounded = false;
  if (dy < 0) {
    int ty = maxi(floorTile((py + dy) / TILE), 0);
    int tl = floorTile((px + 4) / TILE);
    int tr = floorTile((px + 11) / TILE);
    if (sideBlockAt(tl, ty) || sideBlockAt(tr, ty)) {
      py = (float)((ty + 1) * TILE);
      vy = 60.0f;
    } else {
      py += dy;
    }
  } else {
    int tb = floorTile((py + PLAYER_H + dy - 1) / TILE);
    int tl = floorTile((px + 4) / TILE);
    int tr = floorTile((px + 11) / TILE);
    if (tb > oldBottom && (landingBlockAt(tl, tb) || landingBlockAt(tr, tb))) {
      py = (float)(tb * TILE - (int)PLAYER_H);
      grounded = true;
      jumps = 0;
      ridingPlatform = -1;
      vy = 60.0f;
      if (cellAt(tl, tb).bonus == 10 || cellAt(tr, tb).bonus == 10) {
        vy = -630.0f;
        grounded = false;
        jumps = 1;
        sfxSample(wiz3_assets::SAMPLE_BOUNCE, sizeof(wiz3_assets::SAMPLE_BOUNCE));
      }
    } else {
      py += dy;
      for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity& e = entities[i];
        if (!e.alive || (e.type != 6 && e.type != 7 && e.type != 17)) continue;
        if (px + PLAYER_W <= e.x || px >= e.x + spriteWidth(e.type)) continue;
        if (oldY + PLAYER_H <= e.y + 4 && py + PLAYER_H >= e.y) {
          py = e.y - PLAYER_H;
          grounded = true;
          jumps = 0;
          ridingPlatform = i;
          e.platformOn = true;
          vy = 60.0f;
          break;
        }
      }
    }
  }

  if (py > 232) {
    py = 232;
    beginDeath(false);
  }
}

void checkBonuses() {
  int tx = clampi((int)((px + 8) / TILE), 0, MAP_W - 1);
  int ty = clampi((int)((py + 7) / TILE), 0, MAP_H - 2);
  if (cellAt(tx, ty).bonus <= 0 && cellAt(tx, ty + 1).bonus > 0) ++ty;
  Cell& c = cellAt(tx, ty);
  if (c.bonus >= 1 && c.bonus <= 6) collectBonus(c, tx, ty);
  if (c.bonus == 9) finishLevel();
  if (c.bonus == 10 && grounded) {
    vy = -660.0f;
    grounded = false;
    sfxSample(wiz3_assets::SAMPLE_BOUNCE, sizeof(wiz3_assets::SAMPLE_BOUNCE));
  }
  if (c.bonus != 8) switchFlag = false;
}

void checkHazards() {
  bool hidden = invisibleSpell && input.held(BTN_DOWN);
  for (int i = 0; i < MAX_ENTITIES; i++) {
    Entity& e = entities[i];
    if (!e.alive || !e.hazard) continue;
    if (!overlapRect(px + 2, py + 2, PLAYER_W - 4, PLAYER_H - 4,
                     e.x, e.y, spriteWidth(e.type), spriteHeight(e.type))) continue;
    if (hidden || (asbestos && (e.type == 9 || e.type == 13 || e.type == 14))) continue;
    if (invincible > 0) {
      if (invincible > 80.0f / ORIGINAL_FPS) {
        invincible = 80.0f / ORIGINAL_FPS;
        sfxSample(wiz3_assets::SAMPLE_HIT, sizeof(wiz3_assets::SAMPLE_HIT));
      }
      continue;
    }
    beginDeath(true);
    return;
  }
}

void drawSprite(uint8_t type, int frame, float wx, float wy, SpritePass pass) {
  if (type >= 23) return;
  const wiz3_assets::SpriteSheet& sheet = wiz3_assets::SPRITES[type];
  if (sheet.frames == 0) return;
  frame %= sheet.frames;
  if (frame < 0) frame += sheet.frames;
  int sx = floori((wx - cameraX) / 4.0f);
  int sy = floori(wy / 4.0f);
  int base = frame * sheet.width * sheet.height;
  for (int y = 0; y < sheet.height; y++) {
    for (int x = 0; x < sheet.width; x++) {
      uint16_t value = sheet.pixels[base + y * sheet.width + x];
      if (!value) continue;
      if (pass == PASS_RIM) {
        rimNeighbours(sx + x, sy + y);
      } else {
        pixel(sx + x, sy + y, spriteColor(value));
      }
    }
  }
}

void drawActors(SpritePass pass) {
  for (int i = 0; i < MAX_ENTITIES; i++) {
    if (entities[i].alive) {
      const Entity& e = entities[i];
      drawSprite(e.type, e.frame, e.x, e.y, pass);
    }
  }
  bool hidden = invisibleSpell && input.held(BTN_DOWN);
  if (!dead && !hidden) {
    int frame = (int)(walkTimer > 0 ? 0 : 1) + (facingRight ? 0 : 8);
    drawSprite(0, frame, px, py, pass);
  }
}

void drawBonuses(int first, int last, SpritePass pass) {
  for (int x = first; x <= last; x++) {
    int sx = floori(((float)(x * TILE) - cameraX) / 4.0f);
    for (int y = 0; y < MAP_H; y++) {
      drawBonus(cellAt(x, y).bonus, sx, y * 4, pass);
    }
  }
}

void drawWorld() {
  clear(BLACK);
  int first = clampi(floorTile(cameraX / TILE) - 1, 0, MAP_W - 1);
  int last = mini(MAP_W - 1, first + 18);
  for (int x = first; x <= last; x++) {
    int sx = floori(((float)(x * TILE) - cameraX) / 4.0f);
    for (int y = 0; y < MAP_H; y++) {
      int sy = y * 4;
      Cell& c = cellAt(x, y);
      drawTile(c.back, sx, sy, false);
      if (c.fore) drawTile(c.fore, sx, sy, true);
    }
  }

  // Rim everything collectable or animate before filling any of it in, so no
  // sprite ever darkens its neighbour's artwork.
  clearRim();
  drawBonuses(first, last, PASS_RIM);
  drawActors(PASS_RIM);
  drawBonuses(first, last, PASS_FILL);
  drawActors(PASS_FILL);
}

// A spare wizard and a potion flask, in the 3x5 cell the status strip allows.
const uint8_t GLYPH_WIZ[5] = {0b010, 0b111, 0b010, 0b111, 0b101};
const uint8_t GLYPH_FLASK[5] = {0b010, 0b010, 0b111, 0b111, 0b111};

void drawGlyph(int x, int y, const uint8_t rows[5], Color c) {
  for (int r = 0; r < 5; r++) {
    for (int b = 0; b < 3; b++) {
      if (rows[r] & (1 << (2 - b))) pixel(x + b, y + r, c);
    }
  }
}

void drawHud() {
  // The map is exactly screen-height, so an opaque status bar would black out
  // the top two tile rows. Dimming what is already there keeps the whole level
  // playable and still gives the readout a solid backing.
  for (int y = 0; y < 5; y++) {
    for (int x = 0; x < SCREEN_W; x++) pixel(x, y, scaleColor(getPixel(x, y), 0.22f));
  }
  hline(0, 5, SCREEN_W, rgb(26, 32, 56));

  // Lives, one pip per spare wizard, the way the original counts them.
  const Color pipColor = rgb(70, 210, 200);
  if (lives <= 4) {
    for (int i = 0; i < lives; i++) drawGlyph(1 + i * 4, 0, GLYPH_WIZ, pipColor);
  } else {
    drawGlyph(1, 0, GLYPH_WIZ, pipColor);
    drawNumber(5, 0, lives, GREEN);
  }

  // Potions towards the next rung of the spell ladder. The original prints the
  // count next to a flask; a bar reads far faster at 64 pixels across, and the
  // quarter ticks show how far the next spell is.
  drawGlyph(18, 0, GLYPH_FLASK, rgb(230, 90, 210));
  const int gx = 23;
  const int gw = 27;
  fillRect(gx, 1, gw, 3, rgb(12, 15, 26));
  int fill = clampi(124 - potionGauge, 0, 100) * gw / 100;
  if (fill > 0) fillRect(gx, 1, fill, 3, CYAN);
  for (int q = 1; q < 4; q++) {
    int tx = gx + q * gw / 4;
    pixel(tx, 2, tx < gx + fill ? rgb(8, 10, 20) : rgb(34, 42, 66));
  }

  char buf[8];
  buf[0] = 'L';
  formatNumber(buf + 1, currentLevel + 1);
  text(SCREEN_W - 1 - textWidth(buf), 0, buf, WHITE);
}

// Tint the whole panel instead of stamping a slab over it, so the level stays
// visible behind every message.
void scrimScreen(float keep, Color tint, float amount) {
  for (int y = 0; y < SCREEN_H; y++) {
    for (int x = 0; x < SCREEN_W; x++) {
      Color c = getPixel(x, y);
      pixel(x, y, rgb(chan(c.r * keep + tint.r * amount),
                      chan(c.g * keep + tint.g * amount),
                      chan(c.b * keep + tint.b * amount)));
    }
  }
}

// Centered text with a full black outline, which is what lets 3x5 glyphs stay
// legible on top of tile art.
void textOutlined(int y, const char* s, Color c) {
  int x = (SCREEN_W - textWidth(s)) / 2;
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      if (dx || dy) text(x + dx, y + dy, s, BLACK);
    }
  }
  text(x, y, s, c);
}

void numberOutlined(int y, int value, Color c) {
  char buf[16];
  formatNumber(buf, value);
  textOutlined(y, buf, c);
}

void drawOverlay() {
  if (levelClear && !won) {
    scrimScreen(0.34f, rgb(20, 120, 140), 0.12f);
    textOutlined(26, "LEVEL", CYAN);
    numberOutlined(34, currentLevel + 1, WHITE);
  } else if (won) {
    scrimScreen(0.28f, rgb(150, 120, 20), 0.14f);
    textOutlined(18, "WIZ3", CYAN);
    textOutlined(26, "COMPLETE", WHITE);
    numberOutlined(35, score, YELLOW);
    textOutlined(45, "CLICK-RETRY", GRAY);
  } else if (gameOver) {
    scrimScreen(0.28f, rgb(140, 20, 24), 0.14f);
    textOutlined(20, "GAME OVER", WHITE);
    numberOutlined(29, score, YELLOW);
    if (scoreRank == 0) textOutlined(38, "NEW BEST", CYAN);
    textOutlined(46, "CLICK-RETRY", GRAY);
  } else if (dead) {
    scrimScreen(0.42f, rgb(170, 30, 30), 0.13f);
    textOutlined(30, "OUCH", RED);
  }
}

void draw() {
  drawWorld();
  drawHud();
  drawOverlay();
}

void init() {
  setSfxStyle(STYLE_CHIP);
  music(MUS_ACTION);
  currentLevel = 0;
  lives = 3;
  score = 0;
  scoreRank = -1;
  potionGauge = 124;
  powerupStage = 0;
  maxJumps = 1;
  highJump = -540.0f;
  hasKey = false;
  invisibleSpell = false;
  asbestos = false;
  restoreOnDeath = false;
  invincible = 0;
  checkpointFlag = 1;
  checkpointSearchX = 0;
  restartLevel = false;
  dead = false;
  gameOver = false;
  won = false;
  loadLevel(0);
}

void update(float dt) {
  dt = clampf(dt, 0.0f, 0.08f);

  if (won) {
    draw();
    if (input.justDown(BTN_CLICK)) init();
    return;
  }
  if (gameOver) {
    gameOverTimer += dt;
    draw();
    if (gameOverTimer > 0.5f && input.justDown(BTN_CLICK)) init();
    return;
  }
  if (levelClear) {
    levelTimer += dt;
    draw();
    if (levelTimer > 0.8f) {
      if (currentLevel + 1 < wiz3_assets::LEVEL_COUNT) {
        checkpointFlag = 1;
        checkpointSearchX = 0;
        loadLevel(currentLevel + 1);
      } else {
        levelClear = false;
        won = true;
        scoreRank = submitScore(score);
      }
    }
    return;
  }
  if (dead) {
    deathTimer += dt;
    draw();
    if (deathTimer > 0.9f) {
      if (lives > 0) {
        loadLevel(currentLevel);
        invincible = 80.0f / ORIGINAL_FPS;
      } else {
        gameOver = true;
        gameOverTimer = 0;
        scoreRank = submitScore(score);
      }
    }
    return;
  }

  if (invincible > 0) invincible = fmaxf_(0.0f, invincible - dt);
  updateEntities(dt);
  movePlayer(dt);
  if (dead) {
    draw();
    return;
  }
  checkBonuses();
  if (input.justDown(BTN_UP)) handleInteraction();
  if (restartLevel) {
    restartLevel = false;
    loadLevel(currentLevel);
    draw();
    return;
  }
  checkHazards();
  if (dead) {
    draw();
    return;
  }

  if (currentLevel == wiz3_assets::LEVEL_COUNT - 1 && px > (MAP_W - 8) * TILE) {
    finishLevel();
  }
  const float cameraMin = playerLeftBound - TILE;
  const float cameraMax = fmaxf_(cameraMin, playerRightBound - VIEW_W + TILE);
  cameraX = lerpf(cameraX, clampf(px - VIEW_W * 0.5f, cameraMin, cameraMax),
                  clampf(dt * 8.0f, 0.0f, 1.0f));
  draw();
}

}  // namespace

PT_GAME_SCORED(wiz3, "WIZ3", init, update, pt::SCORE_POINTS)
