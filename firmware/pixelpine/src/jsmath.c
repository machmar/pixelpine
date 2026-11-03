/* Math.sin / Math.cos / % exactly as JavaScript computes them. V8 implements Math.sin and Math.cos with fdlibm
 * (base/ieee754.cc); this is the same fdlibm code, minus the huge-argument reduction (__kernel_rem_pio2, 2 KB):
 * every angle here is below 2^19 * pi/2. jfmod is exact, like JS %. test/compare.py checks them bit for bit. */
#include <stdint.h>
#include <string.h>
#include "fx.h"

static inline int32_t hi(double x) { uint64_t u; memcpy(&u, &x, 8); return (int32_t)(u >> 32); }
static inline double with_hi(int32_t h) { uint64_t u = (uint64_t)(uint32_t)h << 32; double x; memcpy(&x, &u, 8); return x; }

static const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03,
  S3 = -1.98412698298579493134e-04, S4 = 2.75573137070700676789e-06, S5 = -2.50507602534068634195e-08,
  S6 = 1.58969099521155010221e-10;
static const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03,
  C3 = 2.48015872894767294178e-05, C4 = -2.75573143513906633035e-07, C5 = 2.08757232129817482790e-09,
  C6 = -1.13596475577881948265e-11;

static double k_sin(double x, double y, int iy) {
  int32_t ix = hi(x) & 0x7fffffff;
  if (ix < 0x3e400000 && (int)x == 0) return x;
  double z = x * x, v = z * x, r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
  if (iy == 0) return x + v * (S1 + z * r);
  return x - ((z * (0.5 * y - v * r) - y) - v * S1);
}
static double k_cos(double x, double y) {
  int32_t ix = hi(x) & 0x7fffffff;
  if (ix < 0x3e400000 && (int)x == 0) return 1.0;
  double z = x * x, r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
  if (ix < 0x3FD33333) return 1.0 - (0.5 * z - (z * r - x * y));
  double qx = ix > 0x3fe90000 ? 0.28125 : with_hi(ix - 0x00200000);
  double hz = 0.5 * z - qx, a = 1.0 - qx;
  return a - (hz - (z * r - x * y));
}

static const int32_t npio2_hw[32] = {
  0x3FF921FB, 0x400921FB, 0x4012D97C, 0x401921FB, 0x401F6A7A, 0x4022D97C, 0x4025FDBB, 0x402921FB,
  0x402C463A, 0x402F6A7A, 0x4031475C, 0x4032D97C, 0x40346B9C, 0x4035FDBB, 0x40378FDB, 0x403921FB,
  0x403AB41B, 0x403C463A, 0x403DD85A, 0x403F6A7A, 0x40407E4C, 0x4041475C, 0x4042106C, 0x4042D97C,
  0x4043A28C, 0x40446B9C, 0x404534AC, 0x4045FDBB, 0x4046C6CB, 0x40478FDB, 0x404858EB, 0x404921FB };
static const double invpio2 = 6.36619772367581382433e-01, pio2_1 = 1.57079632673412561417e+00,
  pio2_1t = 6.07710050650619224932e-11, pio2_2 = 6.07710050630396597660e-11, pio2_2t = 2.02226624879595063154e-21,
  pio2_3 = 2.02226624871116645580e-21, pio2_3t = 8.47842766036889956997e-32;

static int rem_pio2(double x, double *y) {          /* |x| <= 2^19 * pi/2 */
  int32_t hx = hi(x), ix = hx & 0x7fffffff;
  if (ix <= 0x3fe921fb) { y[0] = x; y[1] = 0; return 0; }
  if (ix < 0x4002d97c) {                             /* |x| < 3pi/4: n = +-1 */
    double z;
    if (hx > 0) {
      z = x - pio2_1;
      if (ix != 0x3ff921fb) { y[0] = z - pio2_1t; y[1] = (z - y[0]) - pio2_1t; }
      else { z -= pio2_2; y[0] = z - pio2_2t; y[1] = (z - y[0]) - pio2_2t; }
      return 1;
    }
    z = x + pio2_1;
    if (ix != 0x3ff921fb) { y[0] = z + pio2_1t; y[1] = (z - y[0]) + pio2_1t; }
    else { z += pio2_2; y[0] = z + pio2_2t; y[1] = (z - y[0]) + pio2_2t; }
    return -1;
  }
  double t = x < 0 ? -x : x;
  int32_t n = (int32_t)(t * invpio2 + 0.5);
  double fn = (double)n, r = t - fn * pio2_1, w = fn * pio2_1t;
  if (n < 32 && ix != npio2_hw[n - 1]) y[0] = r - w;
  else {
    int32_t j = ix >> 20, i;
    y[0] = r - w;
    i = j - ((hi(y[0]) >> 20) & 0x7ff);
    if (i > 16) {
      t = r; w = fn * pio2_2; r = t - w; w = fn * pio2_2t - ((t - r) - w); y[0] = r - w;
      i = j - ((hi(y[0]) >> 20) & 0x7ff);
      if (i > 49) { t = r; w = fn * pio2_3; r = t - w; w = fn * pio2_3t - ((t - r) - w); y[0] = r - w; }
    }
  }
  y[1] = (r - y[0]) - w;
  if (hx < 0) { y[0] = -y[0]; y[1] = -y[1]; return -n; }
  return n;
}

double jsin(double x) {
  double y[2];
  if ((hi(x) & 0x7fffffff) <= 0x3fe921fb) return k_sin(x, 0, 0);
  switch (rem_pio2(x, y) & 3) {
  case 0: return k_sin(y[0], y[1], 1);
  case 1: return k_cos(y[0], y[1]);
  case 2: return -k_sin(y[0], y[1], 1);
  default: return -k_cos(y[0], y[1]);
  }
}
double jcos(double x) {
  double y[2];
  if ((hi(x) & 0x7fffffff) <= 0x3fe921fb) return k_cos(x, 0);
  switch (rem_pio2(x, y) & 3) {
  case 0: return k_cos(y[0], y[1]);
  case 1: return -k_sin(y[0], y[1], 1);
  case 2: return -k_cos(y[0], y[1]);
  default: return k_sin(y[0], y[1], 1);
  }
}

double jfmod(double x, double y) {                  /* x >= 0, y > 0: exact, every subtraction is exact */
  if (x < y) return x;
  double s = y;
  while (s * 2 <= x) s *= 2;
  for (; s >= y; s *= 0.5) if (x >= s) x -= s;
  return x;
}
