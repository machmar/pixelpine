/* The 15 light shows, integer-only ports of the simulator's ANIMS. t = frame counter (50 fps).
 * Positions: *_x4/_y4 = 0.25 mm unless noted. NOT tested on hardware; written to measure size. */
#include "pp.h"

uint8_t scale8(uint8_t a, uint8_t b) { return (uint8_t)(((uint16_t)a * b + a) >> 8); }
static inline int16_t iabs(int16_t v) { return v < 0 ? -v : v; }
static inline uint8_t lerp8(uint8_t a, uint8_t b, uint8_t f) { return (uint8_t)(a + ((((int16_t)b - a) * f) >> 8)); }

uint8_t noise8(uint16_t x, uint16_t y) {
  uint8_t xi = x >> 8, yi = y >> 8, xf = (uint8_t)x, yf = (uint8_t)y;
  uint8_t p0 = perm[xi], p1 = perm[(uint8_t)(xi + 1)];
  uint8_t a = perm[(uint8_t)(p0 + yi)], b = perm[(uint8_t)(p1 + yi)];
  uint8_t c = perm[(uint8_t)(p0 + yi + 1)], d = perm[(uint8_t)(p1 + yi + 1)];
  return lerp8(lerp8(a, b, xf), lerp8(c, d, xf), yf);
}

void splat(uint8_t *buf, int16_t x4, int16_t y4, uint8_t amp, uint8_t r4) {
  int16_t gx0 = (x4 - r4) / PITCH4 - 1, gx1 = (x4 + r4) / PITCH4 + 1;
  int16_t gy0 = (y4 - r4 - LED_Y0_4) / PITCH4 + GYMIN - 1, gy1 = (y4 + r4 - LED_Y0_4) / PITCH4 + GYMIN + 1;
  uint16_t rr = (uint16_t)r4 * r4;
  for (int16_t gy = gy0; gy <= gy1; gy++)
    for (int16_t gx = gx0; gx <= gx1; gx++) {
      int i = led_at(gx, gy);
      if (i < 0) continue;
      int16_t dx = led_x4[i] - x4, dy = (int16_t)led_y4[i] - y4;
      uint16_t d2 = (uint16_t)(dx * dx + dy * dy);
      if (d2 >= rr) continue;
      uint8_t f = (uint8_t)(255 - ((uint32_t)d2 * 255u) / rr);
      uint8_t v = scale8(amp, scale8(f, f));
      if (v > buf[i]) buf[i] = v;
    }
}

static void fade(uint8_t *buf, uint8_t keep) { for (uint8_t i = 0; i < NLED; i++) buf[i] = scale8(buf[i], keep); }
static void zero(void *p, uint16_t n) { uint8_t *b = p; while (n--) *b++ = 0; }

void show_init(uint8_t k, show_state_t *s) {
  zero(s, sizeof *s);
  if (k == 6) for (uint8_t j = 0; j < 7; j++) {
    s->flies.f[j].x4 = (int16_t)(((int8_t)rnd8()) >> 1) * 16;          /* 1/16 mm */
    s->flies.f[j].y4 = (int16_t)(LED_Y0_4 + 48 + (rnd8() & 127)) * 4;
    s->flies.f[j].h = rnd8(); s->flies.f[j].ph = rnd8();
  }
  if (k == 11) for (uint8_t j = 0; j < 9; j++) { s->orn.o[j].i = rnd8() % NLED; s->orn.o[j].hold = 100 + (rnd8() & 127); }
}

void show_frame(uint8_t k, show_state_t *s, uint16_t t, uint8_t *out) {
  switch (k) {
  case 0: /* Twinkle */
    for (uint8_t i = 0; i < NLED; i++) {
      uint8_t ph = s->twinkle.ph[i];
      if (!ph) {
        if (rnd8() < 2) { s->twinkle.ph[i] = 1; s->twinkle.pk[i] = (uint8_t)(115 | (rnd8() & 0x8C)); }
        out[i] = 13; continue;
      }
      uint8_t pk = s->twinkle.pk[i], sp = 2 + (pk & 3);
      uint8_t np = ph + sp;
      s->twinkle.ph[i] = np < ph ? 0 : np;
      uint8_t e = (uint8_t)sin8_lut[ph >> 1];
      out[i] = qadd8(13, scale8(pk | 3, (uint8_t)((e * e) >> 6)));
    }
    break;
  case 1: { /* Snowfall: flakes land on their column, pile up, melt */
    typeof(s->snowf) *S = &s->snowf;
    for (uint8_t i = 0; i < NLED; i++) out[i] = 5;
    if (rnd8() < 23 && S->n < 24) {
      S->fl[S->n].x4 = (int16_t)((int8_t)rnd8()) * 2 / 3;
      S->fl[S->n].y4 = (int16_t)(LED_Y0_4 - 12) * 4 - (rnd8() & 63);          /* 1/16 mm */
      S->fl[S->n].vy = 1 + (rnd8() & 1); S->fl[S->n].sw = rnd8(); S->n++;
    }
    for (int8_t j = (int8_t)S->n - 1; j >= 0; j--) {
      S->fl[j].y4 += S->fl[j].vy;
      int16_t x = S->fl[j].x4 + (sin8((uint8_t)(t * 2 + S->fl[j].sw)) >> 5), y = S->fl[j].y4 >> 2;
      int16_t gx = (x + (x < 0 ? -PITCH4 / 2 : PITCH4 / 2)) / PITCH4, col = gx - GXMIN;
      if (!S->melt && col >= 0 && col < GW && col_bottom[col] >= 0) {
        int li = led_at(gx, col_bottom[col] - S->depth[col]);
        if (li >= 0 && y >= led_y4[li]) {
          S->snow[li] = 255; S->depth[col]++; S->total++; S->fl[j] = S->fl[--S->n]; continue;
        }
      }
      if (y > LED_Y1_4 + 12) { S->fl[j] = S->fl[--S->n]; continue; }
      splat(out, x, y, 242, PITCH4 * 5 / 4);
    }
    if (S->total > 70) S->melt = 1;
    if (S->melt) {
      uint8_t any = 0;
      for (uint8_t i = 0; i < NLED; i++) if (S->snow[i]) { S->snow[i] = qsub8(S->snow[i], 1 + (LV(i) < 128)); any |= S->snow[i]; }
      if (!any) { S->melt = 0; S->total = 0; zero(S->depth, sizeof S->depth); }
    }
    for (uint8_t i = 0; i < NLED; i++)
      if (S->snow[i]) out[i] = max8(out[i], scale8(S->snow[i], (uint8_t)(107 + (sin8((uint8_t)(t * 3 + i * 37)) >> 4))));
    break;
  }
  case 2: /* Breathe */
    for (uint8_t i = 0; i < NLED; i++) {
      uint8_t c = (uint8_t)(128 - sin8((uint8_t)(((t * 63) >> 6) - (LV(i) >> 2) + 64)));
      uint8_t e = scale8(c, (uint8_t)((c >> 1) + 128));
      out[i] = qadd8(15, scale8(240, scale8(e, (uint8_t)(255 - (LV(i) >> 2)))));
    }
    break;
  case 3: /* Spiral garland */
    for (uint8_t i = 0; i < NLED; i++) {
      int8_t th = led_th[i];
      uint8_t depth = (uint8_t)(115 + sin8((uint8_t)(th + 64))), b = 9;
      for (uint8_t g = 0; g < 2; g++) {
        uint8_t ph = (uint8_t)(((LV(i) * 166) >> 6) + th - ((t * 9) >> 4) + g * 128);
        uint8_t band = gauss8(iabs((int8_t)(ph - 128)));
        uint8_t bulbs = (uint8_t)(166 + (sin8((uint8_t)(th * 7 - t * 24 + g * 73)) >> 1));
        uint8_t v = scale8(scale8(band, bulbs), depth);
        b = qadd8(b, g ? scale8(v, 140) : v);
      }
      out[i] = b;
    }
    break;
  case 4: { /* Candle flicker */
    uint8_t gust = (uint8_t)(184 + (noise8((uint16_t)(t * 18), 7u << 8) >> 2));
    for (uint8_t i = 0; i < NLED; i++) {
      uint8_t n1 = noise8((uint16_t)(i * 187), (uint16_t)(t * 31));
      uint8_t region = (uint8_t)(201 + (noise8((uint16_t)(led_x4[i] * 6 + t * 8), (uint16_t)(led_y4[i] * 6)) >> 3));
      out[i] = scale8(scale8((uint8_t)(56 + scale8(n1, 158)), gust), region);
    }
    break;
  }
  case 5: { /* Star ripple */
    uint16_t knew = t / 105;
    for (uint8_t i = 0; i < NLED; i++) {
      uint16_t b = 9 + ((31u * (255u - (led_ds4[i] * 6u > 255u ? 255u : led_ds4[i] * 6u))) >> 8);
      for (uint8_t kk = 0; kk < 4 && kk <= knew; kk++) {
        uint16_t age = t - (knew - kk) * 105;
        int16_t d = (int16_t)led_ds4[i] - (int16_t)((age * 27) >> 4);
        uint8_t amp = (uint8_t)(age * 2 > 242 ? 0 : 242 - age * 2);
        b += scale8(gauss8((iabs(d) * 5) >> 2), amp);
      }
      out[i] = b > 255 ? 255 : (uint8_t)b;
    }
    break;
  }
  case 6: /* Fireflies (positions in 1/16 mm) */
    fade(s->flies.buf, 245);
    for (uint8_t j = 0; j < 7; j++) {
      typeof(s->flies.f[0]) *f = &s->flies.f[j];
      f->h += (int8_t)(noise8((uint16_t)(t * 9), (uint16_t)(j * 70) << 3) - 128) >> 5;
      f->x4 += sin8((uint8_t)(f->h + 64)) >> 6;
      f->y4 += sin8(f->h) >> 6;
      int16_t x4 = f->x4 >> 2, y4 = f->y4 >> 2;
      int16_t row = (y4 - LED_Y0_4) / PITCH4; if (row < 0) row = 0; if (row >= GH) row = GH - 1;
      if (y4 < LED_Y0_4 + 12 || y4 > LED_Y1_4 - 4 || iabs(x4) > row_half4[row] - 8) f->h += 9;
      int8_t e = sin8((uint8_t)(t * (3 + (j & 3)) + f->ph));
      uint8_t blink = (uint8_t)(64 + scale8(191, e > 0 ? (uint8_t)((e * e) >> 6) : 0));
      splat(s->flies.buf, x4, y4, blink, PITCH4 * 27 / 20);
    }
    for (uint8_t i = 0; i < NLED; i++) out[i] = max8(s->flies.buf[i], 5);
    break;
  case 7: /* Rising sparks (1/16 mm) */
    fade(s->sparks.buf, 222);
    if (rnd8() < 56 && s->sparks.n < 40) {
      typeof(s->sparks.p[0]) *p = &s->sparks.p[s->sparks.n++];
      p->x4 = (int16_t)((int8_t)rnd8()) * 3 / 4 * 4; p->y4 = (LED_Y1_4 + 6) * 4;
      p->vy = 3 + (rnd8() & 3); p->life = 115 + (rnd8() >> 1);
    }
    for (int8_t j = (int8_t)s->sparks.n - 1; j >= 0; j--) {
      typeof(s->sparks.p[0]) *p = &s->sparks.p[j];
      p->y4 -= p->vy;
      int16_t prog = (int16_t)(((int32_t)(LED_Y1_4 * 4 - p->y4) * 255) / ((LED_Y1_4 - LED_Y0_4) * 4));
      if (prog > p->life) { *p = s->sparks.p[--s->sparks.n]; continue; }
      uint8_t amp = (uint8_t)(255 - (prog * 255) / p->life);
      splat(s->sparks.buf, (p->x4 >> 2) + (sin8((uint8_t)(t * 16 + j * 40)) >> 5), p->y4 >> 2, scale8(amp, 217), PITCH4 * 11 / 10);
    }
    for (uint8_t i = 0; i < NLED; i++) out[i] = max8(s->sparks.buf[i], (uint8_t)(6 + (scale8(LV(i), LV(i)) >> 4)));
    break;
  case 8: { /* Fill & drain */
    uint16_t tc = t % 550;
    uint8_t L = tc < 200 ? (uint8_t)(tc * 255 / 200) : tc < 275 ? 255 : tc < 475 ? (uint8_t)(255 - (tc - 275) * 255 / 200) : 0;
    for (uint8_t i = 0; i < NLED; i++) {
      int16_t surf = 265 - ((L * 280) >> 8) + (sin8((uint8_t)(LU(i) * 2 + t * 5)) >> 4) + (sin8((uint8_t)(LU(i) * 4 - t * 8)) >> 5);
      int16_t dv = (int16_t)LV(i) - surf;
      uint8_t v = dv > 7 ? 153 : dv > -4 ? (uint8_t)(76 + dv * 10) : 8;
      out[i] = qadd8(v, scale8(gauss8((iabs(dv) * 16) / 7), 128));
    }
    break;
  }
  case 9: /* Northern lights: 3 noise samples per LED (the heaviest show) */
    for (uint8_t i = 0; i < NLED; i++) {
      uint16_t w = (uint16_t)((LU(i) + 128) * 3) + (uint16_t)(sin8((uint8_t)(LV(i) * 3 + t)) * 2);
      uint8_t n1 = noise8((uint16_t)(w + t * 3), (uint16_t)(t * 5 + 800));
      uint8_t r1 = (uint8_t)(255 - iabs((int16_t)n1 * 2 - 255));
      uint8_t n2 = noise8((uint16_t)(w * 2 - t * 2 + 10240), (uint16_t)(t * 7));
      uint8_t r2 = (uint8_t)(255 - iabs((int16_t)n2 * 2 - 255));
      uint8_t rays = (uint8_t)(89 + scale8(166, noise8((uint16_t)((LU(i) + 128) * 13), (uint16_t)(LV(i) * 3 - t * 23))));
      uint8_t c = qadd8(scale8(242, scale8(scale8(r1, r1), r1)), scale8(128, scale8(scale8(r2, r2), scale8(r2, r2))));
      out[i] = qadd8(6, scale8(c, rays));
    }
    break;
  case 10: { /* Fireworks (1/64 mm, velocities 1/64 mm per frame) */
    typeof(s->fw) *F = &s->fw;
    fade(F->buf, 213);
    if (F->next) F->next--;
    else {
      F->next = 55 + (rnd8() % 80);
      for (uint8_t r = 0; r < 3; r++) if (!F->rk[r].ty4) {
        F->rk[r].tx = (int8_t)(((int8_t)rnd8()) / 3);
        F->rk[r].x4 = F->rk[r].tx * 16 * 3 / 5; F->rk[r].y4 = (LED_Y1_4 + 8) * 16;
        F->rk[r].ty4 = (int16_t)(LED_Y0_4 + 16 + (rnd8() % 80)) * 16; break;
      }
    }
    for (uint8_t r = 0; r < 3; r++) {
      typeof(F->rk[0]) *R = &F->rk[r];
      if (!R->ty4) continue;
      R->y4 -= 43; R->x4 += ((R->tx * 16) - R->x4) >> 5;
      splat(F->buf, R->x4 >> 2, R->y4 >> 4, 204, PITCH4);
      if (R->y4 <= R->ty4) {
        uint8_t nb = 18 + (rnd8() & 7);
        for (uint8_t j = 0; j < nb && F->npt < 36; j++) {
          typeof(F->pt[0]) *p = &F->pt[F->npt++];
          uint8_t a = (uint8_t)(j * 256 / nb + (rnd8() & 7)), sp = 45 + (rnd8() & 15);
          p->x4 = R->x4 * 4; p->y4 = R->y4 * 4;
          p->vx = (int8_t)((sin8((uint8_t)(a + 64)) * sp) >> 7); p->vy = (int8_t)((sin8(a) * sp) >> 7);
          p->age = 0; p->life = 60 + (rnd8() & 31);
        }
        R->ty4 = 0;
      }
    }
    for (int8_t j = (int8_t)F->npt - 1; j >= 0; j--) {
      typeof(F->pt[0]) *p = &F->pt[j];
      if (++p->age > p->life) { *p = F->pt[--F->npt]; continue; }
      p->vx -= p->vx >> 5; p->vy -= p->vy >> 5;
      if ((p->age & 3) == 0) p->vy++;                    /* gravity */
      p->x4 += p->vx; p->y4 += p->vy;
      uint8_t u = (uint8_t)((p->age * 255u) / p->life), a = (uint8_t)(255 - u);
      a = scale8(a, a);
      if (u > 153 && (rnd8() & 1)) a >>= 2;
      splat(F->buf, p->x4 >> 4, p->y4 >> 4, a, PITCH4);
    }
    for (uint8_t i = 0; i < NLED; i++) out[i] = max8(F->buf[i], 4);
    break;
  }
  case 11: /* Ornaments */
    for (uint8_t i = 0; i < NLED; i++) {
      uint8_t ph = (uint8_t)(((LV(i) * 115) >> 6) + led_th[i] + 51);
      uint8_t glint = usin8((uint8_t)(led_th[i] * 10 - t * 13)); glint = scale8(glint, glint); glint = scale8(glint, glint);
      out[i] = qadd8(8, scale8(gauss8((iabs((int8_t)(ph - 128)) * 8) / 5), (uint8_t)(26 + scale8(glint, 77))));
    }
    for (uint8_t j = 0; j < 9; j++) {
      typeof(s->orn.o[0]) *o = &s->orn.o[j];
      uint16_t age = t - o->t0, total = 120 + o->hold;
      if (age > total) {
        for (uint8_t tries = 0; tries < 20; tries++) {
          uint8_t c = rnd8() % NLED, ok = 1;
          for (uint8_t q = 0; q < 9; q++) if (q != j && iabs(led_x4[s->orn.o[q].i] - led_x4[c]) + iabs((int16_t)led_y4[s->orn.o[q].i] - led_y4[c]) < 28) ok = 0;
          if (ok) { o->i = c; break; }
        }
        o->t0 = t; o->hold = 100 + (rnd8() & 127); age = 0;
      }
      uint8_t env = age < 60 ? (uint8_t)(age * 4) : age < 60 + o->hold ? 255 : (uint8_t)((total - age) * 4);
      splat(out, led_x4[o->i], led_y4[o->i], env, PITCH4 * 17 / 10);
    }
    break;
  case 12: { /* Soft radar */
    int8_t beam = (int8_t)((sin8((uint8_t)((t * 41) >> 6)) * 39) >> 7);
    int8_t dir = sin8((uint8_t)(((t * 41) >> 6) + 64)) >= 0 ? 1 : -1;
    for (uint8_t i = 0; i < NLED; i++) {
      int16_t d = (int16_t)LPHI(i) - beam, behind = -dir * d;
      uint16_t v = 41 + scale8(128, gauss8(iabs(d)));
      if (behind > 0) v += (71u * (255u - (behind * 9 > 255 ? 255u : (uint16_t)(behind * 9)))) >> 8;
      if (v > 255) v = 255;
      out[i] = scale8((uint8_t)v, (uint8_t)(255 - (led_ds4[i] >> 2)));
    }
    break;
  }
  case 13: { /* Heartbeat */
    uint8_t tp = (uint8_t)(t % 62);
    for (uint8_t i = 0; i < NLED; i++) {
      int16_t dx = iabs(led_x4[i]), dy = iabs((int16_t)led_y4[i] - 120);
      int16_t d4 = (dx > dy ? dx + (dy * 3 >> 3) : dy + (dx * 3 >> 3));
      int16_t tau = (int16_t)tp - ((d4 * 29) >> 7); if (tau < 0) tau += 62;
      uint8_t env = qadd8(gauss8(((tau - 4) * 16) / 4), scale8(153, gauss8(((tau - 17) * 16) / 5)));
      uint8_t fall = (uint8_t)(255 - (d4 > 140 ? 115 : (d4 * 115) / 140));
      out[i] = qadd8(13, scale8(230, scale8(env, fall)));
    }
    break;
  }
  case 14: /* Tinsel shimmer */
    for (uint8_t i = 0; i < NLED; i++) {
      uint8_t gy = LGY(i);
      int8_t dir = (gy & 1) ? 1 : -1;
      uint8_t s1 = usin8((uint8_t)(led_x4[i] * 4 + gy * 37 + dir * t * 12 + perm[gy]));
      for (uint8_t q = 0; q < 4; q++) s1 = scale8(s1, s1);
      if (gy % 3) s1 = scale8(s1, 90);
      uint8_t s2 = usin8((uint8_t)((t * (24 + (perm[i] & 31))) >> 3) + perm[(uint8_t)(i * 7)]);
      for (uint8_t q = 0; q < 5; q++) s2 = scale8(s2, s2);
      out[i] = qadd8(qadd8(15, scale8(204, s1)), scale8(217, s2));
    }
    break;
  }
}
