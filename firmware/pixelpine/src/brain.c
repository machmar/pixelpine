/* Pine's brain: emotions + state machine, the `brain` object of simulator/index.html, line for line.
 * All in double, as in the JS, with Math.random() = rand01(). The speech bubbles are preview-only (the real Pine
 * "talks with his face"), but say() still draws its random line so the random stream stays in step with the
 * simulator. Pine's clock runs at 1x, the simulator's "1x like the real tree" setting. */
#include <math.h>
#include "pine.h"

brain_t brain;

static double clampd(double x, double a, double b) { return x < a ? a : x > b ? b : x; }
static void say(void) { (void)rand01(); }            /* say(key) of a LINES key: pick() draws one random */
static void set(uint8_t s) { brain.state = s; brain.state_t = 0; }

static uint8_t pick_show(void) {
  uint8_t k, n = 0, seen;
  do {
    k = (uint8_t)(rand01() * NSHOWS);
    seen = 0;
    for (uint8_t j = 0; j < brain.nrecent; j++) if (brain.recent[j] == k) seen = 1;
  } while (seen && n++ < 20);
  if (brain.nrecent < 4) brain.recent[brain.nrecent++] = k;
  else { for (int j = 0; j < 3; j++) brain.recent[j] = brain.recent[j + 1]; brain.recent[3] = k; }
  return k;
}

/* work({k, trans, dur, focus}); k < 0 = pick one, trans T_RANDOM = Pine's choice, dur < 0 = default */
static void work(int k, uint8_t trans, double dur, double focus) {
  brain_t *b = &brain;
  set(S_WORK);
  uint8_t show = k < 0 ? pick_show() : (uint8_t)k;
  double tired = 0.55 + 0.45 * b->E.energy;
  scene_t *sc = scene_new_show(show, tired * (b->reluctant > 0 ? 0.72 : 1), b->reluctant > 0 ? 0.8 : 1);
  go_show(sc, trans, dur);
  b->next_show_t = 18 + rand01() * 8;
  if (focus > 0) b->E.focus = focus;
}
static void face(uint8_t beh, int state, uint8_t trans) { if (state >= 0) set((uint8_t)state); go_face(beh, trans, -1); }

static void next_daydream(uint8_t trans) {
  brain_t *b = &brain;
  emo_t *E = &b->E;
  static const uint8_t beh_of[5] = { F_IDLE, F_GIGGLE, F_SING, F_WINK, F_SNEEZE };
  double w[5] = { 3, 2 * E->mood, 2 * E->mood * E->energy, 1.5 * E->bond + 0.3, 0.8 };
  int nw = b->bt - b->sneeze_at > 120 ? 5 : 4;
  double sum = 0;
  for (int j = 0; j < nw; j++) sum += w[j];
  double r = rand01() * sum;
  uint8_t beh = F_IDLE;
  for (int j = 0; j < nw; j++) if ((r -= w[j]) <= 0) { beh = beh_of[j]; break; }
  if (beh == F_SNEEZE) { b->sneeze_at = b->bt; say(); }
  else if (beh != F_IDLE && rand01() < 0.5) say();
  b->beh_t = 0; b->beh_dur = 8 + rand01() * 7;
  face(beh, -1, trans);
}
static void daydream(uint8_t trans) {
  set(S_DAYDREAM); brain.dd_t = 0; brain.dd_limit = 25 + rand01() * 20;
  next_daydream(trans);
}
static void react(uint8_t beh) {
  if (!brain.has_ret) { brain.has_ret = 1; brain.ret_state = brain.state; brain.ret_scene = scene_now(); }
  set(S_REACT);
  go_face(beh, T_FADE, 0.3);
}
static void ret_daydream(void) { brain.has_ret = 1; brain.ret_state = S_DAYDREAM; brain.ret_scene = 0; }
static void end_react(void) {
  brain_t *b = &brain;
  uint8_t had = b->has_ret, st = b->ret_state;
  scene_t *sc = b->ret_scene;
  b->has_ret = 0; b->ret_scene = 0;
  if (!had) { daydream(T_FADE); return; }
  if (st == S_WORK && sc && sc->kind == K_SHOW) { set(S_WORK); go_show(sc, T_FADE, 0.6); return; }
  if (st == S_GRUMPY || b->E.annoy > 0.75) { face(F_GRUMPY, S_GRUMPY, T_FADE); return; }
  if (st == S_SLEEP) { face(F_SLEEP, S_SLEEP, T_FADE); return; }
  set(S_DAYDREAM); b->dd_t = 0; next_daydream(T_FADE);
}
static void to_sleep(void) { face(F_SLEEP, S_SLEEP, T_BLINK); }
static void to_doze(void) { say(); face(F_DOZE, S_DOZE, T_BLINK); }
static void reminded(void) { say(); face(F_SURPRISED, S_REMINDED, T_FADE); }
static void excited(uint8_t keyed) { if (keyed) say(); face(F_EXCITED, S_EXCITED, T_FADE); }
static void wake_groggy(void) {
  brain_t *b = &brain;
  say(); b->reluctant = 60; b->E.mood -= 0.08;
  if (b->E.energy < 0.4) b->E.annoy += 0.2;           /* woken while really tired ("I was sleeping!") */
  set(S_MORNING); go_face(F_MORNING, T_FADE, -1);
}

void brain_init(double bond) {
  brain_t *b = &brain;
  b->E.energy = 0.75; b->E.focus = 0.9; b->E.mood = 0.65; b->E.annoy = 0; b->E.excite = 0.3; b->E.bond = bond;
  b->state = S_MORNING; b->state_t = 0; b->bt = 0; b->clock = 1; b->ambient = 1;
  b->last_touch = 0; b->poked_t = 0; b->look_t = 0;
  b->nrecent = 0; b->favorite = (uint8_t)(rand01() * NSHOWS); b->next_show_t = 20;
  b->dd_t = 0; b->dd_limit = 30; b->beh_t = 0; b->beh_dur = 10; b->sneeze_at = -999;
  b->nwake = 0; b->ntick = 0; b->reluctant = 0; b->has_ret = 0; b->ret_scene = 0; b->save_t = 0;
}

void brain_scene_dropped(scene_t *sc) {
  if (brain.ret_scene == sc) { brain.ret_scene = 0; if (brain.ret_state == S_WORK) brain.ret_state = S_DAYDREAM; }
}

face_ctx_t brain_face_ctx(void) {
  face_ctx_t c = { brain.E.mood, brain.E.energy, brain.look_t > 0, brain.poked_t > 0 };
  return c;
}

void brain_tick(double dt) {
  brain_t *b = &brain;
  emo_t *E = &b->E;
  double dtB = dt * b->clock;
  b->poked_t -= dt; b->look_t -= dt; b->reluctant -= dt;
  b->save_t += dt;
  /* feelings run on Pine's clock (dtB); pacing of shows, daydreams and reactions stays real time (dt) */
  b->bt += dt; b->state_t += dt;
  double dark = b->ambient ? 1 : 2;
  double lonely = b->bt - b->last_touch > 120 ? 0.1 : 0;
  double base = 0.45 + 0.35 * E->bond - 0.45 * E->annoy - lonely;
  E->mood += (base - E->mood) * 0.005 * dtB;
  E->annoy -= 0.004 * dtB;
  E->excite -= E->excite * 0.03 * dtB;
  if (b->bt - b->last_touch > 600) E->bond -= 0.00005 * dtB;
  if (touch.petting) { E->mood += 0.15 * dt; E->annoy -= 0.25 * dt; E->bond += 0.004 * dt; }
  E->energy = clampd(E->energy, 0, 1); E->focus = clampd(E->focus, 0, 1); E->mood = clampd(E->mood, 0, 1);
  E->annoy = clampd(E->annoy, 0, 1); E->excite = clampd(E->excite, 0, 1); E->bond = clampd(E->bond, 0.05, 1);

  uint8_t s = b->state;
  if (E->annoy > 0.78 && s != S_GRUMPY && s != S_SLEEP && s != S_STIR && s != S_REACT) {
    say(); face(F_GRUMPY, S_GRUMPY, T_FADE); return;
  }
  switch (s) {
  case S_MORNING:
    if (scene_done()) {
      if (b->reluctant > 0) work(-1, T_BLINK, -1, 0);
      else if (E->mood > 0.6 && E->energy > 0.5) excited(1);
      else work(-1, T_IRIS, -1, 0);
    }
    break;
  case S_WORK: {
    E->energy -= 0.0006 * dtB * dark;
    E->focus -= 0.002 * dtB * (1.25 - 0.5 * E->bond);
    if ((b->next_show_t -= dt) < 0) { if (rand01() < 0.4) say(); work(-1, T_RANDOM, -1, 0); break; }
    double r = rand01();
    if (E->energy < 0.35 && r < dt * 0.03) scene_overlay(F_YAWN, 0);
    else if (r < dt * 0.02 * (1 - E->focus)) { if (scene_overlay(F_PEEK, 0)) say(); }
    else if (rand01() < dt * 0.01 * (1 - E->focus)) scene_overlay(F_SURPRISED, 1.4);
    if (E->energy < 0.12) { to_doze(); break; }
    if (E->focus < 0.25 && rand01() < dt * 0.12) { say(); daydream(T_JELLY); }
    break;
  }
  case S_DAYDREAM: {
    E->focus += 0.003 * dtB; E->energy -= 0.0004 * dtB * dark;
    b->dd_t += dt; b->beh_t += dt;
    scene_t *now = scene_now();
    uint8_t loops = now->kind != K_FACE || face_dur[now->beh] < 0;
    if (scene_done() || (loops && b->beh_t > b->beh_dur)) next_daydream(T_FADE);
    if (b->dd_t > b->dd_limit || E->energy < 0.15) {
      if (E->energy < 0.3) to_doze();
      else if (E->mood > 0.65 && E->energy > 0.5 && rand01() < 0.5) excited(1);
      else if (E->focus > 0.6) reminded();
      else b->dd_t = b->dd_limit - 8;
    }
    break;
  }
  case S_DOZE: if (scene_done()) to_sleep(); break;
  case S_STIR: if (scene_done()) face(F_SLEEP, S_SLEEP, T_FADE); break;
  case S_SLEEP:
    E->energy += 0.005 * dtB * (b->ambient ? 1 : 1.5) * (touch.petting ? 2 : 1);
    if (E->energy >= 1 && b->ambient && b->state_t > 20) { say(); face(F_MORNING, S_MORNING, T_BLINK); }
    break;
  case S_GRUMPY:
    if (E->annoy < 0.3) { say(); set(S_DAYDREAM); b->dd_t = 0; next_daydream(T_FADE); }
    break;
  case S_EXCITED: if (scene_done()) work(b->favorite, T_IRIS, -1, 1); break;
  case S_REMINDED: if (scene_done()) work(-1, T_SPARKLE, -1, 1); break;
  case S_REACT: {
    scene_t *now = scene_now();
    uint8_t petface = now->kind == K_FACE && now->beh == F_PET;
    if (touch.petting && petface) break;
    if (scene_done() || (petface && !touch.petting)) end_react();
    break;
  }
  }
}

void brain_gesture(uint8_t g) {
  brain_t *b = &brain;
  emo_t *E = &b->E;
  uint8_t s = b->state;
  uint8_t was_lonely = b->bt - b->last_touch > 90;
  b->last_touch = b->bt;
  uint8_t asleep = s == S_SLEEP || s == S_DOZE || s == S_STIR;
  switch (g) {
  case G_HOVER:
    E->excite += 0.1; E->bond += 0.003;
    if (s == S_DAYDREAM) {
      b->look_t = 3;
      if (was_lonely && E->mood > 0.45 && E->energy > 0.4) excited(0);      /* "you're back!" */
      else if (rand01() < 0.6) say();
    } else if (s == S_WORK) { if (E->bond > 0.35 && scene_overlay(F_PEEK, 0)) say(); }
    else if (s == S_SLEEP) say();
    else if (s == S_GRUMPY) E->annoy -= 0.05;
    break;
  case G_TAP:
    if (s == S_WORK) { E->focus = fmin(1, E->focus + 0.25); E->mood += 0.02; say(); work(-1, T_SPARKLE, -1, 0); }
    else if (s == S_DAYDREAM) reminded();
    else if (asleep) {
      E->annoy += 0.15;
      uint8_t n = 0;
      for (uint8_t k = 0; k < b->nwake; k++) if (b->bt - b->wake_taps[k] < 30) b->wake_taps[n++] = b->wake_taps[k];
      b->nwake = n;
      if (b->nwake < 8) b->wake_taps[b->nwake++] = b->bt;
      if (E->energy > 0.7 || b->nwake >= 3) { b->nwake = 0; wake_groggy(); }
      else { say(); face(F_STIR, S_STIR, T_FADE); }
    } else if (s == S_GRUMPY) { E->annoy += 0.08; b->poked_t = 0.6; say(); }
    else if (s == S_EXCITED) work(b->favorite, T_IRIS, -1, 1);
    else if (s == S_MORNING) work(-1, T_IRIS, -1, 0);
    break;
  case G_PRESS:
    if (s == S_WORK) { say(); work(-1, T_SPARKLE, -1, 0); }
    else if (s == S_DAYDREAM) reminded();
    else if (asleep) { E->annoy += 0.35; wake_groggy(); }
    else if (s == S_GRUMPY) { E->annoy += 0.2; b->poked_t = 0.8; }               /* "HEY!" */
    break;
  case G_SQUEEZE:
    if (asleep) { E->energy += 0.2; E->annoy += 0.1; react(F_SURPRISED); ret_daydream(); }
    else if (E->annoy >= 0.5 || s == S_GRUMPY) { E->annoy -= 0.2; say(); react(F_SQUIRM); }
    else { E->mood += 0.25; E->bond += 0.05; E->excite += 0.3; say(); react(F_HUG); }
    break;
  case G_TICKLE: {
    uint8_t n = 0;
    for (uint8_t k = 0; k < b->ntick; k++) if (b->bt - b->tickles[k] < 25) b->tickles[n++] = b->tickles[k];
    b->ntick = n;
    if (b->ntick < 8) b->tickles[b->ntick++] = b->bt;
    if (b->ntick >= 3 || s == S_GRUMPY) { E->annoy += 0.45; say(); if (s != S_GRUMPY) react(F_SQUIRM); }
    else if (s == S_SLEEP) { E->energy += 0.15; say(); react(F_TICKLED); ret_daydream(); }
    else { E->mood += 0.12; E->excite += 0.2; E->bond += 0.02; say(); react(F_TICKLED); }
    break;
  }
  case G_PET_START:
    if (s == S_SLEEP) { /* "mmm..." */ }
    else if (s == S_GRUMPY) { /* he stays turned away until the annoyance melts */ }
    else if (s != S_REACT) { say(); react(F_PET); }
    break;
  case G_PET_END:
    break;
  }
}

void brain_set_ambient(uint8_t on) {
  brain_t *b = &brain;
  b->ambient = on;
  if (!on && (b->state == S_WORK || b->state == S_DAYDREAM)) { say(); b->E.energy -= 0.15; }
  if (on && b->state == S_SLEEP && rand01() < 0.6) { say(); face(F_MORNING, S_MORNING, T_BLINK); }
}
