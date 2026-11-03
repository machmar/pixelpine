/* PixelPine firmware: shared declarations.
 *
 * The behaviour is a port of the simulator (simulator/index.html); the names follow it so the two can be read side
 * by side: ANIMS -> shows.c, FB / renderPose -> face.c, scenes + transitions + engine -> scene.c, touch -> touch.c,
 * brain -> brain.c. Hardware drivers are in ../hw (PY32F003) and ../host (PC test build). */
#pragma once
#include <stdint.h>
#include "geom.h"
#include "fx.h"

#define FW_DT 0.02                          /* the firmware frame period: 50 fps (RATE / FW_DT in the simulator) */
#define FPS 50
#define NSHOWS 15

/* Math.random() of the simulator: one xorshift32 stream for the brain, transitions and touch noise */
extern uint32_t g_rng;
static inline double rand01(void) { return xs_d(&g_rng); }

/* LED position (mm, Q12) and star distance: the LEDs sit on an exact pitch grid */
static inline int32_t LXQ(int i) { return (led_gx[i] * PITCH_Q4) >> 2; }
static inline int32_t LYQ(int i) { return LED_YBASE_Q + ((led_gy[i] * PITCH_Q4) >> 2); }
static inline int32_t LDS(int i) { return (int32_t)led_ds[i] << 2; }
static inline uint32_t LTH(int i) { return (uint32_t)led_th[i] << 16; }      /* cone angle, turns */
static inline q12 LU(int i) { return (LXQ(i) * 3) / 66; }                     /* U = LX / 22 */
static inline q12 LBODY(int i) { return QC(1.0) - ((led_v[i] * 1434) >> 12); }   /* 0.65 + 0.35 (1 - V) */

static inline int led_at(int gx, int gy) {
  if (gx < GXMIN || gx > GXMAX || gy < GYMIN || gy > GYMAX) return -1;
  int i = grid[(gy - GYMIN) * GW + gx - GXMIN];
  return i == 255 ? -1 : i;
}

/* ---------- light shows (shows.c) ---------- */
typedef struct { double x, y, vy, ph, sw; } flake_t;
typedef struct { int32_t x, y; int16_t vy; uint16_t ph, a, life; } spark_t;
typedef struct { int32_t x, y; int16_t vx, vy; uint8_t age; uint16_t life; } fwpt_t;
typedef struct {
  uint8_t k;
  uint32_t rng;                             /* s.rnd */
  double t;                                 /* s.t, seconds */
  uint32_t n;                               /* frames stepped, t = n * FW_DT */
  union {
    struct { uint32_t tw[NLED]; uint8_t twe[NLED]; };   /* twinkle: frame | step << 8 | (peak - 0.45) << 19; end frame */
    struct { uint8_t lit[(NLED + 7) / 8]; int8_t depth[GW]; uint8_t nfl, melt; uint16_t total, mk; double spawn; flake_t fl[26]; } sn;
    struct { double x, y, h, sp, ph, w; } ff[7];
    struct { double spawn; uint8_t np; spark_t p[48]; } sp;
    struct { double next; uint8_t nrk, npt, nfl; struct { int32_t x, y, tx, ty; } rk[4]; fwpt_t pt[52];
             struct { int32_t x, y; q12 a; } fl[4]; } fw;
    struct { uint8_t i[9]; double t0[9], hold[9]; } orn;
  } u;
} show_t;
void show_init(show_t *s, uint8_t k, uint32_t seed);
void show_frame(show_t *s, uint16_t *out);            /* one 50 fps step; advances s->t */
q12 show_px(const show_t *s, const uint16_t *out, int i);   /* the show's output for LED i */
void splat(uint16_t *buf, int32_t x, int32_t y, q12 amp, q12 r);      /* soft blob, max-combine; mm Q12 */
void add_splat(uint16_t *buf, int32_t x, int32_t y, q12 amp, q12 r);

/* ---------- Pine's face (face.c) ---------- */
enum { F_IDLE, F_GIGGLE, F_SLEEP, F_STIR, F_WINK, F_SING, F_SNEEZE, F_DOZE, F_GRUMPY, F_EXCITED,
       F_SURPRISED, F_MORNING, F_PET, F_HUG, F_SQUIRM, F_TICKLED, F_PEEK, F_YAWN, F_COUNT };
typedef struct {
  double t;
  uint32_t rng;
  double bt, bl, dbl, gt, bp, bd, zt, nt, pt;   /* behaviour timers; NaN = not set yet (JS `undefined`) */
  int8_t lx, ly;
  uint8_t was_poked, nl;                        /* nl = entries in the list below */
  union {
    struct { int8_t x, y; float t; } sp[24];    /* sparkles (grid cell: the JS rounds a fixed position) */
    struct { double x, y, t; } z[4];            /* sleep z's */
    struct { double x, y, t, ph; } nt_[4];      /* music notes */
    double puff[8];                             /* grumpy puffs (age) */
  } l;
} face_t;
typedef struct { double mood, energy; uint8_t look, poked; } face_ctx_t;
extern const double face_dur[F_COUNT];          /* one-shot length in seconds, < 0 = loops */
void face_init(face_t *f, uint32_t seed);
uint8_t face_step(face_t *f, uint8_t beh, double dt, const face_ctx_t *c, uint16_t *fbuf);   /* 1 = timeline ended */

/* ---------- scenes, transitions, engine (scene.c) ---------- */
enum { K_SHOW, K_FACE };
enum { T_FADE, T_IRIS, T_BLINK, T_SPARKLE, T_SNOW, T_JELLY, T_RANDOM, T_NONE };
typedef struct scene {
  uint8_t used, kind, beh, done;                /* beh / done: face scenes */
  uint8_t has_ov, ov_beh, ov_fin;               /* face overlay on a show */
  double level, speed, acc, frozen;             /* show scenes */
  q12 level_q, env_q;
  show_t sh;
  face_t st;                                    /* face scene state, or the overlay's */
  uint16_t out[NLED], fbuf[NLED];
} scene_t;
scene_t *scene_new_show(uint8_t k, double level, double speed);
void go_show(scene_t *sc, uint8_t trans, double dur);           /* dur < 0: the transition's default */
void go_face(uint8_t beh, uint8_t trans, double dur);
uint8_t scene_overlay(uint8_t beh, double freeze);
scene_t *scene_now(void);
uint8_t scene_done(void);
void engine_tick(double dt);
q12 engine_px(int i);                           /* final perceived brightness of LED i for this frame */
void engine_end_frame(void);
void engine_reset(void);

/* ---------- touch star classifier (touch.c) and brain (brain.c) ---------- */
enum { G_HOVER, G_TAP, G_PRESS, G_SQUEEZE, G_TICKLE, G_PET_START, G_PET_END };
typedef struct {
  double raw, now, t0, hover_t, off_t, taps[8], last_t;
  uint8_t touching, pressed, squeezed, petting, hovering, ntaps, last;
} touch_t;
extern touch_t touch;
#define TH_HOVER 0.08
#define TH_TOUCH 0.35
#define TH_PRESS 0.65
#define TH_SQUEEZE 1.0
void touch_tick(double raw, double dt);         /* raw = sensor level in the simulator's units */

enum { S_MORNING, S_WORK, S_DAYDREAM, S_DOZE, S_SLEEP, S_STIR, S_GRUMPY, S_EXCITED, S_REMINDED, S_REACT };
typedef struct {
  double energy, focus, mood, annoy, excite, bond;
} emo_t;
typedef struct {
  emo_t E;
  uint8_t state, ambient, favorite, nrecent, recent[4], nwake, ntick, ret_state, has_ret;
  scene_t *ret_scene;
  double state_t, bt, clock, last_touch, poked_t, look_t, next_show_t, dd_t, dd_limit, beh_t, beh_dur;
  double sneeze_at, reluctant, save_t, wake_taps[8], tickles[8];
} brain_t;
extern brain_t brain;
void brain_init(double bond);
void brain_tick(double dt);
void brain_gesture(uint8_t g);
void brain_set_ambient(uint8_t on);
face_ctx_t brain_face_ctx(void);
void brain_scene_dropped(scene_t *sc);           /* the engine reuses a slot the brain still points at */

/* ---------- one frame (pine.c) ---------- */
void pine_init(double bond);
void pine_frame(double touch_raw, uint8_t *q);   /* q[i] = 8-bit BCM level of LED i (gamma 2.2) */
void pine_begin(double touch_raw);               /* the same frame in three steps, without a level buffer: */
uint8_t pine_level(int i);                       /*   level of LED i (call for each LED once) */
void pine_end(void);
extern uint16_t pine_mix[NLED];                  /* Q12 perceived brightness of the last frame (tests) */
