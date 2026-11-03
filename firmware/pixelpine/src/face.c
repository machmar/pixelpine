/* Pine's face: the pixel parts, renderPose() and the 18 behaviours (FB) of simulator/index.html.
 * Behaviour logic is in double, statement for statement as in the JS; st->rng is the scene's makeRng() stream.
 * A pose is built in a pose_t (the JS object literal), rendered into a Q12 target and eased into fbuf. */
#include <math.h>
#include "pine.h"

#define FACE_AX 0
#define FACE_AY 15
enum { E_OPEN, E_WIDE, E_HAPPY, E_SLEEP, E_CLOSED, E_LID, E_SQUINT, E_DOT, E_SPARK, E_NONE };
enum { M_SMILE, M_SMALL, M_GRIN, M_CAT, M_O, M_YAWN, M_FLAT, M_POUT, M_WAVY, M_NONE };
enum { G_Z, G_NOTE, G_PLUS, G_DOT, G_DROP, G_PUFF };

typedef struct { int8_t n; int8_t p[8][2]; } shape_t;
static const shape_t EYE[] = {              /* left eye; the right eye is the mirror image */
  [E_OPEN] = { 4, { {-4, -1}, {-3, -1}, {-4, 0}, {-3, 0} } },
  [E_WIDE] = { 6, { {-4, -2}, {-3, -2}, {-4, -1}, {-3, -1}, {-4, 0}, {-3, 0} } },
  [E_HAPPY] = { 3, { {-5, 0}, {-4, -1}, {-3, 0} } },
  [E_SLEEP] = { 3, { {-5, -1}, {-4, 0}, {-3, -1} } },
  [E_CLOSED] = { 3, { {-5, 0}, {-4, 0}, {-3, 0} } },
  [E_LID] = { 5, { {-5, -1}, {-4, -1}, {-3, -1}, {-4, 0}, {-3, 0} } },
  [E_SQUINT] = { 3, { {-5, -1}, {-4, 0}, {-5, 1} } },
  [E_DOT] = { 1, { {-4, 0} } },
  [E_SPARK] = { 5, { {-4, -2}, {-5, -1}, {-4, -1}, {-3, -1}, {-4, 0} } },
};
static const uint8_t GAZE[] = { [E_OPEN] = 1, [E_WIDE] = 1, [E_DOT] = 1, [E_LID] = 1, [E_SPARK] = 1, [E_NONE] = 0 };
static const shape_t MOUTH[] = {
  [M_SMILE] = { 5, { {-2, 2}, {-1, 3}, {0, 3}, {1, 3}, {2, 2} } },
  [M_SMALL] = { 3, { {-1, 2}, {0, 3}, {1, 2} } },
  [M_GRIN] = { 8, { {-2, 2}, {-1, 2}, {0, 2}, {1, 2}, {2, 2}, {-1, 3}, {0, 3}, {1, 3} } },
  [M_CAT] = { 5, { {-2, 2}, {-1, 3}, {0, 2}, {1, 3}, {2, 2} } },
  [M_O] = { 4, { {0, 1}, {-1, 2}, {1, 2}, {0, 3} } },
  [M_YAWN] = { 8, { {-1, 1}, {0, 1}, {1, 1}, {-1, 2}, {1, 2}, {-1, 3}, {0, 3}, {1, 3} } },
  [M_FLAT] = { 3, { {-1, 3}, {0, 3}, {1, 3} } },
  [M_POUT] = { 5, { {-2, 3}, {-1, 2}, {0, 2}, {1, 2}, {2, 3} } },
  [M_WAVY] = { 5, { {-2, 3}, {-1, 2}, {0, 3}, {1, 2}, {2, 3} } },
  [M_NONE] = { 0, { {0, 0} } },
};
static const shape_t GLYPH[] = {
  [G_Z] = { 7, { {0, 0}, {1, 0}, {2, 0}, {1, 1}, {0, 2}, {1, 2}, {2, 2} } },
  [G_NOTE] = { 5, { {1, 0}, {2, 0}, {1, 1}, {1, 2}, {0, 2} } },
  [G_PLUS] = { 5, { {0, -1}, {-1, 0}, {0, 0}, {1, 0}, {0, 1} } },
  [G_DOT] = { 1, { {0, 0} } },
  [G_DROP] = { 2, { {0, 0}, {0, 1} } },
  [G_PUFF] = { 3, { {0, 0}, {-1, -1}, {-1, 0} } },
};

/* one-shot timelines (FB[x].dur); < 0 = loops until the brain moves on */
const double face_dur[F_COUNT] = {
  [F_IDLE] = -1, [F_GIGGLE] = -1, [F_SLEEP] = -1, [F_STIR] = 2.4, [F_WINK] = 2.8, [F_SING] = -1, [F_SNEEZE] = 7.5,
  [F_DOZE] = 8, [F_GRUMPY] = -1, [F_EXCITED] = 2.6, [F_SURPRISED] = 1.4, [F_MORNING] = 6.2, [F_PET] = -1,
  [F_HUG] = 2.2, [F_SQUIRM] = 1.5, [F_TICKLED] = 3, [F_PEEK] = 2.2, [F_YAWN] = 1.8,
};

typedef struct { uint8_t g, cell; float x, y, v; } extra_t;   /* positions only get rounded to a grid cell */
typedef struct {
  uint8_t eyeL, eyeR, mouth;
  int lx, ly, dx, dy;
  double lvl, mouth_lvl, blush, body;
  uint8_t nex;
  extra_t ex[30];
} pose_t;

static void pose_reset(pose_t *p) {
  p->eyeL = p->eyeR = E_OPEN; p->mouth = M_SMILE;
  p->lx = p->ly = p->dx = p->dy = 0;
  p->lvl = 1; p->mouth_lvl = 1; p->blush = 0; p->body = 0; p->nex = 0;
}
static void eye(pose_t *p, uint8_t e) { p->eyeL = p->eyeR = e; }
/* an extra already on a grid cell with its brightness in Q12 (x, y exact integers, v = Q12 / 4096) */
static void exq(pose_t *p, uint8_t g, int x, int y, q12 v) {
  if (p->nex < 30) { extra_t *e = &p->ex[p->nex++]; e->g = g; e->cell = 1; e->x = (float)x; e->y = (float)y; e->v = (float)v; }
}
static void ex(pose_t *p, uint8_t g, double x, double y, double v) {
  if (p->nex < 30) { extra_t *e = &p->ex[p->nex++]; e->g = g; e->cell = 0; e->x = (float)x; e->y = (float)y; e->v = (float)v; }
}
static int jround(double x) { return (int)floor(x + 0.5); }          /* Math.round */
#define RND(st) xs_d(&(st)->rng)

static uint16_t tgt[NLED];                       /* render target, shared: rendered and eased in one go */
static void pset(int gx, int gy, q12 v) { int i = led_at(gx, gy); if (i >= 0 && v > tgt[i]) tgt[i] = (uint16_t)v; }

static void render_pose(const pose_t *p) {
  q12 body = dq(p->body);
  for (int i = 0; i < NLED; i++) tgt[i] = (uint16_t)(body ? qm(body, LBODY(i)) : 0);
  double L = p->lvl;
  int ax = FACE_AX + p->dx, ay = FACE_AY + p->dy;
  q12 Lq = dq(L);
  for (int side = 1; side >= -1; side -= 2) {
    uint8_t e = side > 0 ? p->eyeL : p->eyeR;
    if (e == E_NONE) continue;
    int lx = GAZE[e] ? p->lx : 0, ly = GAZE[e] ? p->ly : 0;
    for (int k = 0; k < EYE[e].n; k++) pset(ax + side * EYE[e].p[k][0] + lx, ay + EYE[e].p[k][1] + ly, Lq);
  }
  q12 mq = dq(L * p->mouth_lvl);
  for (int k = 0; k < MOUTH[p->mouth].n; k++) pset(ax + MOUTH[p->mouth].p[k][0], ay + MOUTH[p->mouth].p[k][1], mq);
  if (p->blush) {
    q12 bq = dq((p->blush < 1 ? p->blush : 1) * 0.5 * L);
    for (int s = 1; s >= -1; s -= 2) for (int x = -6; x <= -5; x++) pset(ax + s * x, ay + 2, bq);
  }
  for (int j = 0; j < p->nex; j++) {
    const extra_t *e = &p->ex[j];
    if (e->v <= 0) continue;
    q12 v;
    int bx, by;
    if (e->cell) { v = Lq == ONE ? (q12)e->v : qm((q12)e->v, Lq); bx = (int)e->x; by = (int)e->y; }
    else { v = dq((e->v < 1 ? e->v : 1) * L); bx = jround(e->x); by = jround(e->y); }   /* float -> double: exact */
    for (int k = 0; k < GLYPH[e->g].n; k++) pset(bx + GLYPH[e->g].p[k][0], by + GLYPH[e->g].p[k][1], v);
  }
}

/* ---------- little behaviour helpers ---------- */
static uint8_t blinker(face_t *st, double dt) {
  if (isnan(st->bt)) st->bt = 1 + RND(st) * 2;
  st->bt -= dt;
  if (st->bt < 0) { st->bl = 0.13; st->bt = 2 + RND(st) * 3.5; if (RND(st) < 0.2) st->dbl = 0.3; }
  if (st->dbl > 0) { st->dbl -= dt; if (st->dbl <= 0) st->bl = 0.13; }
  if (st->bl > 0) { st->bl -= dt; return 1; }
  return 0;
}
static void glancer(face_t *st, double dt, int *lx, int *ly) {
  st->gt = (isnan(st->gt) ? 1.5 : st->gt) - dt;
  if (st->gt < 0) {
    st->gt = 2.5 + RND(st) * 4;
    double r = RND(st);
    st->lx = (int8_t)(r < 0.3 ? -1 : r < 0.6 ? 1 : 0);
    st->ly = (int8_t)(RND(st) < 0.25 ? -1 : 0);
  }
  *lx = st->lx; *ly = st->ly;
}
static void sparkles(face_t *st, double dt, double rate, double x0, double x1, double y0, double y1, pose_t *p) {
  if (xs_next(&st->rng) < (uint32_t)(dt * rate * 4294967296.0) && st->nl < 24) {    /* st.rnd() < dt * rate */
    double x = x0 + RND(st) * (x1 - x0), y = y0 + RND(st) * (y1 - y0);
    st->l.sp[st->nl].x = (int8_t)jround(x); st->l.sp[st->nl].y = (int8_t)jround(y); st->l.sp[st->nl].n = 0; st->nl++;
  }
  uint8_t n = 0;
  for (uint8_t k = 0; k < st->nl; k++)                              /* (s.t += dt) < 0.45 */
    if (++st->l.sp[k].n <= sizeof spark_tab / sizeof spark_tab[0]) st->l.sp[n++] = st->l.sp[k];
  st->nl = n;
  for (uint8_t k = 0; k < n; k++) {
    uint16_t e = spark_tab[st->l.sp[k].n - 1];
    exq(p, e >> 15 ? G_PLUS : G_DOT, st->l.sp[k].x, st->l.sp[k].y, e & 0x7FFF);
  }
}

void face_init(face_t *f, uint32_t seed) {
  f->t = 0; f->rng = xs_seed(seed);
  f->bt = f->gt = f->bp = f->zt = f->nt = f->pt = NAN;
  f->bl = f->dbl = f->bd = 0;
  f->lx = f->ly = 0; f->was_poked = 0; f->nl = 0;
}

/* FB[beh].update(st, dt, ctx) -> pose */
static void update(face_t *st, uint8_t beh, double dt, const face_ctx_t *c, pose_t *p) {
  const double t = st->t;
  pose_reset(p);
  switch (beh) {
  case F_IDLE: {
    uint8_t blink = blinker(st, dt);
    int lx = 0, ly = 0;
    if (!c->look) glancer(st, dt, &lx, &ly);
    st->bp = (isnan(st->bp) ? 5 : st->bp) - dt;
    if (st->bp < 0) { st->bp = 6 + RND(st) * 6; st->bd = 2; }
    if (st->bd > 0) st->bd -= dt;
    eye(p, blink ? E_CLOSED : c->look ? E_WIDE : E_OPEN); p->lx = lx; p->ly = ly;
    p->mouth = c->mood > 0.75 ? M_CAT : c->mood < 0.3 ? M_FLAT : M_SMILE;
    p->blush = 0.2 + (st->bd > 0 ? 0.6 * fsin(M_PI * st->bd / 2) : 0) + (c->look ? 0.3 : 0);
    p->body = 0.05 + 0.02 * fsin(t * 1.3);
    break;
  }
  case F_GIGGLE: {
    uint8_t burst = t - floor(t / 1.6) * 1.6 < 0.9;
    eye(p, E_HAPPY); p->mouth = burst ? M_GRIN : M_CAT;
    p->dy = burst && (int)floor(t * 9) % 2 ? -1 : 0; p->blush = 0.5; p->body = 0.06;
    sparkles(st, dt, 3, -8, 8, 10, 19, p);
    break;
  }
  case F_SLEEP: {
    st->zt = (isnan(st->zt) ? 0.4 : st->zt) - dt;
    if (st->zt < 0 && st->nl < 4) { st->zt = 1.9; st->l.z[st->nl].x = 3; st->l.z[st->nl].y = 11; st->l.z[st->nl].t = 0; st->nl++; }
    else if (st->zt < 0) st->zt = 1.9;
    uint8_t n = 0;
    for (uint8_t k = 0; k < st->nl; k++) { st->l.z[k].t += dt; if (st->l.z[k].t < 3.2) st->l.z[n++] = st->l.z[k]; }
    st->nl = n;
    double breath = 0.5 + 0.5 * fsin(t * 2 * M_PI / 4.8);
    uint8_t dream = fsin(t * 0.23) > 0.8;
    eye(p, E_SLEEP); p->mouth = dream ? M_CAT : M_SMALL; p->mouth_lvl = 0.6; p->lvl = 0.7 + 0.25 * breath;
    p->blush = 0.15; p->body = 0.02 + 0.05 * breath;
    for (uint8_t k = 0; k < n; k++) {
      double zt = st->l.z[k].t;
      ex(p, zt < 1.7 ? G_Z : G_DOT, st->l.z[k].x + zt * 0.9, st->l.z[k].y - zt * 1.6, zt < 2.4 ? 0.75 : 0.75 * (3.2 - zt) / 0.8);
    }
    break;
  }
  case F_STIR:
    if (t < 0.7) { p->eyeL = E_LID; p->eyeR = E_SLEEP; p->mouth = M_POUT; p->body = 0.03; }
    else if (t < 1.5) { eye(p, E_CLOSED); p->mouth = M_WAVY; p->dx = fsin(t * 20) > 0 ? 1 : 0; p->body = 0.03; }
    else { eye(p, E_SLEEP); p->mouth = M_SMALL; p->mouth_lvl = 0.6; p->dx = 1; p->body = 0.03; p->lvl = 0.8; }
    break;
  case F_WINK:
    if (t > 0.8 && t < 1.7) {
      double k = fsin(M_PI * (t - 0.8) / 0.9);
      static const int8_t pts[5][2] = { {0, 1}, {-1, 2}, {1, 2}, {0, 2}, {0, 3} };
      for (int j = 0; j < 5; j++) ex(p, G_DOT, pts[j][0], pts[j][1], k);
    }
    p->eyeL = E_OPEN; p->eyeR = t > 0.8 && t < 1.5 ? E_HAPPY : E_OPEN; p->mouth = t > 0.8 ? M_GRIN : M_SMILE;
    p->blush = t > 0.8 ? 0.45 : 0.15; p->body = 0.05;
    break;
  case F_SING: {
    st->nt = (isnan(st->nt) ? 0 : st->nt) - dt;
    if (st->nt < 0) {
      st->nt = 0.9;
      double x = RND(st) < 0.5 ? -6 : 4, ph = RND(st) * 6;
      if (st->nl < 4) { st->l.nt_[st->nl].x = x; st->l.nt_[st->nl].y = 12; st->l.nt_[st->nl].t = 0; st->l.nt_[st->nl].ph = ph; st->nl++; }
    }
    uint8_t n = 0;
    for (uint8_t k = 0; k < st->nl; k++) { st->l.nt_[k].t += dt; if (st->l.nt_[k].t < 2.6) st->l.nt_[n++] = st->l.nt_[k]; }
    st->nl = n;
    int beat = (int)floor(t * 3.2) % 4;
    eye(p, E_HAPPY); p->mouth = beat % 2 == 0 ? M_O : M_SMALL; p->dx = jround(fsin(t * 1.6)); p->blush = 0.3; p->body = 0.06;
    for (uint8_t k = 0; k < n; k++) {
      double nt = st->l.nt_[k].t;
      ex(p, G_NOTE, st->l.nt_[k].x + fsin(nt * 3 + st->l.nt_[k].ph) * 0.8, st->l.nt_[k].y - nt * 1.8, nt < 2 ? 0.8 : 0.8 * (2.6 - nt) / 0.6);
    }
    break;
  }
  case F_SNEEZE:
    if (t < 3.1) ex(p, G_DOT, jround(fsin(t * 2.2) * 1.2), 1 + t * (15 / 3.1), 1);
    else if (t < 4.3) ex(p, G_DOT, 0, 16, 1);
    if (t >= 4.3 && t < 5.3) {
      double k = (t - 4.3) / 1.0;
      for (int a = 0; a < 12; a++) {
        double g = a / 12.0 * 2 * M_PI;
        ex(p, G_DOT, fcos(g) * (1 + k * 9), 16 + fsin(g) * (1 + k * 6), 1 - k);
      }
    }
    if (t < 3.0) { eye(p, E_OPEN); p->ly = t < 2.3 ? -1 : 0; p->mouth = M_O; p->body = 0.05; }
    else if (t < 4.3) { eye(p, E_SQUINT); p->mouth = M_WAVY; p->dx = jround((RND(st) - 0.5) * 1.3); p->body = 0.05; }
    else if (t < 4.8) { eye(p, E_SQUINT); p->mouth = M_YAWN; p->dy = -1; p->body = 0.22; }
    else { eye(p, E_HAPPY); p->mouth = M_GRIN; p->blush = 0.5; p->body = 0.06; }
    break;
  case F_DOZE:
    if (t < 1.2) { eye(p, E_OPEN); p->mouth = M_SMALL; p->body = 0.05; }
    else if (t < 2.5) { eye(p, E_LID); p->mouth = M_SMALL; p->body = 0.04; }
    else if (t < 3.8) { eye(p, E_SLEEP); p->mouth = M_SMALL; p->dy = 1; p->body = 0.035; }
    else if (t < 4.4) { eye(p, E_WIDE); p->mouth = M_O; p->dy = -1; p->body = 0.08; }
    else if (t < 5.6) { eye(p, E_OPEN); p->lx = t < 5 ? -1 : 1; p->mouth = M_FLAT; p->body = 0.05; }
    else if (t < 6.6) { eye(p, E_LID); p->mouth = M_SMALL; p->body = 0.04; }
    else { eye(p, E_SLEEP); p->mouth = M_SMALL; p->mouth_lvl = 0.6; p->dy = 1; p->body = 0.03; p->lvl = 0.8; }
    break;
  case F_GRUMPY: {
    st->pt = (isnan(st->pt) ? 1.5 : st->pt) - dt;
    if (st->pt < 0 || (c->poked && !st->was_poked)) { st->pt = 3 + RND(st) * 2; if (st->nl < 8) st->l.puff[st->nl++] = 0; }
    st->was_poked = c->poked;
    uint8_t n = 0;
    for (uint8_t k = 0; k < st->nl; k++) { st->l.puff[k] += dt; if (st->l.puff[k] < 0.8) st->l.puff[n++] = st->l.puff[k]; }
    st->nl = n;
    eye(p, c->poked ? E_SQUINT : E_LID); p->lx = -1; p->mouth = M_POUT; p->dx = -2; p->body = 0.03;
    for (uint8_t k = 0; k < n; k++) { double pt = st->l.puff[k]; ex(p, G_PUFF, -7 - pt * 1.5, 17 - pt * 2, 1 - pt / 0.8); }
    break;
  }
  case F_EXCITED:
    sparkles(st, dt, 6, -9, 9, 9, 20, p);
    if ((int)floor(t * 4) % 2 == 0) { static const int8_t ys[4] = { 0, 1, 2, 4 }; for (int j = 0; j < 4; j++) ex(p, G_DOT, 0, 7 + ys[j], 1); }
    eye(p, (int)floor(t * 6) % 3 == 0 ? E_SPARK : E_WIDE); p->mouth = M_GRIN; p->dy = (int)floor(t * 10) % 2 ? -1 : 0;
    p->blush = 0.45; p->body = 0.09;
    break;
  case F_SURPRISED:
    if (t < 0.55) { eye(p, E_WIDE); p->mouth = M_O; p->dy = -1; p->body = 0.08; }
    else { eye(p, E_OPEN); p->lx = 1; p->mouth = M_WAVY; p->body = 0.06; ex(p, G_DROP, 7, 12 + fmin(2, (t - 0.55) * 3), 0.9); }
    break;
  case F_MORNING:
    if (t < 1.0) { eye(p, E_SLEEP); p->mouth = M_SMALL; p->mouth_lvl = 0.5; p->body = 0.02 + t * 0.02; p->lvl = 0.6; }
    else if (t < 1.8) { eye(p, E_LID); p->mouth = M_SMALL; p->body = 0.04; }
    else if (t < 3.4) { eye(p, E_CLOSED); p->mouth = M_YAWN; p->dy = -1; p->body = 0.05; }
    else if (t < 4.4) { eye(p, t > 3.9 && t < 4.02 ? E_CLOSED : E_OPEN); p->mouth = M_SMILE; p->body = 0.06; p->blush = 0.2; }
    else { eye(p, E_HAPPY); p->mouth = M_GRIN; p->body = 0.06; p->blush = 0.3; ex(p, G_PLUS, 0, 12 - (t - 4.4) / 1.8 * 10, 1); }
    break;
  case F_PET:
    eye(p, E_HAPPY); p->mouth = M_CAT; p->blush = 0.65 + 0.3 * fsin(t * 3); p->body = 0.07 + 0.02 * fsin(t * 2);
    break;
  case F_HUG:
    eye(p, E_SQUINT); p->mouth = M_GRIN; p->blush = 1; p->dy = (int)floor(t * 8) % 2 ? -1 : 0; p->body = 0.1;
    sparkles(st, dt, 12, -9, 9, 9, 20, p);
    break;
  case F_SQUIRM:
    eye(p, E_SQUINT); p->mouth = M_WAVY; p->dx = jround(fsin(t * 25)); p->body = 0.04;
    break;
  case F_TICKLED:
    eye(p, t - floor(t / 0.8) * 0.8 < 0.6 ? E_HAPPY : E_SQUINT); p->mouth = M_GRIN; p->dy = (int)floor(t * 12) % 2 ? -1 : 0;
    p->dx = jround(fsin(t * 7)); p->blush = 0.7; p->body = 0.08;
    sparkles(st, dt, 9, -9, 9, 9, 20, p);
    break;
  case F_PEEK: {
    uint8_t b = blinker(st, dt);
    eye(p, b ? E_CLOSED : E_OPEN); p->lx = t < 1.1 ? 1 : 0; p->mouth = M_SMALL; p->blush = 0.3;
    break;
  }
  case F_YAWN:
    eye(p, E_CLOSED); p->mouth = M_YAWN;
    break;
  }
}

/* stepFace(): update, render, ease fbuf toward the target (k = min(1, dt * 22)) */
uint8_t face_step(face_t *st, uint8_t beh, double dt, const face_ctx_t *c, uint16_t *fbuf) {
  static pose_t p;
  update(st, beh, dt, c, &p);
  st->t += dt;
  render_pose(&p);
  q12 k = dq(fmin(1, dt * 22));
  for (int i = 0; i < NLED; i++) fbuf[i] = (uint16_t)(fbuf[i] + qm((q12)tgt[i] - fbuf[i], k));
  return face_dur[beh] >= 0 && st->t >= face_dur[beh];
}
