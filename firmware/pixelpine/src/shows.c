/* The 15 light shows: ports of ANIMS[] in simulator/index.html, one case per show, same order and formulas.
 * Per-frame scalars are doubles computed exactly as in the JS; per-LED maths is Q12 (fx.h); s->rng is s.rnd()
 * with the same seed and the same call order, so the random choices match the simulator draw for draw.
 * Shows whose JS keeps a separate `s.buf` and outputs max(floor, buf) keep buf in `out` and apply the floor in
 * show_px() (saves 480 B of RAM per show). */
#include <math.h>
#include <string.h>
#include "pine.h"

#define RND(s) xs_d(&(s)->rng)
static inline uint32_t fturn_d(double f) { return (uint32_t)(int64_t)(f * 4294967296.0); }   /* fraction of a turn */
static inline q12 lerp16(q12 v) { return v > ONE ? ONE : v; }

/* ---------- splat: soft "light blob" onto the LED grid (positions mm Q12, radius <= 9 mm) ---------- */
__attribute__((optimize("O2"))) static void blob(uint16_t *buf, int32_t x, int32_t y, q12 amp, q12 r, uint8_t add) {
  /* only the grid cells whose LED can be closer than r: LX = gx * P, LY = YBASE + gy * P */
  int32_t g0 = ((x - r) >> 4) * INV_PITCH_Q16 >> 24, g1 = (((x + r) >> 4) * INV_PITCH_Q16 >> 24) + 1;
  int32_t h0 = ((y - r - LED_YBASE_Q) >> 4) * INV_PITCH_Q16 >> 24, h1 = (((y + r - LED_YBASE_Q) >> 4) * INV_PITCH_Q16 >> 24) + 1;
  if (g0 < GXMIN) g0 = GXMIN;
  if (g1 > GXMAX) g1 = GXMAX;
  if (h0 < GYMIN) h0 = GYMIN;
  if (h1 > GYMAX) h1 = GYMAX;
  uint32_t rr = (uint32_t)(r >> 1) * (uint32_t)(r >> 1);
  uint32_t sh = 0;
  while ((rr >> sh) >= 65536u) sh++;
  uint32_t inv = (1u << 30) / (rr >> sh);               /* (d2 >> sh) * inv >> 16 = d2 / rr in Q14 */
  for (int32_t gy = h0; gy <= h1; gy++) {
    int32_t dy = LED_YBASE_Q + ((gy * PITCH_Q4) >> 2) - y;
    if (dy >= r || dy <= -r) continue;
    const uint8_t *row = grid + (gy - GYMIN) * GW - GXMIN;
    for (int32_t gx = g0; gx <= g1; gx++) {
      int i = row[gx];
      if (i == 255) continue;
      int32_t dx = ((gx * PITCH_Q4) >> 2) - x;
      if (dx >= r || dx <= -r) continue;
      uint32_t d2 = (uint32_t)((dx >> 1) * (dx >> 1)) + (uint32_t)((dy >> 1) * (dy >> 1));
      if (d2 >= rr) continue;
      uint32_t x14 = ((d2 >> sh) * inv) >> 16;          /* (d/r)^2, Q14 */
      q12 ff;
      if (x14 < 64) {                                    /* centre: (1 - sqrt x)^2 is too steep for the table */
        q12 f = ONE - (q12)(((int32_t)(isqrt32(d2) << 1) << 12) / r);
        ff = qm(f, f);
      } else {
        uint32_t k = x14 >> 6, fr = x14 & 63u;
        ff = splat_tab[k] - (q12)((((int32_t)splat_tab[k] - splat_tab[k + 1]) * (int32_t)fr) >> 6);
      }
      q12 v = qm(amp, ff);
      if (add) { q12 s = buf[i] + v; buf[i] = (uint16_t)(s > ONE ? ONE : s); }
      else if (v > buf[i]) buf[i] = (uint16_t)v;
    }
  }
}
void splat(uint16_t *buf, int32_t x, int32_t y, q12 amp, q12 r) { blob(buf, x, y, amp, r, 0); }
void add_splat(uint16_t *buf, int32_t x, int32_t y, q12 amp, q12 r) { blob(buf, x, y, amp, r, 1); }

static void decay_buf(uint16_t *buf, double k) {
  uint32_t m = (uint32_t)(k * 65536.0 + 0.5);
  for (int i = 0; i < NLED; i++) buf[i] = (uint16_t)((buf[i] * m + 32768u) >> 16);
}
static double smooth_d(double e0, double e1, double x) {
  double t = (x - e0) / (e1 - e0);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}
/* first frame n with n * dt > x (the JS adds dt to p.age every frame and compares) */
static int age_frames(double x) {
  int n = (int)(x / FW_DT);
  while (n > 0 && (n - 1) * FW_DT > x) n--;
  while (n * FW_DT <= x) n++;
  return n;
}
static inline q12 pow_n(q12 x, int n) { q12 r = ONE; while (n--) r = qm(r, x); return r; }

void show_init(show_t *s, uint8_t k, uint32_t seed) {
  s->k = k; s->rng = xs_seed(seed); s->t = 0; s->n = 0;
  for (unsigned b = 0; b < sizeof s->u; b++) ((uint8_t *)&s->u)[b] = 0;
  switch (k) {
  case 0: for (int i = 0; i < NLED; i++) s->u.tw[i] = 255; break;            /* ph = -1: idle */
  case 6:
    for (int j = 0; j < 7; j++) {
      double x = (RND(s) * 2 - 1) * 8, y = LED_Y0 + 12 + RND(s) * 28, h = RND(s) * 6.28;
      double sp = 5 + RND(s) * 3, ph = RND(s) * 6.28, w = 1.5 + RND(s) * 1.5;
      s->u.ff[j].x = dq16(x); s->u.ff[j].y = dq16(y); s->u.ff[j].h = dturn(h); s->u.ff[j].sp = dq16(sp * FW_DT);
      s->u.ff[j].ny = dq16(ph * 10); s->u.ff[j].ph = dturn(ph); s->u.ff[j].w = dturn(w * FW_DT);
    }
    break;
  case 10: s->u.fw.next = 0.3; break;
  case 11:
    for (int j = 0; j < 9; j++) {
      s->u.orn.i[j] = (uint8_t)(((uint64_t)xs_next(&s->rng) * NLED) >> 32);
      s->u.orn.t0[j] = -RND(s) * 5; s->u.orn.hold[j] = 2 + RND(s) * 3;
    }
    break;
  }
}

q12 show_px(const show_t *s, const uint16_t *out, int i) {
  q12 v = out[i];
  switch (s->k) {
  case 6: return qmax(QC(0.02), v);
  case 7: return qmax(v, QC(0.025) + qm(QC(0.07), qm(led_v[i], led_v[i])));
  case 10: return qmax(QC(0.015), v);
  default: return v;
  }
}

void show_frame(show_t *s, uint16_t *out) {
  const double t = s->t, dt = FW_DT;
  switch (s->k) {
  case 0: /* Twinkle */
    for (int i = 0; i < NLED; i++) {
      uint32_t w = s->u.tw[i], n = w & 255u;
      if (n == 255) {
        if (xs_next(&s->rng) < 38654706u) {                          /* s.rnd() < dt * 0.45 */
          float dur = (float)(0.9 + RND(s) * 1.8);
          double pk = 0.45 + RND(s) * 0.55;
          uint32_t db, lo = 0, hi = 140;
          memcpy(&db, &dur, 4);
          while (lo < hi) { uint32_t m = (lo + hi) >> 1; if (tw_ph[m] < db) lo = m + 1; else hi = m; }
          uint32_t nend = lo + 1;                                       /* first frame with ph / dur >= 1 */
          s->u.tw[i] = (uint32_t)(dt / dur * 65536.0 + 0.5) << 8 | (uint32_t)(dq(pk) - 1843) << 19;
          s->u.twe[i] = (uint8_t)nend;
        }
        out[i] = QC(0.05);
      } else {
        n++;
        uint32_t inc = (w >> 8) & 0x7FFu, x16 = n * inc;
        if (n >= s->u.twe[i]) { s->u.tw[i] = 255; out[i] = QC(0.05); }
        else {
          q12 e = qsin(x16 << 15), pk = (q12)(w >> 19) + 1843;       /* jsin(pi * x) */
          out[i] = (uint16_t)lerp16(QC(0.05) + qm(pk, qm(e, e)));
          s->u.tw[i] = (w & ~255u) | n;
        }
      }
    }
    break;

  case 1: { /* Snowfall: flake y in Q16 mm, fall per frame in Q16 */
    typeof(s->u.sn) *S = &s->u.sn;
    for (int i = 0; i < NLED; i++) out[i] = QC(0.02);
    S->spawn += dt * 4.5;
    while (S->spawn >= 1) {
      S->spawn -= 1;
      if (S->nfl < 26) {
        flake_t *f = &S->fl[S->nfl++];
        double x = (RND(s) * 2 - 1) * 21, y = LED_Y0 - 3 - RND(s) * 5, vy = 4 + RND(s) * 3.5;
        double ph = RND(s) * 6.28, sw = 0.6 + RND(s) * 1.2;
        f->x = dq16(x); f->y = dq16(y); f->vy = dq16(vy * dt); f->ph = dturn(ph); f->sw = dq(sw);
      }
    }
    uint32_t t14 = dturn(t * 1.4);
    for (int k = S->nfl - 1; k >= 0; k--) {                  /* fixed point: double here cost ~5 ms a frame */
      flake_t *f = &S->fl[k];
      f->y += f->vy;
      int32_t x = f->x + ((f->sw * qsin(t14 + f->ph)) >> 8);              /* Q16 */
      int gx = (int)((((x >> 8) * INV_PITCH_Q16) + (1 << 23)) >> 24);   /* Math.round(x / P) */
      int8_t bottom = (gx >= GXMIN && gx <= GXMAX) ? col_bottom[gx - GXMIN] : -1;
      uint8_t gone = 0;
      if (bottom >= 0 && !S->melt) {
        int li = led_at(gx, bottom - S->depth[gx - GXMIN]);
        if (li >= 0 && f->y >= row_y16[led_gy[li] - GYMIN]) {
          S->lit[li >> 3] |= (uint8_t)(1u << (li & 7)); S->depth[gx - GXMIN]++; S->total++; gone = 1;
        }
      }
      if (!gone && f->y > dq16(LED_Y1 + 3)) gone = 1;
      if (gone) { for (int j = k; j < S->nfl - 1; j++) S->fl[j] = S->fl[j + 1]; S->nfl--; continue; }
      splat(out, x >> 4, f->y >> 4, QC(0.95), QC(PITCH * 1.25));
    }
    if (S->total > 70) S->melt = 1;
    if (S->melt) {                              /* every lit LED melts from 1; snow_k = frames to reach 0 */
      uint8_t any = 0;
      S->mk++;
      for (int i = 0; i < NLED; i++)
        if ((S->lit[i >> 3] >> (i & 7)) & 1) { if (S->mk < snow_k[i]) any = 1; else S->lit[i >> 3] &= (uint8_t)~(1u << (i & 7)); }
      if (!any) { S->melt = 0; S->total = 0; S->mk = 0; for (int g = 0; g < GW; g++) S->depth[g] = 0; }
    }
    uint32_t t2 = dturn(t * 2);
    for (int i = 0; i < NLED; i++)
      if ((S->lit[i >> 3] >> (i & 7)) & 1) {
        q12 sv = S->melt ? ONE - (q12)(((int32_t)S->mk << 12) / snow_k[i]) : ONE;   /* ~linear melt */
        q12 v = qm(sv, QC(0.42) + qm(QC(0.06), qsin(t2 + (uint32_t)i * 683565276u)));
        if (v > out[i]) out[i] = (uint16_t)v;
      }
    break;
  }

  case 2: { /* Breathe */
    uint32_t tb = fturn_d(t / 5.2);
    for (int i = 0; i < NLED; i++) {
      q12 c = (ONE - qcos(tb - (uint32_t)led_v[i] * 230687u)) >> 1;
      out[i] = (uint16_t)(QC(0.06) + qm(QC(0.94), qm(qpow16(c), ONE - ((led_v[i] * 901) >> 12))));
    }
    break;
  }

  case 3: { /* Spiral garland */
    uint32_t t11 = fturn_d(t * 0.11), t3 = dturn(t * 3), g9 = dturn(0.5 * 9);
    for (int i = 0; i < NLED; i++) {
      uint32_t th = LTH(i);
      q12 b = QC(0.035), dep = QC(0.45) + qm(QC(0.55), qcos(th));
      for (int g = 0; g < 2; g++) {
        uint32_t ph = (uint32_t)led_v[i] * 2726298u + th - t11 + (g ? 0x80000000u : 0);
        int32_t d = (((int32_t)(ph - 0x80000000u) >> 16) * 63015) >> 16;         /* (ph - 0.5) / 0.065 */
        q12 bulbs = QC(0.65) + qm(QC(0.35), qsin(th * 7 - t3 + (g ? g9 : 0)));
        q12 v = qm(qm(qgauss(d), bulbs), dep);
        b += g ? qm(v, QC(0.55)) : v;
      }
      out[i] = (uint16_t)lerp16(b);
    }
    break;
  }

  case 4: { /* Candle flicker */
    double gust = 0.72 + 0.28 * smooth_d(0.22, 0.6, fxd(noise2(dq16(t * 0.35), dq16(7.3))));
    double y = t * 2.4;
    noise_row_t r0, r1, r2, rg[GH];
    noise_row(&r0, dq16(y)); noise_row(&r1, dq16(y * 2.1 + 5)); noise_row(&r2, dq16(y * 4.3 + 11));
    for (int g = 0; g < GH; g++) noise_row(&rg[g], (int32_t)(((LED_YBASE_Q + (((g + GYMIN) * PITCH_Q4) >> 2)) * 5898LL) >> 12));
    int32_t tr = dq16(t * 0.6);
    q12 gq = dq(gust);
    for (int i = 0; i < NLED; i++) {
      q12 n1 = qm(QC(0.55), noise2r(&r0, (i * 383255) >> 3)) + qm(QC(0.3), noise2r(&r1, ((i * 804834) >> 3) + (17 << 16))) + qm(QC(0.15), noise2r(&r2, ((i * 1647995) >> 3) + (3 << 16)));
      q12 region = QC(0.85) + qm(QC(0.3), noise2r(&rg[led_gy[i] - GYMIN], ((LXQ(i) * 5898) >> 12) + tr) - QC(0.5));
      out[i] = (uint16_t)qclamp(qm(qm(QC(0.22) + qm(QC(0.62), n1), gq), region));
    }
    break;
  }

  case 5: { /* Star ripple */
    const double period = 2.1;
    double kNow = floor(t / period);
    q12 R[4], A[4];
    int nk = 0;
    for (double k = kNow; k > kNow - 4; k--) {
      double age = t - k * period;
      if (age < 0) continue;
      R[nk] = dq(age * 21); A[nk] = qm(qexpn(dq(age / 2.4)), QC(0.95)); nk++;
    }
    for (int i = 0; i < NLED; i++) {
      q12 b = QC(0.035) + qm(QC(0.12), qexpn((LDS(i) * 410) >> 12));          /* exp(-DS / 10) */
      for (int k = 0; k < nk; k++) b += qm(qgauss(((LDS(i) - R[k]) * 1280) >> 12), A[k]);   /* / 3.2 */
      out[i] = (uint16_t)lerp16(b);
    }
    break;
  }

  case 6: { /* Fireflies (buf = out) */
    decay_buf(out, 0.9569539574730467);                       /* exp(-dt * 2.2) */
    int32_t nx = dq16(t * 0.7);
    for (int j = 0; j < 7; j++) {                         /* fixed point: same steering, ~50x cheaper than double */
      typeof(s->u.ff[0]) *f = &s->u.ff[j];
      f->h += (uint32_t)((noise2(nx, f->ny) - 2048) * 20026);          /* (noise - 0.5) * dt * 6 rad, in turns */
      f->x += (qcos(f->h) * f->sp) >> 12; f->y += (qsin(f->h) * f->sp) >> 12;
      int32_t gq = (f->y - dq16(LED_Y0)) + dq16(PITCH / 2);           /* halfWidthAt: Math.round((y - Y0) / P) */
      int g = (gq >= 0 ? gq / dq16(PITCH) : -((-gq + dq16(PITCH) - 1) / dq16(PITCH))) + GYMIN;
      g = g < GYMIN ? GYMIN : g > GYMAX ? GYMAX : g;
      int32_t half = (row_half[g - GYMIN] << 4) - (2 << 16);
      if (f->y < dq16(LED_Y0 + 3) || f->y > dq16(LED_Y1 - 1) || (f->x < 0 ? -f->x : f->x) > half) {
        uint32_t want = qatan2(dq16((LED_Y0 + LED_Y1) / 2 + 6) - f->y, -f->x);
        f->h += (uint32_t)(((int32_t)(want - f->h) >> 10) * 61);                       /* angDiff * dt * 3 */
      }
      q12 e = qsin(s->n * f->w + f->ph);
      q12 blink = QC(0.25) + qm(QC(0.75), e > 0 ? qm(e, e) : 0);
      splat(out, f->x >> 4, f->y >> 4, blink, QC(PITCH * 1.35));
    }
    break;
  }

  case 7: { /* Rising sparks (buf = out); y Q16 mm, vy Q16 mm per frame */
    typeof(s->u.sp) *S = &s->u.sp;
    decay_buf(out, 0.8693582353988059);                       /* exp(-dt * 7) */
    S->spawn += dt * 11;
    while (S->spawn >= 1) {
      S->spawn -= 1;
      double x = (RND(s) * 2 - 1) * ROW_HALF_MAX * 0.85, vy = -(11 + RND(s) * 9), ph = RND(s) * 6.28;
      double a = 0.6 + 0.4 * RND(s), life = 0.45 + 0.55 * RND(s);
      if (S->np < 48) {
        spark_t *p = &S->p[S->np++];
        p->x = dq(x); p->y = dq16(LED_Y1 + 1.5); p->vy = (int16_t)(vy * dt * 65536.0 - 0.5);
        p->ph = (uint16_t)(dturn(ph) >> 16); p->a = (uint16_t)dq(a); p->life = (uint16_t)dq(life);
      }
    }
    uint32_t t5 = dturn(t * 5);
    const int64_t inv = (int64_t)(4294967296.0 / (16.0 * (LED_Y1 - LED_Y0)) + 0.5);
    for (int k = S->np - 1; k >= 0; k--) {
      spark_t *p = &S->p[k];
      p->y += p->vy;
      q12 prog = (q12)(((int64_t)(dq16(LED_Y1) - p->y) * inv) >> 32);
      if (prog > p->life) { for (int j = k; j < S->np - 1; j++) S->p[j] = S->p[j + 1]; S->np--; continue; }
      q12 amp = qm(p->a, qpow12(ONE - (q12)(((int32_t)prog << 12) / p->life)));
      splat(out, p->x + qm(QC(0.9), qsin(t5 + ((uint32_t)p->ph << 16))), p->y >> 4, amp, QC(PITCH * 1.1));
    }
    break;
  }

  case 8: { /* Fill & drain */
    double tc = jfmod(t, 11);
    double L = tc < 4 ? smooth_d(0, 4, tc) : tc < 5.5 ? 1 : tc < 9.5 ? 1 - smooth_d(5.5, 9.5, tc) : 0;
    q12 base = dq(1.04 - L * 1.1);
    uint32_t w1 = dturn(t * 2.6), w2 = dturn(t * 4.1);
    for (int i = 0; i < NLED; i++) {
      q12 surf = base + qm(QC(0.025), qsin((uint32_t)LU(i) * 1168188u + w1)) + qm(QC(0.015), qsin((uint32_t)LU(i) * 2169492u - w2));
      q12 dv = led_v[i] - surf;
      q12 under = qsmooth(((dv + QC(0.015)) * 91022) >> 12);                     /* / 0.045 */
      q12 e = (dv * 146286) >> 12;                                                /* / 0.028 */
      out[i] = (uint16_t)qclamp(QC(0.03) + qm(under, QC(0.6)) + qm(qgauss(e), QC(0.5)));
    }
    break;
  }

  case 9: { /* Northern lights */
    uint32_t t27 = dturn(t * 0.27);
    int32_t x07 = dq16(t * 0.07), x05 = dq16(t * 0.05), y45 = dq16(t * 0.45);
    noise_row_t r09, r13, rr[GH];
    noise_row(&r09, dq16(t * 0.09 + 3.1)); noise_row(&r13, dq16(t * 0.13));
    for (int g = 0; g < GH; g++) {                     /* V of grid row g, as in LV */
      int i0 = grid[g * GW + (0 - GXMIN)] != 255 ? grid[g * GW + (0 - GXMIN)] : 0;
      noise_row(&rr[g], ((led_v[i0] * 224) / 10) - y45);
    }
    int32_t rs[GH];                                     /* 0.35 sin(V * 2.8 + t * 0.27) depends on the row only */
    for (int g = 0; g < GH; g++) {
      int i0 = grid[g * GW + (0 - GXMIN)] != 255 ? grid[g * GW + (0 - GXMIN)] : 0;
      rs[g] = (qsin((uint32_t)led_v[i0] * 467275u + t27) * 22938) >> 12;
    }
    for (int i = 0; i < NLED; i++) {
      int32_t w = ((LU(i) * 256) / 10) + rs[led_gy[i] - GYMIN];          /* U*1.6 + 0.35 sin(..), Q16 */
      q12 r1 = ONE - qabs(2 * noise2r(&r09, w + x07) - ONE);
      q12 r2 = ONE - qabs(2 * noise2r(&r13, ((w * 6963) >> 12) - x05 + (40 << 16)) - ONE);
      q12 rays = QC(0.35) + qm(QC(0.65), noise2r(&rr[led_gy[i] - GYMIN], LU(i) * 104));
      q12 r22 = qm(r2, r2);
      out[i] = (uint16_t)qclamp(QC(0.025) + qm(qm(QC(0.95), qm(qm(r1, r1), r1)) + qm(QC(0.5), qm(r22, r22)), rays));
    }
    break;
  }

  case 10: { /* Fireworks (buf = out); rocket / particle positions Q16 mm, velocities Q16 mm per frame */
    typeof(s->u.fw) *F = &s->u.fw;
    decay_buf(out, 0.835270211411272);                       /* exp(-dt * 9) */
    F->next -= dt;
    if (F->next <= 0) {
      F->next = 1.1 + RND(s) * 1.6;
      double tx = (RND(s) * 2 - 1) * 10, ty = LED_Y0 + 4 + RND(s) * 20;
      if (F->nrk < 4) { F->rk[F->nrk].x = dq16(tx * 0.6); F->rk[F->nrk].y = dq16(LED_Y1 + 2);
                        F->rk[F->nrk].tx = dq16(tx); F->rk[F->nrk].ty = dq16(ty); F->nrk++; }
    }
    for (int k = F->nrk - 1; k >= 0; k--) {
      typeof(F->rk[0]) *r = &F->rk[k];
      r->y -= 44564;                                                              /* 34 mm/s */
      r->x += (r->tx - r->x) * 3 / 100;                                           /* * dt * 1.5 */
      splat(out, r->x >> 4, r->y >> 4, QC(0.8), QC(PITCH * 0.9));
      if (r->y <= r->ty) {
        int n = 18 + (int)(RND(s) * 8);
        for (int j = 0; j < n; j++) {
          uint32_t ra = xs_next(&s->rng), rs = xs_next(&s->rng), rl = xs_next(&s->rng);
          uint32_t at = (uint32_t)(((uint64_t)j << 32) / (uint32_t)n) + (uint32_t)(((uint64_t)ra * 136713055u) >> 32);   /* + rnd * 0.2 rad */
          int32_t spq = 14418 + (int32_t)(((uint64_t)rs * 7864u) >> 32);    /* (11 + rnd * 6) * dt, Q16 mm per frame */
          if (F->npt < 52) {
            fwpt_t *p = &F->pt[F->npt++];
            double life = 1.2 + rl * (1.0 / 4294967296.0) * 0.8;
            p->x = r->x; p->y = r->y; p->age = 0; p->life = (uint16_t)dq(life);
            p->nend = (uint8_t)age_frames(life); p->n06 = (uint8_t)age_frames(0.6 * life);
            p->vx = (int16_t)((qcos(at) * spq + 2048) >> 12); p->vy = (int16_t)((qsin(at) * spq + 2048) >> 12);
          }
        }
        if (F->nfl < 4) { F->fl[F->nfl].x = r->x >> 4; F->fl[F->nfl].y = r->y >> 4; F->fl[F->nfl].a = ONE; F->nfl++; }
        for (int j = k; j < F->nrk - 1; j++) F->rk[j] = F->rk[j + 1];
        F->nrk--;
      }
    }
    int32_t drag = (int32_t)(0.9685065820791976 * 65536.0 + 0.5);   /* exp(-dt * 1.6) */
    for (int k = F->npt - 1; k >= 0; k--) {
      fwpt_t *p = &F->pt[k];
      p->vx = (int16_t)((p->vx * drag + 32768) >> 16);
      p->vy = (int16_t)(((p->vy * drag + 32768) >> 16) + 236);                  /* + 9 mm/s^2 * dt */
      p->x += p->vx; p->y += p->vy; p->age++;
      if (p->age >= p->nend) {                                                    /* age > life */
        for (int j = k; j < F->npt - 1; j++) F->pt[j] = F->pt[j + 1];
        F->npt--; continue;
      }
      q12 u = (q12)(((int32_t)p->age * 335544) / p->life);                /* age / life, Q12 */
      q12 tw = p->age >= p->n06 ? (xs_next(&s->rng) < 0x80000000u ? QC(0.35) : ONE) : ONE;   /* u > 0.6 */
      splat(out, p->x >> 4, p->y >> 4, qm(qpow14(ONE - u), tw), QC(PITCH * 1.0));
    }
    for (int k = F->nfl - 1; k >= 0; k--) {
      add_splat(out, F->fl[k].x, F->fl[k].y, qm(F->fl[k].a, QC(0.35)), QC(9));
      F->fl[k].a -= QC(0.08);
      if (F->fl[k].a <= 0) { for (int j = k; j < F->nfl - 1; j++) F->fl[j] = F->fl[j + 1]; F->nfl--; }
    }
    break;
  }

  case 11: { /* Ornaments */
    typeof(s->u.orn) *O = &s->u.orn;
    uint32_t t25 = dturn(t * 2.5);
    for (int i = 0; i < NLED; i++) {
      uint32_t th = LTH(i), ph = (uint32_t)led_v[i] * 1887437u + 858993459u + th;
      q12 d = (((int32_t)(ph - 0x80000000u) >> 16) * 25) >> 4;                    /* (ph - 0.5) / 0.04 */
      q12 g = (ONE + qsin(th * 10 - t25)) >> 1;
      q12 g2 = qm(g, g), glint = qm(qm(g2, g2), g2);
      out[i] = (uint16_t)(QC(0.03) + qm(qgauss(d), QC(0.1) + qm(QC(0.3), glint)));
    }
    for (int j = 0; j < 9; j++) {
      double age = t - O->t0[j], total = 2.4 + O->hold[j];
      if (age > total) {
        for (int tries = 0; tries < 20; tries++) {          /* keep baubles apart */
          int c = (int)(((uint64_t)xs_next(&s->rng) * NLED) >> 32), ok = 1;
          for (int q = 0; q < 9 && ok; q++) {
            if (q == j) continue;
            int32_t dx = (LXQ(O->i[q]) - LXQ(c)) >> 4, dy = (LYQ(O->i[q]) - LYQ(c)) >> 4;
            if ((uint32_t)(dx * dx + dy * dy) <= 49u * 65536u) ok = 0;
          }
          if (ok) { O->i[j] = (uint8_t)c; break; }
        }
        O->t0[j] = t; O->hold[j] = 2 + RND(s) * 3; age = 0;
      }
      double hold = O->hold[j];
      double env = age < 1.2 ? smooth_d(0, 1.2, age) : age < 1.2 + hold ? 1 : 1 - smooth_d(1.2 + hold, total, age);
      splat(out, LXQ(O->i[j]), LYQ(O->i[j]), dq(env), QC(PITCH * 1.7));
    }
    break;
  }

  case 12: { /* Soft radar */
    double w = 2 * M_PI / 8, beam = 0.95 * jsin(w * t);
    int dir = jcos(w * t) >= 0 ? 1 : -1;
    uint32_t B = dturn(beam);
    for (int i = 0; i < NLED; i++) {
      int32_t dturns = -(int32_t)(B - ra_phi[i]);                                 /* -angDiff(beam, phi) */
      q12 d = ((dturns >> 16) * 25736) >> 16;                                     /* turns -> rad, Q12 */
      q12 e = (d * 9752) >> 12, behind = dir > 0 ? -d : d;                        /* d / 0.42 */
      int32_t bsign = dir > 0 ? -dturns : dturns;                                 /* sign of behind, full precision */
      if (!dturns) {                                                              /* on the beam: decide in double */
        double dd = -(beam - (int32_t)ra_phi[i] / TURN_PER_RAD);
        bsign = (dir > 0 ? -dd : dd) > 0;
      }
      q12 tail = bsign > 0 ? qm(qexpn((behind * 5851) >> 12), QC(0.28)) : 0;     /* exp(-behind / 0.7) */
      out[i] = (uint16_t)qclamp(qm(QC(0.16) + qm(QC(0.5), qgauss(e)) + tail, ONE - ((LDS(i) * 17) >> 12)));   /* 1 - 0.25 DS / 60 */
    }
    break;
  }

  case 13: { /* Heartbeat */
    const double per = 1.25;
    int32_t tm = (int32_t)(jfmod(t, per) * 32768.0 + 0.5), perq = 40960;          /* Q15 */
    for (int i = 0; i < NLED; i++) {
      int32_t tau15 = tm - hb_d55[i];
      if (tau15 < 0) tau15 += perq;
      q12 tau = tau15 >> 3;
      q12 a = ((tau - QC(0.08)) * 58514) >> 12, b = ((tau - QC(0.34)) * 45511) >> 12;
      q12 env = qgauss(a) + qm(QC(0.6), qgauss(b));
      out[i] = (uint16_t)qclamp(QC(0.05) + qm(QC(0.9), qm(env, hb_k[i])));
    }
    break;
  }

  case 14: { /* Tinsel shimmer */
    uint32_t t24 = dturn(t * 2.4), n = s->n;
    for (int i = 0; i < NLED; i++) {
      int gy = led_gy[i];
      q12 a = qsin(((uint32_t)ti_ph[i] << 16) + (gy % 2 ? t24 : -t24));
      q12 glint = 0, sp = 0;
      if (a > 0) { q12 a2 = qm(a, a), a4 = qm(a2, a2), a8 = qm(a4, a4); glint = qm(qm(a8, a4), a2); }   /* ^14 */
      if (gy % 3) glint = qm(glint, QC(0.35));
      q12 b = qsin(n * (34178264u + (uint32_t)(((uint64_t)hash1u((uint32_t)i) * 47849569u) >> 32)) + ((uint32_t)ti_c[i] << 16));
      if (b > 0) { q12 b2 = qm(b, b), b4 = qm(b2, b2), b8 = qm(b4, b4), b16 = qm(b8, b8); sp = qm(qm(b16, b16), b8); }   /* ^40 */
      out[i] = (uint16_t)qclamp(QC(0.06) + qm(QC(0.8), glint) + qm(QC(0.85), sp));
    }
    break;
  }
  }
  s->t += FW_DT;
  s->n++;
}
