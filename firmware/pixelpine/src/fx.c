/* Fixed-point maths, see fx.h. */
#pragma GCC optimize("O2")                   /* the per-LED hot path: speed over size here */
#include <limits.h>
#include <math.h>
#include "pine.h"

/* the hashes' constants go through an empty asm so GCC emits MULS (single cycle on this core) instead of a
 * 20-instruction shift-and-add sequence */
#define MULC(x, c) ({ uint32_t c_ = (c); __asm__("" : "+r"(c_)); (x) * c_; })
static inline uint32_t hash_i(uint32_t x) {
  x = MULC(x ^ (x >> 16), 0x7feb352du);
  x = MULC(x ^ (x >> 15), 0x846ca68bu);
  return x ^ (x >> 16);
}

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

uint32_t hash_u(uint32_t x) { return hash_i(x); }

/* noise2(x, y) of the simulator: smooth value noise on the integer lattice, hash2 at the corners */
q12 noise2(int32_t x, int32_t y) {
  int32_t xi = x >> 16, yi = y >> 16;
  q12 xf = (x & 0xFFFF) >> 4, yf = (y & 0xFFFF) >> 4;
  q12 u = qm(qm(xf, xf), 3 * ONE - 2 * xf), v = qm(qm(yf, yf), 3 * ONE - 2 * yf);
  uint32_t h = (uint32_t)xi * 374761393u + (uint32_t)yi * 668265263u;    /* hash2 corners share the products */
  q12 a = (q12)(hash_i(h) >> 20), b = (q12)(hash_i(h + 374761393u) >> 20);
  q12 c = (q12)(hash_i(h + 668265263u) >> 20), d = (q12)(hash_i(h + 374761393u + 668265263u) >> 20);
  return a + qm(b - a, u) + qm(c - a, v) + qm(qm(a - b - c + d, u), v);
}

uint8_t gamma8(q12 v) {
  uint32_t q = gamma_lo[v >> 3];                /* level at v rounded down to 8, then up to the exact one */
  while (gamma_thr[q] <= v) q++;               /* gamma_thr[255] = 4097 > any value */
  return (uint8_t)q;
}

/* round(1023 * v^2.2) for the board's 10-bit scan (within 1 step, monotonic): table of 1023 * 64 * (k/128)^2.2 and
 * linear interpolation. The simulator's comparisons stay on gamma8. */
static const uint16_t g10[129] = {
  0, 2, 7, 17, 32, 52, 78, 109, 147, 190, 240, 296, 358, 427, 503, 586,
  675, 771, 875, 985, 1103, 1228, 1360, 1500, 1647, 1802, 1964, 2134, 2312, 2497, 2691, 2892,
  3101, 3318, 3544, 3777, 4018, 4268, 4526, 4792, 5067, 5350, 5641, 5941, 6249, 6565, 6891, 7225,
  7567, 7918, 8278, 8647, 9024, 9410, 9805, 10209, 10622, 11044, 11475, 11914, 12363, 12821, 13288, 13764,
  14249, 14744, 15247, 15760, 16282, 16814, 17354, 17904, 18464, 19033, 19611, 20199, 20796, 21403, 22019, 22645,
  23280, 23925, 24580, 25244, 25918, 26602, 27295, 27999, 28711, 29434, 30167, 30909, 31661, 32423, 33195, 33977,
  34769, 35571, 36382, 37204, 38036, 38878, 39730, 40592, 41464, 42346, 43238, 44141, 45053, 45976, 46909, 47852,
  48806, 49770, 50744, 51728, 52723, 53728, 54744, 55770, 56806, 57852, 58909, 59977, 61055, 62143, 63242, 64352,
  65472,
};
uint16_t gamma10(q12 v) {
  if (v <= 0) return 0;
  if (v >= 4096) return 1023;
  uint32_t k = (uint32_t)v >> 5, f = (uint32_t)v & 31u;
  uint32_t y = g10[k] + (((g10[k + 1] - g10[k]) * f + 16u) >> 5);
  return (uint16_t)((y + 32u) >> 6);
}

double noise2d(double x, double y) {
  double xi = floor(x), yi = floor(y), xf = x - xi, yf = y - yi;
  double u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
  int32_t X = (int32_t)xi, Y = (int32_t)yi;
  double a = hash2u(X, Y) / 4294967296.0, b = hash2u(X + 1, Y) / 4294967296.0;
  double c = hash2u(X, Y + 1) / 4294967296.0, d = hash2u(X + 1, Y + 1) / 4294967296.0;
  return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

uint32_t qatan2(int32_t y, int32_t x) {             /* octant + atan(z) ~ z (pi/4 + 0.273 (1 - |z|)) */
  if (!x && !y) return 0;
  int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
  while (ax > 32767 || ay > 32767) { ax >>= 1; ay >>= 1; }                          /* 32-bit division below */
  int32_t z = ay <= ax ? (ay << 16) / (ax ? ax : 1) : (ax << 16) / ay;                 /* 0..1, Q16 */
  uint32_t a = (uint32_t)z * (uint32_t)(8192 + ((2847 * (65536 - z)) >> 16));          /* turns (1/8 turn = 2^29) */
  if (ay > ax) a = 0x40000000u - a;
  if (x < 0) a = 0x80000000u - a;
  if (y < 0) a = (uint32_t)-a;
  return a;
}

void noise_row(noise_row_t *r, int32_t y) {
  r->xi = INT32_MIN;
  r->h0 = (uint32_t)(y >> 16) * 668265263u;
  r->h1 = r->h0 + 668265263u;
  r->v = smooth_tab[(y & 0xFFFF) >> 8] + ((((int32_t)smooth_tab[((y & 0xFFFF) >> 8) + 1] - smooth_tab[(y & 0xFFFF) >> 8]) * (int32_t)(y & 255)) >> 8);
}
q12 noise2r(noise_row_t *r, int32_t x) {           /* consecutive calls with x in the same or next cell reuse hashes */
  int32_t xi = x >> 16;
  uint32_t k = (x & 0xFFFF) >> 8;
  q12 u = smooth_tab[k] + ((((int32_t)smooth_tab[k + 1] - smooth_tab[k]) * (int32_t)(x & 255)) >> 8);
  if (xi != r->xi) {
    uint32_t hx = (uint32_t)xi * 374761393u;
    if (xi == r->xi + 1) { r->a = r->b; r->c = r->d; }
    else { r->a = (q12)(hash_i(hx + r->h0) >> 20); r->c = (q12)(hash_i(hx + r->h1) >> 20); }
    r->b = (q12)(hash_i(hx + 374761393u + r->h0) >> 20); r->d = (q12)(hash_i(hx + 374761393u + r->h1) >> 20);
    r->xi = xi;
  }
  q12 ab = r->a + qm(r->b - r->a, u), cd = r->c + qm(r->d - r->c, u);
  return ab + qm(cd - ab, r->v);
}

double sin_hp(uint32_t ph) {                       /* Taylor to 3rd order around the nearest table point */
  uint32_t q = ph >> 30, x = ph & 0x3FFFFFFFu;
  if (q & 1) x = 0x40000000u - x;
  uint32_t k = (x + (1u << 21)) >> 22;              /* nearest of 257 points per quarter */
  int64_t d = ((int64_t)x - ((int64_t)k << 22)) * 6746518852LL >> 32;   /* offset in rad, Q30 (x pi/2 in Q32) */
  int64_t s0 = sin30_tab[k], c0 = sin30_tab[256 - k];
  int64_t d2 = d * d >> 30, d3 = d2 * d >> 30;
  int64_t v = s0 + (c0 * d >> 30) - (s0 * d2 >> 31) - (c0 * d3 / 6 >> 30);
  return (q & 2 ? -v : v) * (1.0 / 1073741824.0);
}
