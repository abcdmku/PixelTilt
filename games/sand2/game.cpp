// Sand II — a real granular-physics toy. A rainbow band of ~1000 soft
// particles simulated with position-based dynamics (the small cousin of how
// film/game sand is done): integrate, predict positions, then iteratively
// project overlaps apart with tangential friction, and derive velocities
// from how far positions actually moved. There is ONE driving force law and
// no gesture modes anywhere:
//
//   field = raw specific force (tilt AND shake, straight off the IMU)
//         + rotating-frame terms about the panel center (centrifugal,
//           Euler, Coriolis; omega from the field's sweep rate when the
//           panel is upright, the IMU yaw rate when it lies flat)
//
// Because the packing is soft (particles compress and shear a little, like
// real ~60%-dense sand, instead of a rigid one-grain-per-cell lattice),
// every gesture just works: tilting pours, a flick sloshes the whole bed
// sideways and splashes up the far wall, a down-jerk past free fall lifts
// it off the floor, spinning swirls and flings it, and any combination of
// those at once is still just the field. Rest is automatically clean —
// derived velocities of a settled pile are ~zero, no phantom momentum.
// Out-of-plane jolts (push-pull on a vertical panel) can't appear in the
// in-plane field, so they add a small agitation kick — the one term that
// isn't pure field. CLICK resets the rainbow. In the emulator: arrows tilt,
// Space shakes, Q/E spin.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int W = 64, H = 64;
constexpr int ROWS = 16;             // rainbow band height
constexpr int NP = W * ROWS;         // particle count

// Pixel-space scale of 1 g: held still the field is gravity following the
// real law a = g*sin(tilt angle) over the full 0-90 degrees; mid-shake it
// swings past 1 g and flips, and the bed lifts off the floor like the real
// thing.
constexpr float PX_PER_G = 900.0f;
// Rotation pseudo-forces are geometric (omega^2 * r in px IS px accel), but
// gravity here is compressed (900 vs the panel's true ~3270 px/s^2), so
// full-scale rotation would overpower it 3.6x beyond real life. 0.55 splits
// the difference: spins clearly fling and swirl, gravity still wins at rest.
constexpr float ROT_SCALE = 0.55f;
constexpr float VMAX = 260.0f;       // speed cap, px/s
constexpr float FIELD_MAX = 3.0f;    // sanity clamp only — full slam violence
constexpr int   SUBSTEPS = 2;        // integration substeps per frame
constexpr int   ITERS = 3;           // constraint relaxation sweeps
constexpr float DIAM = 1.0f;         // particle contact distance, px
// Friction coefficients are PER RELAXATION SWEEP and compound over ITERS
// sweeps: net tangential damping ~= 1-(1-mu)^ITERS. These values give ~0.4
// net contact friction (a sand-like repose) and ~0.45 against walls.
constexpr float MU = 0.16f;
constexpr float WALL_MU = 0.18f;

float pxs[NP], pys[NP];              // positions (px, sub-pixel)
float vxs[NP], vys[NP];              // velocities (px/s)
float oxs[NP], oys[NP];              // substep-start positions
Color col[NP];

// Spatial hash: linked particle lists per cell, rebuilt each substep.
int16_t head[W * H];
int16_t nxt[NP];
int16_t cellOf[NP];   // bucket each particle was built into this substep

// Unit field direction for the current frame (zero when dead level): the
// constraint solver pushes the upstream particle of an overlapping pair
// harder, so pressure escapes to the free surface instead of compressing
// deep piles.
float fdirX, fdirY;

// Rotation about the panel normal, rad/s (+ = clockwise as viewed).
// Upright, derived from how fast the gravity vector sweeps in screen coords
// (the panel rotating CW makes the field appear to rotate CCW); flat, the
// IMU's yaw rate IS the normal-axis rate. rotAlpha is spin-up/down.
float rotOmega, prevOmega, rotAlpha;

// Solver effort inputs only — there is no sleep or settle state.
float maxSp;    // fastest grain last frame (picks the substep count)
int   deepOv;   // deep overlaps last frame (backs off sweeps during a crush)

// Zero-compression bookkeeping (see decompress()).
uint16_t occGen[W * H];    // generation-stamped occupancy (no per-frame clear)
uint16_t occStamp;
int16_t  clumped[NP];
int16_t  bfsQ[W * H];
uint16_t bfsGen[W * H];
uint16_t bfsStamp;
int8_t   ringX[168], ringY[168];  // offsets |dx|,|dy| <= 6, sorted by distance
int      ringN;
int16_t  lastReloc[NP];   // where each grain was relocated to last frame

void resetField() {
  int i = 0;
  for (int y = 0; y < ROWS; y++)
    for (int x = 0; x < W; x++, i++) {
      pxs[i] = x + 0.5f;
      pys[i] = y + 0.5f;
      vxs[i] = 0;
      vys[i] = 0;
      float hue = x * 330.0f / (W - 1) + randRange(-8, 8);
      col[i] = hsv(hue, 0.95f, 0.7f + 0.3f * randf());
    }
}

void init() {
  setSfxStyle(STYLE_SOFT);
  music(MUS_CHILL);
  ringN = 0;
  for (int dy = -6; dy <= 6; dy++)
    for (int dx = -6; dx <= 6; dx++) {
      if ((!dx && !dy) || dx * dx + dy * dy > 36) continue;
      int k = ringN++;
      while (k > 0 && ringX[k - 1] * ringX[k - 1] + ringY[k - 1] * ringY[k - 1] >
                          dx * dx + dy * dy) {
        ringX[k] = ringX[k - 1];
        ringY[k] = ringY[k - 1];
        k--;
      }
      ringX[k] = (int8_t)dx;
      ringY[k] = (int8_t)dy;
    }
  resetField();
  rotOmega = 0;
  prevOmega = 0;
  rotAlpha = 0;
  maxSp = 0;
  deepOv = 0;
  occStamp = 0;
  bfsStamp = 0;
  for (int c = 0; c < W * H; c++) { occGen[c] = 0; bfsGen[c] = 0; }
  for (int i = 0; i < NP; i++) lastReloc[i] = -1;
}

// Fast inverse square root (one Newton step, ~0.2% error — plenty for
// grain contacts). The ESP32-S3 FPU has no hardware sqrt or divide, so this
// pure multiply/add path is several times cheaper than sqrtf + divisions.
inline float rsqrt_(float x) {
  union { float f; uint32_t u; } v;
  v.f = x;
  v.u = 0x5F3759DFu - (v.u >> 1);
  float y = v.f;
  return y * (1.5f - 0.5f * x * y * y);
}

void buildGrid() {
  for (int c = 0; c < W * H; c++) head[c] = -1;
  for (int i = 0; i < NP; i++) {
    int cx = clampi(floori(pxs[i]), 0, W - 1);
    int cy = clampi(floori(pys[i]), 0, H - 1);
    int c = cy * W + cx;
    nxt[i] = head[c];
    head[c] = (int16_t)i;
    cellOf[i] = (int16_t)c;
  }
}

// Push an overlapping pair apart, with position-level tangential friction:
// damping the pair's relative slip THIS substep is what lets heaps hold a
// slope (static friction) while still shearing when pushed hard.
inline void solvePair(int i, int j) {
  // SQUARE grains, separated along the axis of least penetration (the
  // minimum-translation vector for two axis-aligned unit cells). This is
  // the key to a pixel display: round grains settle into HEXAGONAL packing,
  // which cannot align to a square pixel grid — cells end up holding two
  // grains while neighbours sit empty, and the anti-compression pass then
  // shuffles grains every frame (visible as pixels endlessly swapping
  // spots). Square contacts settle into a square lattice: exactly one grain
  // per pixel, nothing to relocate, rock-steady at rest. Bonus: no sqrt.
  float dx = pxs[j] - pxs[i], dy = pys[j] - pys[i];
  float adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
  if (adx >= DIAM || ady >= DIAM) return;
  float ovx = DIAM - adx, ovy = DIAM - ady;
  float nx, ny, ov;
  if (ovx <= ovy) {
    nx = dx < 0.0f ? -1.0f : 1.0f;
    ny = 0.0f;
    ov = ovx;
  } else {
    nx = 0.0f;
    ny = dy < 0.0f ? -1.0f : 1.0f;
    ov = ovy;
  }
  // Full-stiffness pushes — this is what makes the bed feel incompressible
  // and lively; the bulk damping below keeps stacked chains from boiling.
  // Deep overlaps get the full correction so slams decompress immediately
  // (compression is also a perf drain: packed cells multiply contacts).
  float push = ov * (ov > 0.3f ? 1.0f : 0.9f);
  if (ov > 0.2f) deepOv++;
  // Gravity-weighted split: the particle on the sky side of the pair takes
  // most of the correction, decompressing deep stacks toward the surface.
  float fdot = nx * fdirX + ny * fdirY;   // >0 when j is downstream of i
  float wi = clampf(0.5f + 0.3f * fdot, 0.15f, 0.85f);
  pxs[i] -= nx * push * wi;
  pys[i] -= ny * push * wi;
  pxs[j] += nx * push * (1.0f - wi);
  pys[j] += ny * push * (1.0f - wi);
  if (ov > 0.0f) {  // friction only for true contact, not the skin range
    float mdx = (pxs[i] - oxs[i]) - (pxs[j] - oxs[j]);
    float mdy = (pys[i] - oys[i]) - (pys[j] - oys[j]);
    float mn = mdx * nx + mdy * ny;
    float tx = mdx - mn * nx, ty = mdy - mn * ny;
    float slip2 = tx * tx + ty * ty;
    // Coulomb friction with a real STATIC regime. Below a tiny slip the
    // contact STICKS: the pair's relative tangential motion is cancelled
    // outright (k = 0.5 shares the correction equally), so a resting pile
    // locks up and genuinely stops — no timers, no global damping, nothing
    // that could also stifle a real flow. Above that threshold the contact
    // slips with kinetic friction, rising with shear rate so avalanche
    // sheets dissipate instead of ricocheting off the bed.
    constexpr float STICK = 0.004f;   // px of slip per substep
    float k = slip2 < STICK * STICK
                  ? 0.5f
                  : 0.5f * (MU + fminf_(0.25f, 3.0f * slip2));
    pxs[i] -= tx * k;
    pys[i] -= ty * k;
    pxs[j] += tx * k;
    pys[j] += ty * k;
  }
}

// ZERO COMPRESSION: hard one-grain-per-pixel. The soft solver keeps grains
// ~a pixel apart, but a hard slam can still momentarily squeeze several
// into one cell. This pass makes that impossible — any grain sharing a cell
// is relocated to the nearest free cell (spiral search, offsets sorted by
// distance at init). Positions only; it runs AFTER velocities are derived,
// so it never injects momentum. The bed is now strictly incompressible: it
// always occupies exactly 1024 distinct pixels, and every grain is visible.
void decompress() {
  if (++occStamp == 0) {
    for (int c = 0; c < W * H; c++) occGen[c] = 0;
    occStamp = 1;
  }
  int nclump = 0;
  for (int i = 0; i < NP; i++) {
    int cx = clampi(floori(pxs[i]), 0, W - 1);
    int cy = clampi(floori(pys[i]), 0, H - 1);
    int c = cy * W + cx;
    if (occGen[c] != occStamp) {
      occGen[c] = occStamp;
      lastReloc[i] = -1;   // sitting in its own cell: forget any old spot
    } else {
      clumped[nclump++] = (int16_t)i;
    }
  }
  // Nearest free cell: a cheap distance-sorted ring scan resolves virtually
  // every clump; BFS is the exhaustive fallback for grains buried deep in a
  // centrifugal pack. 1024 grains in 4096 pixels means a free cell always
  // exists, so relocation cannot fail.
  for (int p = 0; p < nclump; p++) {
    int i = clumped[p];
    int scx = clampi(floori(pxs[i]), 0, W - 1);
    int scy = clampi(floori(pys[i]), 0, H - 1);
    bool placed = false;
    // Hysteresis: a grain that had to be relocated last frame reclaims the
    // SAME cell if it is still free and still adjacent. Without this the
    // search picks a different neighbour as the pile shifts, and grains
    // visibly trade places every frame in any banked pile.
    int prev = lastReloc[i];
    if (prev >= 0 && occGen[prev] != occStamp) {
      int dx = (prev % W) - scx, dy = (prev / W) - scy;
      if (dx * dx + dy * dy <= 8) {
        occGen[prev] = occStamp;
        pxs[i] = (prev % W) + 0.5f;
        pys[i] = (prev / W) + 0.5f;
        continue;
      }
    }
    for (int k = 0; k < ringN; k++) {
      int nx = scx + ringX[k], ny = scy + ringY[k];
      if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
      int c = ny * W + nx;
      if (occGen[c] == occStamp) continue;
      occGen[c] = occStamp;
      pxs[i] = nx + 0.5f;
      pys[i] = ny + 0.5f;
      lastReloc[i] = (int16_t)c;
      placed = true;
      break;
    }
    if (placed) continue;
    int start = scy * W + scx;
    if (++bfsStamp == 0) {
      for (int c = 0; c < W * H; c++) bfsGen[c] = 0;
      bfsStamp = 1;
    }
    int qh = 0, qt = 0;
    bfsQ[qt++] = (int16_t)start;
    bfsGen[start] = bfsStamp;
    while (qh < qt) {
      int c = bfsQ[qh++];
      if (occGen[c] != occStamp) {
        occGen[c] = occStamp;
        pxs[i] = (c % W) + 0.5f;
        pys[i] = (c / W) + 0.5f;
        lastReloc[i] = (int16_t)c;
        break;
      }
      int cx = c % W, cy = c / W;
      if (cx > 0     && bfsGen[c - 1] != bfsStamp) { bfsGen[c - 1] = bfsStamp; bfsQ[qt++] = (int16_t)(c - 1); }
      if (cx < W - 1 && bfsGen[c + 1] != bfsStamp) { bfsGen[c + 1] = bfsStamp; bfsQ[qt++] = (int16_t)(c + 1); }
      if (cy > 0     && bfsGen[c - W] != bfsStamp) { bfsGen[c - W] = bfsStamp; bfsQ[qt++] = (int16_t)(c - W); }
      if (cy < H - 1 && bfsGen[c + W] != bfsStamp) { bfsGen[c + W] = bfsStamp; bfsQ[qt++] = (int16_t)(c + W); }
    }
  }
}

void draw() {
  clear(rgb(3, 4, 10));
  for (int i = 0; i < NP; i++) pixel(floori(pxs[i]), floori(pys[i]), col[i]);
}

void update(float dt) {
  if (input.justDown(BTN_CLICK)) {
    resetField();
    sfx(SFX_SELECT);
  }

  // The field: raw specific force in g, zero extra filtering. A tiny
  // deadzone keeps sensor noise from creeping a dead-level board.
  float gvx = input.gravityX, gvy = input.gravityY;
  float mag = sqrtf_(gvx * gvx + gvy * gvy);
  // No deadzone: the faintest lean still pours. (The engine already
  // subtracts the boot-time level reference, so a resting board reads ~0.)
  if (mag > FIELD_MAX) {
    float sc = FIELD_MAX / mag;
    gvx *= sc;
    gvy *= sc;
  }
  float fgx = gvx * PX_PER_G, fgy = gvy * PX_PER_G;
  if (mag > 0.05f) { fdirX = gvx / mag; fdirY = gvy / mag; }
  else             { fdirX = 0; fdirY = 0; }

  // Rotation about the panel normal, straight from the IMU (input.spin is
  // the true normal-axis angular velocity in every orientation — see the
  // firmware's body-rate derivation). No inference from how the field
  // sweeps: that could not tell a spin from an ordinary tilt, which is why
  // tilting used to fling sand and spinning did nothing in most poses.
  // A small deadzone keeps gyro noise from creeping a resting bed.
  float w = fabsf_(input.spin) > 0.15f ? input.spin : 0.0f;
  rotOmega += (clampf(w, -15.0f, 15.0f) - rotOmega) * fminf_(1.0f, 20.0f * dt);
  if (fabsf_(rotOmega) < 0.05f) rotOmega = 0.0f;
  rotAlpha = dt > 0.0001f ? clampf((rotOmega - prevOmega) / dt, -20.0f, 20.0f) : 0;
  prevOmega = rotOmega;

  // Flick impulse. The field alone cannot sell a flick when the panel is
  // upright: there, a full 1 g of gravity pins the bed down and the pile's
  // own repose angle must be exceeded before anything moves, so a flick
  // that visibly sloshes a flat panel does almost nothing vertically. Real
  // sand in an upright frame does slosh, so the in-plane linear
  // acceleration is ALSO applied directly as a velocity impulse — that is
  // orientation-independent, so a flick feels the same in every pose. The
  // per-grain variation is what shears the bed loose instead of sliding it
  // rigidly, and it decays through the normal contact damping.
  float am = sqrtf_(input.accelX * input.accelX + input.accelY * input.accelY);
  if (am > 0.12f) {
    float ik = 1500.0f * (am - 0.12f) / am * dt;
    float ix = input.accelX * ik, iy = input.accelY * ik;
    for (int i = 0; i < NP; i++) {
      float j = 0.7f + 0.6f * randf();
      vxs[i] += ix * j;
      vys[i] += iy * j;
    }
  }

  // Out-of-plane jolt: a push-pull along the panel normal can't show up in
  // the in-plane field, so it becomes a small agitation kick, biased
  // against the field (grains hop off the bed) with sideways scatter.
  float jolt = fabsf_(input.accelZ);
  if (jolt > 0.35f) {
    float amp = fminf_(jolt - 0.35f, 1.5f) * 700.0f * dt;
    float ux = 0, uy = 0;
    if (mag > 0.05f) { ux = -gvx / mag; uy = -gvy / mag; }
    for (int i = 0; i < NP; i++) {
      float k = amp * (0.4f + randf());
      vxs[i] += ux * k + (randf() - 0.5f) * amp;
      vys[i] += uy * k + (randf() - 0.5f) * amp;
    }
  }

  // The sim ALWAYS runs. There is no sleep or settle state: nothing is ever
  // frozen, quieted or gated, so the sand stays live and responds to the
  // faintest input at any moment. (Every stopping mechanism this once had —
  // sleep, wake thresholds, settle ramps, slow-grain quench — is gone.
  // Motion is removed only by real friction.)

  // Effort policy, bounded at both ends:
  //  - impact mode (fast, not yet crushed): a third substep — finer steps
  //    mean less interpenetration, which is stiffer-looking AND cheaper
  //    (packed cells multiply contact probes);
  //  - crush mode (sustained centrifugal/wall pack, hundreds of deep
  //    overlaps): the compression is unresolvable this instant, so cap the
  //    sweeps instead of burning the frame budget — the bed springs back
  //    the moment the crush ends and full effort resumes.
  // Substeps are capped at 2. A third substep was previously used for fast
  // impacts, but on the real ESP32 it triggered constantly and tripled the
  // solver cost (14-20 ms/frame, well past the frame budget) for no visible
  // benefit. Sweeps stay at 3: that third relaxation sweep is what lets the
  // static-friction regime actually lock a resting pile.
  int sub = 2;
  int iters = deepOv > 150 ? 2 : ITERS;
  float h = fminf_(dt, 1.0f / 30.0f) / sub;
  if (h <= 0.0f) { draw(); return; }
  // NO artificial settling anywhere. Motion is only ever removed by real
  // physics — contact friction, wall drag and inelastic collisions.
  maxSp = 0;
  deepOv = 0;

  for (int s = 0; s < sub; s++) {
    // Integrate: per-particle field (uniform part + rotating-frame terms),
    // predict positions.
    for (int i = 0; i < NP; i++) {
      float ax = fgx, ay = fgy;
      if (rotOmega != 0.0f || rotAlpha != 0.0f) {
        // Centrifugal + Euler only. Coriolis (∝ velocity) is deliberately
        // omitted: the omega estimate can be partially spurious while
        // re-aiming a mid-range tilt, and Coriolis turns fast avalanche
        // streams into ceiling-bound arcs when that happens — while adding
        // almost nothing visually for a sand toy. The magnitude cap bounds
        // the sustained centrifugal crush of a hard flat spin (otherwise it
        // packs the whole bed against the rim, which both over-compresses
        // and blows the contact count — the flat-fling lag).
        float rx = pxs[i] - 32.0f, ry = pys[i] - 32.0f;
        float cf = rotOmega * rotOmega;
        float arx = ROT_SCALE * (cf * rx + rotAlpha * ry);
        float ary = ROT_SCALE * (cf * ry - rotAlpha * rx);
        float am2 = arx * arx + ary * ary;
        if (am2 > 1000.0f * 1000.0f) {
          float sc = 1000.0f * rsqrt_(am2);
          arx *= sc;
          ary *= sc;
        }
        ax += arx;
        ay += ary;
      }
      float vx = vxs[i] + ax * h;
      float vy = vys[i] + ay * h;
      float sp2 = vx * vx + vy * vy;
      if (sp2 > VMAX * VMAX) {
        float sc = VMAX / sqrtf_(sp2);
        vx *= sc;
        vy *= sc;
      }
      vxs[i] = vx;
      vys[i] = vy;
      oxs[i] = pxs[i];
      oys[i] = pys[i];
      pxs[i] += vx * h;
      pys[i] += vy * h;
    }

    buildGrid();

    for (int it = 0; it < iters; it++) {
      // Forward half-plane sweep: own cell (later list entries only) plus
      // E, SW, S, SE neighbors — every unordered pair visited exactly once,
      // ~45% fewer probes than scanning the full 3x3 and skipping half.
      for (int i = 0; i < NP; i++) {
        int c = cellOf[i];
        int cx = c % W, cy = c / W;
        for (int j = nxt[i]; j >= 0; j = nxt[j]) solvePair(i, j);
        if (cx < W - 1)
          for (int j = head[c + 1]; j >= 0; j = nxt[j]) solvePair(i, j);
        if (cy < H - 1) {
          int rowS = c + W;
          if (cx > 0)
            for (int j = head[rowS - 1]; j >= 0; j = nxt[j]) solvePair(i, j);
          for (int j = head[rowS]; j >= 0; j = nxt[j]) solvePair(i, j);
          if (cx < W - 1)
            for (int j = head[rowS + 1]; j >= 0; j = nxt[j]) solvePair(i, j);
        }
      }
      // Walls: hard clamp plus tangential friction while touching, so a
      // sliding bed drags against the floor instead of skating forever —
      // with the same static regime as grain contacts (a barely-creeping
      // grain sticks to the wall outright).
      constexpr float WSTICK = 0.004f;
      for (int i = 0; i < NP; i++) {
        bool hx = false, hy = false;
        if (pxs[i] < 0.5f)     { pxs[i] = 0.5f;     hx = true; }
        if (pxs[i] > W - 0.5f) { pxs[i] = W - 0.5f; hx = true; }
        if (pys[i] < 0.5f)     { pys[i] = 0.5f;     hy = true; }
        if (pys[i] > H - 0.5f) { pys[i] = H - 0.5f; hy = true; }
        if (hx) {
          float slip = pys[i] - oys[i];
          pys[i] -= slip * (fabsf_(slip) < WSTICK ? 1.0f : WALL_MU);
        }
        if (hy) {
          float slip = pxs[i] - oxs[i];
          pxs[i] -= slip * (fabsf_(slip) < WSTICK ? 1.0f : WALL_MU);
        }
      }
    }

    // The PBD step: velocity IS how far the constraints let you move. This
    // gives inelastic collisions, wall absorption, and clean rest (a
    // settled pile's derived velocities are ~zero) with no extra rules.
    for (int i = 0; i < NP; i++) {
      pxs[i] = clampf(pxs[i], 0.5f, W - 0.5f);
      pys[i] = clampf(pys[i], 0.5f, H - 0.5f);
    }

    float invH = 1.0f / h;
    // A whisper of bulk drag (air resistance, essentially) — enough to keep
    // the integrator from gaining energy, far too little to stop a flow.
    float damp = fmaxf_(0.0f, 1.0f - 0.3f * h);
    for (int i = 0; i < NP; i++) {
      // Contacts are dissipative: the constraint solver may redirect or slow
      // a particle freely, but may raise its speed only slower than gravity
      // can reclaim it — otherwise a compressed bed vents its overlap
      // through the surface as an ever-accelerating dust spray whenever the
      // field rotates. With the gain capped below the field's per-substep
      // pull, ejected grains can never outrun gravity: they fluff and fall.
      float pvx = vxs[i], pvy = vys[i];
      float vx = (pxs[i] - oxs[i]) * invH;
      float vy = (pys[i] - oys[i]) * invH;
      float sp2 = vx * vx + vy * vy;
      float psp2 = pvx * pvx + pvy * pvy;
      // Constraints may redirect/stop freely but may only ADD speed slower
      // than a moderate field reclaims it (scaled per substep) — stops the
      // compressed-bed dust-mist without muting slam decompression. The
      // sqrt only runs when speed actually grew (rare).
      if (sp2 > psp2) {
        float psp = psp2 > 1e-12f ? psp2 * rsqrt_(psp2) : 0.0f;
        float allow = psp + 500.0f * h;
        if (sp2 > allow * allow) {
          float sc = allow * rsqrt_(sp2);
          vx *= sc;
          vy *= sc;
          sp2 = allow * allow;
        }
      }
      if (sp2 > VMAX * VMAX) {
        float sc = VMAX * rsqrt_(sp2);
        vx *= sc;
        vy *= sc;
        sp2 = VMAX * VMAX;
      }
      vx *= damp;
      vy *= damp;
      vxs[i] = vx;
      vys[i] = vy;
      if (sp2 > maxSp * maxSp) maxSp = sqrtf_(sp2);
    }
  }

  // Positions only, after velocities are derived: no momentum injected.
  decompress();

  draw();
}

}  // namespace

PT_GAME(sand2, "SAND II", init, update)
