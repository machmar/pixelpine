/* Fixed-point maths for the per-LED loops. The MCU has no FPU, so each light show's inner loop runs in Q12
 * (value * 4096) with table-based sin / exp / pow. Per-frame scalars (one value per frame, not per LED) stay in
 * double, written the same way as the simulator's JavaScript, and are converted with the helpers below.
 *
 *   q12    1.0 = 4096, int32. Products of two values up to +-8.0 fit in 32 bits.
 *   Q16    1.0 = 65536, used for value-noise coordinates (needs the integer part and a fine fraction).
 *   turns  an angle as a uint32 fraction of a full circle (2^32 = 2pi). Adding and multiplying wrap exactly
 *          like an angle does, so sin(a + b * t) is a few integer operations. */
#pragma once
#include <stdint.h>

typedef int32_t q12;
#define ONE 4096
#define QC(x) ((q12)((x) * 4096.0 + ((x) >= 0 ? 0.5 : -0.5)))           /* compile-time constant */
#define TURN_PER_RAD 683565275.57643158                                   /* 2^32 / 2pi */

static inline q12 qm(q12 a, q12 b) { return (a * b) >> 12; }
static inline q12 qclamp(q12 x) { return x < 0 ? 0 : x > ONE ? ONE : x; }
static inline q12 qmax(q12 a, q12 b) { return a > b ? a : b; }
static inline q12 qmin(q12 a, q12 b) { return a < b ? a : b; }
static inline q12 qabs(q12 a) { return a < 0 ? -a : a; }
static inline q12 qsmooth(q12 t) { t = qclamp(t); return qm(qm(t, t), 3 * ONE - 2 * t); }   /* t*t*(3-2t) */

/* double -> fixed, for per-frame scalars */
static inline q12 dq(double x) { return (q12)(x * 4096.0 + (x >= 0 ? 0.5 : -0.5)); }
static inline int32_t dq16(double x) { return (int32_t)(x * 65536.0 + (x >= 0 ? 0.5 : -0.5)); }
static inline uint32_t dturn(double rad) { return (uint32_t)(int64_t)(rad * TURN_PER_RAD); }   /* wraps mod 2pi */
static inline double fxd(q12 x) { return x * (1.0 / 4096.0); }

q12 qsin(uint32_t turn);
static inline q12 qcos(uint32_t turn) { return qsin(turn + 0x40000000u); }
q12 qexpn(q12 x);                         /* exp(-x), x >= 0 */
q12 qgauss(q12 d);                        /* exp(-d*d) */
q12 qpow16(q12 x), qpow12(q12 x), qpow14(q12 x);   /* x^1.6, x^1.2, x^1.4 for 0 <= x <= 1 */
uint32_t isqrt32(uint32_t v);
uint8_t gamma8(q12 v);                    /* round(255 * v^2.2): the simulator's 8-bit "raw scan" level */
double noise2d(double x, double y);       /* noise2 in double, for per-frame scalars (exact as the JS) */
double jsin(double x), jcos(double x);    /* Math.sin / Math.cos, bit-exact (jsmath.c) */
double jfmod(double x, double y);         /* JS x % y for x >= 0, y > 0 */
#define dsin jsin
#define dcos jcos

/* the simulator's integer hashes and value noise (index.html "portable helpers"): hashes are bit-exact */
uint32_t hash_u(uint32_t x);              /* hashU(x) * 2^32 */
static inline uint32_t hash1u(uint32_t i) { return hash_u(i * 0x9e3779b1u); }
static inline uint32_t hash2u(int32_t x, int32_t y) { return hash_u((uint32_t)x * 374761393u + (uint32_t)y * 668265263u); }
q12 noise2(int32_t x16, int32_t y16);     /* value noise 0..1, coordinates in Q16 */

/* xorshift32, as makeRng() in the simulator. rnd_u() is the raw state; s / 2^32 is the simulator's s.rnd() */
static inline uint32_t xs_next(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static inline double xs_d(uint32_t *s) { return xs_next(s) * (1.0 / 4294967296.0); }
static inline uint32_t xs_seed(uint32_t seed) { return seed ? seed : 1; }
