/* Scenes, transitions and the engine: showScene / faceScene / stepScene / applyTransition / goTo / engineTick of
 * simulator/index.html. Two differences, both invisible:
 *  - Scenes live in a pool of two slots (the JS allocates objects). At most two scenes are ever alive (the
 *    current one and the one a transition is fading to); the brain's saved `ret` scene is always one of them.
 *  - The simulator renders every scene into a frame buffer (A, B, mix) and then combines them. Here each LED's
 *    final value is computed once, at the end of the frame, straight from the scenes' own buffers (engine_px):
 *    same arithmetic, 1.4 KB less RAM. */
#include <math.h>
#include "pine.h"

static scene_t slot[2];
static struct {
  scene_t *cur, *to;                 /* eng.cur, eng.tr.to (NULL = no transition) */
  uint8_t type, last_type, finish;
  double dur, t, et;                 /* transition length and progress, eng.t */
  uint32_t seed;
  /* per-frame transition parameters for engine_px() */
  q12 s, a1, a2, a3;
  uint64_t h1, h2;
  int8_t sh[GH];
} eng = { .last_type = T_NONE };
static uint32_t seed_counter = 1, scene_seed = 1;
static const double TRANS_DUR[6] = { 1.0, 1.4, 1.6, 0.9, 2.4, 1.4 };

void engine_reset(void) {
  slot[0].used = slot[1].used = 0;
  eng.cur = eng.to = 0; eng.last_type = T_NONE; eng.et = 0; eng.finish = 0;
  seed_counter = 1; scene_seed = 1;
}

static void promote(void) { if (eng.to) { eng.cur->used = 0; eng.cur = eng.to; eng.to = 0; eng.finish = 0; } }

static scene_t *alloc(void) {
  promote();
  for (int k = 0; k < 2; k++)
    if (&slot[k] != eng.cur && &slot[k] != brain.ret_scene) return &slot[k];
  for (int k = 0; k < 2; k++)                         /* never happens with the simulator's brain; just in case */
    if (&slot[k] != eng.cur) { brain_scene_dropped(&slot[k]); return &slot[k]; }
  return &slot[0];
}

scene_t *scene_new_show(uint8_t k, double level, double speed) {
  scene_t *sc = alloc();
  sc->used = 1; sc->kind = K_SHOW; sc->has_ov = 0; sc->ov_fin = 0; sc->done = 0;
  sc->level = level; sc->speed = speed; sc->acc = 0; sc->frozen = 0;
  for (int i = 0; i < NLED; i++) sc->out[i] = sc->fbuf[i] = 0;
  show_init(&sc->sh, k, 0x9e3779b9u ^ ((uint32_t)k * 7919u + seed_counter++ * 104729u));
  return sc;
}

static void start(scene_t *sc, uint8_t type, double dur) {
  if (type == T_RANDOM) {
    uint8_t pool[5], n = 0;
    static const uint8_t all[5] = { T_FADE, T_IRIS, T_SPARKLE, T_SNOW, T_JELLY };
    for (int k = 0; k < 5; k++) if (all[k] != eng.last_type) pool[n++] = all[k];
    type = pool[(uint32_t)(rand01() * n)];
  }
  eng.last_type = type;
  eng.to = sc; sc->used = 1; eng.type = type; eng.t = 0; eng.finish = 0;
  eng.dur = dur >= 0 ? dur : TRANS_DUR[type];
  eng.seed = (uint32_t)floor(rand01() * 1e6);
}

void go_show(scene_t *sc, uint8_t trans, double dur) {
  promote();
  sc->used = 1;
  if (!eng.cur) { eng.cur = sc; return; }
  start(sc, trans, dur);
}

void go_face(uint8_t beh, uint8_t trans, double dur) {
  uint32_t seed = 0xabc123u ^ (scene_seed++ * 7919u);
  promote();
  if (eng.cur && eng.cur->kind == K_FACE) {            /* face -> face: morph, no wipe */
    eng.cur->beh = beh; face_init(&eng.cur->st, seed); eng.cur->done = 0;
    return;
  }
  scene_t *sc = alloc();
  sc->used = 1; sc->kind = K_FACE; sc->beh = beh; sc->done = 0; sc->has_ov = 0;
  face_init(&sc->st, seed);
  for (int i = 0; i < NLED; i++) sc->fbuf[i] = 0;
  if (!eng.cur) { eng.cur = sc; return; }
  start(sc, trans, dur);
}

scene_t *scene_now(void) { return eng.to ? eng.to : eng.cur; }
uint8_t scene_done(void) { scene_t *s = scene_now(); return s->kind == K_FACE && s->done; }

uint8_t scene_overlay(uint8_t beh, double freeze) {
  scene_t *sc = scene_now();
  if (sc->kind != K_SHOW || sc->has_ov) return 0;
  sc->has_ov = 1; sc->ov_beh = beh; sc->ov_fin = 0;
  face_init(&sc->st, scene_seed++ * 104729u);
  if (freeze) sc->frozen = freeze;
  return 1;
}

static void step_scene(scene_t *sc, double dt) {
  face_ctx_t c = brain_face_ctx();
  if (sc->kind == K_SHOW) {
    if (sc->frozen > 0) sc->frozen -= dt;
    else { sc->acc += dt * sc->speed; while (sc->acc >= FW_DT) { sc->acc -= FW_DT; show_frame(&sc->sh, sc->out); } }
    sc->level_q = dq(sc->level);
    if (sc->has_ov) {
      uint8_t fin = face_step(&sc->st, sc->ov_beh, dt, &c, sc->fbuf);
      double d = face_dur[sc->ov_beh] >= 0 ? face_dur[sc->ov_beh] : 2, t = sc->st.t;
      double env = fmin(1, fmin(t / 0.35, fmax(0, d - t) / 0.35));
      sc->env_q = dq(env);
      if (fin) sc->ov_fin = 1;
    }
  } else if (face_step(&sc->st, sc->beh, dt, &c, sc->fbuf)) sc->done = 1;
}

static q12 scene_px(const scene_t *sc, int i) {
  if (sc->kind == K_FACE) return sc->fbuf[i];
  q12 v = qm(show_px(&sc->sh, sc->out, i), sc->level_q);
  if (sc->has_ov) v = qmax(qm(v, ONE - qm(QC(0.55), sc->env_q)), qm(sc->fbuf[i], sc->env_q));
  return v;
}

static double smooth_d(double e0, double e1, double x) {
  double t = (x - e0) / (e1 - e0);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}
static uint64_t thr(double x) { return x <= 0 ? 0 : (uint64_t)ceil(x * 4294967296.0); }   /* hash < x  <=>  u < thr */

void engine_tick(double dt) {
  eng.et += dt;
  step_scene(eng.cur, dt);
  if (!eng.to) return;
  step_scene(eng.to, dt);
  eng.t += dt;
  double f = eng.t / eng.dur;
  if (f > 1) f = 1;
  double s = smooth_d(0, 1, f);
  eng.s = dq(s);
  switch (eng.type) {
  case T_IRIS: eng.a1 = dq(s * 24); break;
  case T_BLINK:
    if (f < 0.5) { eng.a1 = 0; eng.a2 = dq(1 - smooth_d(0, 0.5, f)); }
    else { eng.a1 = 1; eng.a2 = dq(smooth_d(0.55, 1, f)); }
    break;
  case T_SPARKLE: eng.h1 = thr(f - 0.1); eng.h2 = thr(f); break;
  case T_SNOW:
    if (f < 0.5) { eng.a1 = 0; eng.a2 = dq(1 - smooth_d(0, 0.5, f) * 1.1); }
    else { eng.a1 = 1; eng.a2 = dq(smooth_d(0.5, 1, f) * 1.1 - 0.05); eng.a3 = dq(1 - smooth_d(0.85, 1, f)); }
    break;
  case T_JELLY: {
    double amp = 2.4 * dsin(M_PI * f);
    for (int g = 0; g < GH; g++) eng.sh[g] = (int8_t)floor(dsin((g + GYMIN) * 0.9 + eng.et * 14) * amp + 0.5);
    break;
  }
  }
  if (f >= 1) eng.finish = 1;
}

q12 engine_px(int i) {
  if (!eng.to) return scene_px(eng.cur, i);
  const scene_t *A = eng.cur, *B = eng.to;
  switch (eng.type) {
  case T_IRIS: {
    q12 R = qm(led_starr[i], eng.a1), d = LDS(i);
    return d < R - QC(1.6) ? scene_px(B, i) : d < R + QC(0.4) ? qmax(scene_px(B, i), QC(0.9)) : scene_px(A, i);
  }
  case T_BLINK: {
    q12 d = (q12)((qabs(LYQ(i) - BLINK_C_Q) * (int64_t)BLINK_INVH_Q16) >> 16);   /* |LY - VC| / VH */
    if (!eng.a1) return d < eng.a2 ? scene_px(A, i) : d < eng.a2 + QC(0.07) ? QC(0.35) : 0;
    return d < eng.a2 ? scene_px(B, i) : d < eng.a2 + QC(0.07) ? QC(0.35) : 0;
  }
  case T_SPARKLE: {
    uint64_t h = hash1u((uint32_t)i * 7u + eng.seed);
    return h < eng.h1 ? scene_px(B, i) : h < eng.h2 ? ONE : scene_px(A, i);
  }
  case T_SNOW: {
    q12 flake = QC(0.45) + qm(QC(0.2), (q12)(hash1u((uint32_t)i + eng.seed) >> 20));
    if (!eng.a1) return led_v[i] > eng.a2 ? flake : scene_px(A, i);
    return led_v[i] < eng.a2 ? scene_px(B, i) : qm(flake, eng.a3);
  }
  case T_JELLY: {
    int gx = led_gx[i], gy = led_gy[i], sh = eng.sh[gy - GYMIN];
    int a = led_at(gx - sh, gy), b = led_at(gx + sh, gy);
    return qm(a >= 0 ? scene_px(A, a) : 0, ONE - eng.s) + qm(b >= 0 ? scene_px(B, b) : 0, eng.s);
  }
  default:
    return qm(scene_px(A, i), ONE - eng.s) + qm(scene_px(B, i), eng.s);
  }
}

void engine_end_frame(void) {
  if (eng.cur->ov_fin) { eng.cur->has_ov = 0; eng.cur->ov_fin = 0; }
  if (eng.to && eng.to->ov_fin) { eng.to->has_ov = 0; eng.to->ov_fin = 0; }
  if (eng.finish) { eng.cur->used = 0; eng.cur = eng.to; eng.to = 0; eng.finish = 0; }
}
