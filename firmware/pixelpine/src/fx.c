/* Fixed-point maths, see fx.h. */
#include "pine.h"

q12 qsin(uint32_t ph) {
  uint32_t q = ph >> 30, x = (ph >> 14) & 0xFFFFu;            /* quadrant, position in it (16 bits) */
  if (q & 1) x = 0x10000u - x;
  uint32_t k = x >> 8, f = x & 255u;
  int32_t a = sin_tab[k], b = k < 256 ? sin_tab[k + 1] : a;
  int32_t v = (a + (((b - a) * (int32_t)f) >> 8) + 8) >> 4;   /* Q16 table -> Q12 */
  return q & 2 ? -v : v;
}

q12 qexpn(q12 x) {                                           /* exp(-x) = 2^-(x log2 e) */
  if (x <= 0) return ONE;
  if (x >= 16 * ONE) return 0;
  uint32_t y = ((uint32_t)x * 5909u) >> 8;                    /* Q16; log2(e) = 5909 / 4096 */
  uint32_t n = y >> 16, k = (y >> 10) & 63u, f = y & 1023u;
  int32_t a = exp2_tab[k], b = exp2_tab[k + 1];
  return (q12)(((uint32_t)(a + (((b - a) * (int32_t)f) >> 10)) >> n) + 8) >> 4;
}

q12 qgauss(q12 d) {
  if (d < 0) d = -d;
  if (d >= 4 * ONE) return 0;                                 /* exp(-16) */
  return qexpn(qm(d, d));
}

static q12 lut257(const uint16_t *t, q12 x) {
  if (x <= 0) return 0;
  if (x >= ONE) return ONE;
  uint32_t k = (uint32_t)x >> 4, f = (uint32_t)x & 15u;
  return t[k] + ((((int32_t)t[k + 1] - t[k]) * (int32_t)f) >> 4);
}
q12 qpow16(q12 x) { return lut257(pow16_tab, x); }
q12 qpow12(q12 x) { return lut257(pow12_tab, x); }
q12 qpow14(q12 x) { return lut257(pow14_tab, x); }

uint32_t isqrt32(uint32_t v) {
  uint32_t r = 0, bit = 1u << 30;
  while (bit > v) bit >>= 2;
  while (bit) {
    if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
    bit >>= 2;
  }
  return r;
}

uint32_t hash_u(uint32_t x) {
  x = (x ^ (x >> 16)) * 0x7feb352du;
  x = (x ^ (x >> 15)) * 0x846ca68bu;
  return x ^ (x >> 16);
}

/* noise2(x, y) of the simulator: smooth value noise on the integer lattice, hash2 at the corners */
q12 noise2(int32_t x, int32_t y) {
  int32_t xi = x >> 16, yi = y >> 16;
  q12 xf = (x & 0xFFFF) >> 4, yf = (y & 0xFFFF) >> 4;
  q12 u = qm(qm(xf, xf), 3 * ONE - 2 * xf), v = qm(qm(yf, yf), 3 * ONE - 2 * yf);
  q12 a = (q12)(hash2u(xi, yi) >> 20), b = (q12)(hash2u(xi + 1, yi) >> 20);
  q12 c = (q12)(hash2u(xi, yi + 1) >> 20), d = (q12)(hash2u(xi + 1, yi + 1) >> 20);
  return a + qm(b - a, u) + qm(c - a, v) + qm(qm(a - b - c + d, u), v);
}

uint8_t gamma8(q12 v) {
  uint32_t lo = 0, hi = 256;                    /* gamma_thr[255] = 4097 > any value */
  while (lo < hi) { uint32_t m = (lo + hi) >> 1; if (gamma_thr[m] <= v) lo = m + 1; else hi = m; }
  return (uint8_t)lo;
}
