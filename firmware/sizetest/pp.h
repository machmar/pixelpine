/* PixelPine sizing prototype - shared declarations.
 * Throwaway code written to measure flash/RAM (drivers in py32f003/ and ch32v003/); NOT tested on hardware. */
#pragma once
#include <stdint.h>
#include "geom.h"

#define NLINES 16
#define FPS 50

/* ---------- generated const tables (tables.c) ---------- */
extern const int8_t led_x4[NLED], led_th[NLED], col_bottom[GW];
extern const uint8_t led_y4[NLED], led_ds4[NLED], led_ang[NLED];
extern const uint8_t grid[GW * GH], row_half4[GH];
extern const int8_t sin8_lut[256];
extern const uint8_t gauss_lut[64], perm[256];
extern const uint8_t phase_led[NLINES * 15], phase_cath[NLINES * 15];   /* LED index / cathode line, per anode phase */

/* ---------- small integer helpers (no FPU on either MCU; RV32EC has no multiplier either) ---------- */
static inline uint8_t qadd8(uint8_t a, uint8_t b) { unsigned s = a + b; return s > 255 ? 255 : s; }
static inline uint8_t qsub8(uint8_t a, uint8_t b) { return a > b ? a - b : 0; }
static inline uint8_t max8(uint8_t a, uint8_t b) { return a > b ? a : b; }
uint8_t scale8(uint8_t a, uint8_t b);                  /* out of line: ~80 call sites */
static inline int8_t sin8(uint8_t a) { return sin8_lut[a]; }
static inline uint8_t usin8(uint8_t a) { return (uint8_t)(sin8_lut[a] + 128); }
static inline uint8_t gauss8(int16_t d16) { if (d16 < 0) d16 = -d16; return d16 >= 64 ? 0 : gauss_lut[d16]; }
/* per-LED values derived on the fly instead of stored (saves ~1.7 KB of flash) */
#define LV(i)   ((uint8_t)((((uint16_t)led_y4[i] - LED_Y0_4) * (65280u / (LED_Y1_4 - LED_Y0_4))) >> 8))
#define LU(i)   ((int8_t)((led_x4[i] * 23) >> 4))
#define LGX(i)  ((int8_t)((led_x4[i] + (led_x4[i] < 0 ? -PITCH4 / 2 : PITCH4 / 2)) / PITCH4))
#define LGY(i)  ((uint8_t)(((int16_t)led_y4[i] - LED_Y0_4 + PITCH4 / 2) / PITCH4 + GYMIN))
#define LPHI(i) ((int8_t)(64 - led_ang[i]))
#define GAMMA(x) scale8((x), (x))
#define STAR_R16(a) ((uint8_t)(40 + scale8(34, usin8((uint8_t)((a) * 5 + 64)))))
static inline int led_at(int gx, int gy) {
  gx -= GXMIN; gy -= GYMIN;
  if (gx < 0 || gx >= GW || gy < 0 || gy >= GH) return -1;
  uint8_t i = grid[gy * GW + gx];
  return i == 255 ? -1 : i;
}
uint32_t rnd32(void);
static inline uint8_t rnd8(void) { return (uint8_t)rnd32(); }
uint8_t noise8(uint16_t x, uint16_t y);            /* value noise, 8.8 fixed-point coords */
void splat(uint8_t *buf, int16_t x4, int16_t y4, uint8_t amp, uint8_t r4);   /* soft blob, max-combine */

/* ---------- light shows (shows.c) ---------- */
#define NSHOWS 15
typedef union {
  struct { uint8_t ph[NLED]; uint8_t pk[NLED]; } twinkle;
  struct { uint8_t snow[NLED]; int8_t depth[GW]; struct { int16_t x4, y4; uint8_t vy, sw; } fl[24]; uint8_t melt, n; uint16_t total; } snowf;
  struct { uint8_t buf[NLED]; struct { int16_t x4, y4; uint8_t h, ph; } f[7]; } flies;
  struct { uint8_t buf[NLED]; struct { int16_t x4, y4; uint8_t vy, life; } p[40]; uint8_t n; } sparks;
  struct { uint8_t buf[NLED]; struct { int16_t x4, y4, ty4; int8_t tx; } rk[3];
           struct { int16_t x4, y4; int8_t vx, vy; uint8_t age, life; } pt[36]; uint8_t npt, next; } fw;
  struct { struct { uint8_t i, hold; uint16_t t0; } o[9]; } orn;
} show_state_t;
void show_init(uint8_t k, show_state_t *s);
void show_frame(uint8_t k, show_state_t *s, uint16_t t, uint8_t *out);

/* ---------- Pine's face (face.c) ---------- */
enum { F_IDLE, F_GIGGLE, F_SLEEP, F_STIR, F_WINK, F_SING, F_SNEEZE, F_DOZE, F_GRUMPY, F_EXCITED,
       F_SURPRISED, F_MORNING, F_PET, F_HUG, F_SQUIRM, F_TICKLED, F_PEEK, F_YAWN, F_COUNT };
typedef struct { uint16_t t; uint8_t beh, bl, bt, gt, lx, ly, sp_t; struct { int8_t x, y; uint8_t t; } part[8]; } face_state_t;
void face_start(face_state_t *f, uint8_t beh);
uint8_t face_frame(face_state_t *f, uint8_t *out, uint8_t lvl);   /* returns 1 when a timeline ends */

/* ---------- scenes + transitions (scene.c) ---------- */
enum { T_FADE, T_IRIS, T_BLINK, T_SPARKLE, T_SNOW, T_JELLY, T_RANDOM };
void scene_show(uint8_t k, uint8_t level, uint8_t trans);
void scene_face(uint8_t beh, uint8_t trans);
void scene_overlay(uint8_t beh);
uint8_t scene_done(void);
uint8_t scene_is_show(void);
void scene_step(uint8_t *out);

/* ---------- brain + touch (brain.c, touch.c) ---------- */
enum { G_NONE, G_HOVER, G_TAP, G_PET_START, G_PET_END, G_PRESS, G_SQUEEZE, G_TICKLE };
void brain_init(void);
void brain_tick(uint8_t gesture, uint8_t petting, uint8_t dark);
uint8_t touch_tick(uint8_t *petting);
uint16_t touch_sample(void);
uint8_t ambient_dark(void);

/* ---------- LED scan driver (scan.c) + storage (store.c) ---------- */
void scan_init(void);
void build_masks(const uint8_t *frame);
void store_save_bond(uint16_t bond);
uint16_t store_load_bond(void);
