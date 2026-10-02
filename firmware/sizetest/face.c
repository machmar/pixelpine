/* Pine's face: bitmap parts + 18 behaviours, integer port of the simulator's FB table.
 * Grid units (gx, gy); eyes on rows 14-15, mouth 17-18. NOT tested on hardware. */
#include "pp.h"

#define AX 0
#define AY 15
/* eye shapes: 3x4 box, x = -5..-3, y = -2..1 (left eye; right eye mirrored) */
#define E(x, y) (1u << (((y) + 2) * 3 + ((x) + 5)))
enum { EY_OPEN, EY_WIDE, EY_HAPPY, EY_SLEEP, EY_CLOSED, EY_LID, EY_SQUINT, EY_DOT, EY_SPARK };
static const uint16_t eyes[] = {
  E(-4,-1)|E(-3,-1)|E(-4,0)|E(-3,0),
  E(-4,-2)|E(-3,-2)|E(-4,-1)|E(-3,-1)|E(-4,0)|E(-3,0),
  E(-5,0)|E(-4,-1)|E(-3,0),
  E(-5,-1)|E(-4,0)|E(-3,-1),
  E(-5,0)|E(-4,0)|E(-3,0),
  E(-5,-1)|E(-4,-1)|E(-3,-1)|E(-4,0)|E(-3,0),
  E(-5,-1)|E(-4,0)|E(-5,1),
  E(-4,0),
  E(-4,-2)|E(-5,-1)|E(-4,-1)|E(-3,-1)|E(-4,0),
};
#define GAZE_MASK ((1u << EY_OPEN) | (1u << EY_WIDE) | (1u << EY_DOT) | (1u << EY_LID) | (1u << EY_SPARK))
/* mouths: 5x3 box, x = -2..2, y = 1..3 */
#define MO(x, y) (1u << (((y) - 1) * 5 + ((x) + 2)))
enum { M_SMILE, M_SMALL, M_GRIN, M_CAT, M_O, M_YAWN, M_FLAT, M_POUT, M_WAVY };
static const uint16_t mouths[] = {
  MO(-2,2)|MO(-1,3)|MO(0,3)|MO(1,3)|MO(2,2),
  MO(-1,2)|MO(0,3)|MO(1,2),
  MO(-2,2)|MO(-1,2)|MO(0,2)|MO(1,2)|MO(2,2)|MO(-1,3)|MO(0,3)|MO(1,3),
  MO(-2,2)|MO(-1,3)|MO(0,2)|MO(1,3)|MO(2,2),
  MO(0,1)|MO(-1,2)|MO(1,2)|MO(0,3),
  MO(-1,1)|MO(0,1)|MO(1,1)|MO(-1,2)|MO(1,2)|MO(-1,3)|MO(0,3)|MO(1,3),
  MO(-1,3)|MO(0,3)|MO(1,3),
  MO(-2,3)|MO(-1,2)|MO(0,2)|MO(1,2)|MO(2,3),
  MO(-2,3)|MO(-1,2)|MO(0,3)|MO(1,2)|MO(2,3),
};
/* glyphs: 3x3 box, x = -1..1, y = -1..1 around the glyph position */
#define GL(x, y) (1u << (((y) + 1) * 3 + ((x) + 1)))
enum { G_Z, G_NOTE, G_PLUS, G_DOT, G_DROP, G_PUFF, G_BANG };
static const uint16_t glyphs[] = {
  GL(-1,-1)|GL(0,-1)|GL(1,-1)|GL(0,0)|GL(-1,1)|GL(0,1)|GL(1,1),
  GL(0,-1)|GL(1,-1)|GL(0,0)|GL(0,1)|GL(-1,1),
  GL(0,-1)|GL(-1,0)|GL(0,0)|GL(1,0)|GL(0,1),
  GL(0,0),
  GL(0,0)|GL(0,1),
  GL(0,0)|GL(-1,-1)|GL(-1,0),
  GL(0,-1)|GL(0,0)|GL(0,1),
};

typedef struct {
  uint8_t eyeL, eyeR, mouth, blush, lvl, mlvl, body;
  int8_t dx, dy, lx, ly;
  uint8_t ng; struct { uint8_t g; int8_t x, y; uint8_t v; } ex[10];
} pose_t;

static void px(uint8_t *o, int gx, int gy, uint8_t v) { int i = led_at(gx, gy); if (i >= 0 && v > o[i]) o[i] = v; }
static void draw(uint8_t *o, const pose_t *p) {
  if (p->body) for (uint8_t i = 0; i < NLED; i++) o[i] = max8(o[i], scale8(p->body, (uint8_t)(166 + (89 - (LV(i) * 89 >> 8)))));
  int ax = AX + p->dx, ay = AY + p->dy;
  for (int8_t side = 1; side >= -1; side -= 2) {
    uint8_t sh = side > 0 ? p->eyeL : p->eyeR;
    uint16_t m = eyes[sh];
    int lx = (GAZE_MASK >> sh) & 1 ? p->lx : 0, ly = (GAZE_MASK >> sh) & 1 ? p->ly : 0;
    for (int y = 0; y < 4; y++) for (int x = 0; x < 3; x++, m >>= 1)
      if (m & 1) px(o, ax + side * (x - 5) + lx, ay + y - 2 + ly, p->lvl);
  }
  uint16_t m = mouths[p->mouth];
  uint8_t mv = scale8(p->lvl, p->mlvl);
  for (int y = 1; y <= 3; y++) for (int x = -2; x <= 2; x++, m >>= 1) if (m & 1) px(o, ax + x, ay + y, mv);
  if (p->blush) for (int s = -1; s <= 1; s += 2) { px(o, ax + s * 6, ay + 2, scale8(p->blush, 128)); px(o, ax + s * 5, ay + 2, scale8(p->blush, 128)); }
  for (uint8_t k = 0; k < p->ng; k++) {
    uint16_t g = glyphs[p->ex[k].g];
    for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++, g >>= 1)
      if (g & 1) px(o, p->ex[k].x + x, p->ex[k].y + y, scale8(p->ex[k].v, p->lvl));
  }
}
static void add(pose_t *p, uint8_t g, int x, int y, uint8_t v) {
  if (p->ng < 10) { p->ex[p->ng].g = g; p->ex[p->ng].x = (int8_t)x; p->ex[p->ng].y = (int8_t)y; p->ex[p->ng].v = v; p->ng++; }
}

static uint8_t blink(face_state_t *f) {                      /* 1 while the eyes are shut */
  if (!f->bt) f->bt = 50 + (rnd8() % 175);
  if (--f->bt == 0) f->bl = 7;
  if (f->bl) { f->bl--; return 1; }
  return 0;
}
static void glance(face_state_t *f) {
  if (f->gt) { f->gt--; return; }
  f->gt = 125 + (rnd8() % 200);
  uint8_t r = rnd8();
  f->lx = r < 77 ? 0xFF : r < 154 ? 1 : 0;
  f->ly = rnd8() < 64 ? 0xFF : 0;
}
static void sparkles(face_state_t *f, pose_t *p, uint8_t rate) {   /* up to 8 short-lived sparkles */
  if (rnd8() < rate) for (uint8_t k = 0; k < 8; k++) if (!f->part[k].t) {
    f->part[k].x = (int8_t)(rnd8() % 17) - 8; f->part[k].y = (int8_t)(9 + rnd8() % 11); f->part[k].t = 22; break;
  }
  for (uint8_t k = 0; k < 8; k++) if (f->part[k].t) {
    f->part[k].t--;
    add(p, f->part[k].t > 6 && f->part[k].t < 16 ? G_PLUS : G_DOT, f->part[k].x, f->part[k].y, (uint8_t)(f->part[k].t * 11));
  }
}
static void floaters(face_state_t *f, pose_t *p, uint8_t g, uint8_t every, int x0, int y0, uint8_t life) {
  if (++f->sp_t >= every) { f->sp_t = 0; for (uint8_t k = 0; k < 8; k++) if (!f->part[k].t) { f->part[k].x = (int8_t)x0; f->part[k].y = (int8_t)y0; f->part[k].t = life; break; } }
  for (uint8_t k = 0; k < 8; k++) if (f->part[k].t) {
    uint8_t age = life - --f->part[k].t;
    add(p, age < life / 2 ? g : G_DOT, f->part[k].x + age / 55 + (g == G_NOTE ? (sin8((uint8_t)(age * 9 + k * 40)) >> 6) : 0), f->part[k].y - age / 31,
        f->part[k].t < 40 ? (uint8_t)(f->part[k].t * 5) : 200);
  }
}

static const uint16_t durations[F_COUNT] = {
  [F_STIR] = 120, [F_WINK] = 140, [F_SNEEZE] = 375, [F_DOZE] = 400, [F_EXCITED] = 130, [F_SURPRISED] = 70,
  [F_MORNING] = 310, [F_HUG] = 110, [F_SQUIRM] = 75, [F_TICKLED] = 150, [F_PEEK] = 110, [F_YAWN] = 90,
};

void face_start(face_state_t *f, uint8_t beh) {
  uint8_t *b = (uint8_t *)f; for (uint8_t i = 0; i < sizeof *f; i++) b[i] = 0;
  f->beh = beh;
}

uint8_t face_frame(face_state_t *f, uint8_t *out, uint8_t lvl) {
  pose_t p = { .eyeL = EY_OPEN, .eyeR = EY_OPEN, .mouth = M_SMILE, .lvl = 255, .mlvl = 255, .body = 13 };
  uint16_t t = f->t++;
  switch (f->beh) {
  case F_IDLE: {
    uint8_t b = blink(f); glance(f);
    p.eyeL = p.eyeR = b ? EY_CLOSED : EY_OPEN; p.lx = (int8_t)f->lx; p.ly = (int8_t)f->ly;
    p.blush = 51 + (t % 450 < 100 ? usin8((uint8_t)(t * 128 / 100)) >> 1 : 0);
    p.body = (uint8_t)(13 + (sin8((uint8_t)(t * 2)) >> 5));
    break; }
  case F_GIGGLE: {
    uint8_t burst = t % 80 < 45;
    p.eyeL = p.eyeR = EY_HAPPY; p.mouth = burst ? M_GRIN : M_CAT; p.dy = (int8_t)(burst && ((t / 5) & 1) ? -1 : 0);
    p.blush = 128; p.body = 15; sparkles(f, &p, 15);
    break; }
  case F_SLEEP: {
    uint8_t br = usin8((uint8_t)(t * 256 / 240));
    p.eyeL = p.eyeR = EY_SLEEP; p.mouth = (t % 1400) > 1200 ? M_CAT : M_SMALL; p.mlvl = 153;
    p.lvl = (uint8_t)(179 + (br >> 2)); p.blush = 38; p.body = (uint8_t)(5 + (br >> 4));
    floaters(f, &p, G_Z, 95, 4, 12, 160);
    break; }
  case F_STIR:
    if (t < 35) { p.eyeL = EY_LID; p.eyeR = EY_SLEEP; p.mouth = M_POUT; }
    else if (t < 75) { p.eyeL = p.eyeR = EY_CLOSED; p.mouth = M_WAVY; p.dx = (int8_t)((t >> 1) & 1); }
    else { p.eyeL = p.eyeR = EY_SLEEP; p.mouth = M_SMALL; p.mlvl = 153; p.dx = 1; p.lvl = 204; }
    p.body = 8; break;
  case F_WINK:
    p.eyeR = (t > 40 && t < 75) ? EY_HAPPY : EY_OPEN; p.mouth = t > 40 ? M_GRIN : M_SMILE; p.blush = t > 40 ? 115 : 38;
    if (t > 40 && t < 85) { uint8_t k = usin8((uint8_t)((t - 40) * 3)); add(&p, G_PLUS, 0, 2, k); }
    break;
  case F_SING: {
    uint8_t beat = (uint8_t)((t * 16 / 250) & 3);
    p.eyeL = p.eyeR = EY_HAPPY; p.mouth = (beat & 1) ? M_SMALL : M_O; p.dx = (int8_t)(sin8((uint8_t)(t * 2)) >> 6);
    p.blush = 77; p.body = 15; floaters(f, &p, G_NOTE, 45, (t / 45) & 1 ? -5 : 5, 12, 130);
    break; }
  case F_SNEEZE:
    if (t < 155) add(&p, G_DOT, sin8((uint8_t)(t * 2)) >> 6, 1 + t / 10, 255);
    else if (t < 215) add(&p, G_DOT, 0, 16, 255);
    if (t >= 215 && t < 265) for (uint8_t a = 0; a < 8; a++) {
      uint8_t k = (uint8_t)(t - 215), ang = (uint8_t)(a * 32);
      add(&p, G_DOT, (sin8((uint8_t)(ang + 64)) * (1 + k / 5)) >> 7, 16 + ((sin8(ang) * (1 + k / 7)) >> 7), (uint8_t)(255 - k * 5));
    }
    if (t < 150) { p.ly = t < 115 ? -1 : 0; p.mouth = M_O; }
    else if (t < 215) { p.eyeL = p.eyeR = EY_SQUINT; p.mouth = M_WAVY; p.dx = (int8_t)((rnd8() % 3) - 1); }
    else if (t < 240) { p.eyeL = p.eyeR = EY_SQUINT; p.mouth = M_YAWN; p.dy = -1; p.body = 56; }
    else { p.eyeL = p.eyeR = EY_HAPPY; p.mouth = M_GRIN; p.blush = 128; }
    break;
  case F_DOZE:
    if (t < 60) p.mouth = M_SMALL;
    else if (t < 125) { p.eyeL = p.eyeR = EY_LID; p.mouth = M_SMALL; }
    else if (t < 190) { p.eyeL = p.eyeR = EY_SLEEP; p.mouth = M_SMALL; p.dy = 1; }
    else if (t < 220) { p.eyeL = p.eyeR = EY_WIDE; p.mouth = M_O; p.dy = -1; p.body = 20; }
    else if (t < 280) { p.lx = t < 250 ? -1 : 1; p.mouth = M_FLAT; }
    else if (t < 330) { p.eyeL = p.eyeR = EY_LID; p.mouth = M_SMALL; }
    else { p.eyeL = p.eyeR = EY_SLEEP; p.mouth = M_SMALL; p.mlvl = 153; p.dy = 1; p.lvl = 204; }
    break;
  case F_GRUMPY:
    p.eyeL = p.eyeR = EY_LID; p.lx = -1; p.mouth = M_POUT; p.dx = -2; p.body = 8;
    floaters(f, &p, G_PUFF, 170, -7, 17, 40);
    break;
  case F_EXCITED:
    p.eyeL = p.eyeR = (t % 25) < 8 ? EY_SPARK : EY_WIDE; p.mouth = M_GRIN; p.dy = (int8_t)((t / 5) & 1 ? -1 : 0);
    p.blush = 115; p.body = 23;
    if ((t / 12) & 1) { add(&p, G_BANG, 0, 8, 255); add(&p, G_DOT, 0, 11, 255); }
    sparkles(f, &p, 30);
    break;
  case F_SURPRISED:
    if (t < 28) { p.eyeL = p.eyeR = EY_WIDE; p.mouth = M_O; p.dy = -1; p.body = 20; }
    else { p.lx = 1; p.mouth = M_WAVY; add(&p, G_DROP, 7, 12 + (t - 28) / 16, 230); }
    break;
  case F_MORNING:
    if (t < 50) { p.eyeL = p.eyeR = EY_SLEEP; p.mouth = M_SMALL; p.mlvl = 128; p.lvl = 153; p.body = (uint8_t)(5 + t / 10); }
    else if (t < 90) { p.eyeL = p.eyeR = EY_LID; p.mouth = M_SMALL; }
    else if (t < 170) { p.eyeL = p.eyeR = EY_CLOSED; p.mouth = M_YAWN; p.dy = -1; }
    else if (t < 220) { p.eyeL = p.eyeR = (t > 195 && t < 201) ? EY_CLOSED : EY_OPEN; p.blush = 51; }
    else { p.eyeL = p.eyeR = EY_HAPPY; p.mouth = M_GRIN; p.blush = 77; add(&p, G_PLUS, 0, 12 - (t - 220) / 9, 255); }
    break;
  case F_PET:
    p.eyeL = p.eyeR = EY_HAPPY; p.mouth = M_CAT; p.blush = (uint8_t)(166 + (sin8((uint8_t)(t * 4)) >> 1)); p.body = 18;
    break;
  case F_HUG:
    p.eyeL = p.eyeR = EY_SQUINT; p.mouth = M_GRIN; p.blush = 255; p.dy = (int8_t)((t / 6) & 1 ? -1 : 0); p.body = 26;
    sparkles(f, &p, 60); break;
  case F_SQUIRM:
    p.eyeL = p.eyeR = EY_SQUINT; p.mouth = M_WAVY; p.dx = (int8_t)(sin8((uint8_t)(t * 32)) >> 6); p.body = 10; break;
  case F_TICKLED:
    p.eyeL = p.eyeR = (t % 40) < 30 ? EY_HAPPY : EY_SQUINT; p.mouth = M_GRIN; p.dy = (int8_t)((t / 4) & 1 ? -1 : 0);
    p.dx = (int8_t)(sin8((uint8_t)(t * 9)) >> 6); p.blush = 179; p.body = 20; sparkles(f, &p, 45); break;
  case F_PEEK: {
    uint8_t b = blink(f); p.eyeL = p.eyeR = b ? EY_CLOSED : EY_OPEN; p.lx = t < 55 ? 1 : 0; p.mouth = M_SMALL; p.blush = 77; p.body = 0;
    break; }
  case F_YAWN:
    p.eyeL = p.eyeR = EY_CLOSED; p.mouth = M_YAWN; p.body = 0; break;
  }
  p.lvl = scale8(p.lvl, lvl);
  draw(out, &p);
  return durations[f->beh] && f->t >= durations[f->beh];
}
