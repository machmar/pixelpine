/* Pine's brain: 6 emotions (Q16) + behaviour state machine, integer port of the simulator's `brain`.
 * Rates are the "1x like the real tree" values; feelings update once per second. NOT tested on hardware. */
#include "pp.h"

enum { S_MORNING, S_WORK, S_DAYDREAM, S_DOZE, S_SLEEP, S_STIR, S_GRUMPY, S_EXCITED, S_REMINDED, S_REACT };
#define Q(x) ((uint16_t)((x) * 65535.0 + 0.5))
typedef struct { uint16_t energy, focus, mood, annoy, excite, bond; } emo_t;

static emo_t E;
static uint8_t state, ret_state, react_beh, fav, recent[4], nrec, sec, reluctant, wake_taps, tickles, sneeze_cd;
static uint16_t state_t, next_show, dd_t, dd_limit, beh_t, beh_dur, last_touch, save_t, saved_bond;

static void add(uint16_t *v, int32_t d) { int32_t x = (int32_t)*v + d; *v = (uint16_t)(x < 0 ? 0 : x > 65535 ? 65535 : x); }
static void set(uint8_t s) { state = s; state_t = 0; }
static uint8_t chance(uint16_t per_s_q16) { return (rnd32() & 0xFFFF) < per_s_q16 / FPS; }   /* per-frame event */

static uint8_t pick_show(void) {
  uint8_t k, n = 0, bad;
  do { k = rnd8() % NSHOWS; bad = 0; for (uint8_t i = 0; i < nrec; i++) if (recent[i] == k) bad = 1; } while (bad && n++ < 20);
  if (nrec < 4) recent[nrec++] = k; else { recent[0] = recent[1]; recent[1] = recent[2]; recent[2] = recent[3]; recent[3] = k; }
  return k;
}
static void work(int16_t k, uint8_t tr) {
  set(S_WORK);
  uint8_t lvl = (uint8_t)(140 + ((115u * (E.energy >> 8)) >> 8));
  if (reluctant) lvl = scale8(lvl, 184);
  scene_show(k < 0 ? pick_show() : (uint8_t)k, lvl, tr);
  next_show = 900 + (rnd8() % 200) * 2;
}
static void face(uint8_t beh, uint8_t s, uint8_t tr) { if (s != 255) set(s); scene_face(beh, tr); }
static void next_daydream(uint8_t tr) {
  uint16_t w_idle = 768, w_gig = E.mood >> 7, w_sing = (uint16_t)(((uint32_t)E.mood * E.energy) >> 23), w_wink = (E.bond >> 7) + 77;
  uint16_t w_sn = sneeze_cd ? 0 : 205, total = w_idle + w_gig + w_sing + w_wink + w_sn, r = (uint16_t)(rnd32() % total);
  uint8_t beh = r < w_idle ? F_IDLE : (r -= w_idle) < w_gig ? F_GIGGLE : (r -= w_gig) < w_sing ? F_SING : (r -= w_sing) < w_wink ? F_WINK : F_SNEEZE;
  if (beh == F_SNEEZE) sneeze_cd = 120;
  beh_t = 0; beh_dur = 400 + (rnd8() % 175) * 2;
  face(beh, 255, tr);
}
static void daydream(uint8_t tr) { set(S_DAYDREAM); dd_t = 0; dd_limit = 1250 + (rnd8() % 250) * 4; next_daydream(tr); }
static void react(uint8_t beh) { if (state != S_REACT) ret_state = state; react_beh = beh; set(S_REACT); face(beh, 255, T_FADE); }
static void wake_groggy(void) {
  reluctant = 60; add(&E.mood, -Q(0.08));
  if (E.energy < Q(0.4)) add(&E.annoy, Q(0.2));
  face(F_MORNING, S_MORNING, T_FADE);
}

void brain_init(void) {
  E = (emo_t){ Q(0.75), Q(0.9), Q(0.65), 0, Q(0.3), store_load_bond() };
  saved_bond = E.bond; fav = rnd8() % NSHOWS;
  face(F_MORNING, S_MORNING, T_FADE);
}

void brain_tick(uint8_t g, uint8_t petting, uint8_t dark) {
  state_t++; beh_t++; last_touch++;
  /* ---- feelings: once per second ---- */
  if (++sec >= FPS) {
    sec = 0;
    if (reluctant) reluctant--;
    if (sneeze_cd) sneeze_cd--;
    int32_t base = Q(0.45) + ((int32_t)E.bond * 23 >> 6) - ((int32_t)E.annoy * 29 >> 6) - (last_touch > 6000 ? Q(0.1) : 0);
    add(&E.mood, (base - (int32_t)E.mood) >> 8);
    add(&E.annoy, -262);
    add(&E.excite, -(int32_t)(E.excite >> 5));
    if (last_touch > 30000) add(&E.bond, -3);
    int32_t drain = dark ? 2 : 1;
    if (state == S_WORK) { add(&E.energy, -39 * drain); add(&E.focus, -(164 - (int32_t)(E.bond >> 10))); }
    if (state == S_DAYDREAM) { add(&E.energy, -26 * drain); add(&E.focus, 197); }
    if (state == S_SLEEP) add(&E.energy, (dark ? 492 : 328) * (petting ? 2 : 1));
    if (++save_t >= 60) { save_t = 0; if ((E.bond > saved_bond ? E.bond - saved_bond : saved_bond - E.bond) > 655) { store_save_bond(E.bond); saved_bond = E.bond; } }
  }
  if (petting) { add(&E.mood, 197); add(&E.annoy, -328); add(&E.bond, 5); }

  /* ---- gestures ---- */
  if (g != G_NONE) {
    uint8_t lonely = last_touch > 4500;
    last_touch = 0;
    switch (g) {
    case G_HOVER:
      add(&E.excite, Q(0.1)); add(&E.bond, 197);
      if (state == S_DAYDREAM && lonely && E.mood > Q(0.45) && E.energy > Q(0.4)) face(F_EXCITED, S_EXCITED, T_FADE);
      else if (state == S_WORK && E.bond > Q(0.35)) scene_overlay(F_PEEK);
      else if (state == S_GRUMPY) add(&E.annoy, -Q(0.05));
      break;
    case G_TAP:
      if (state == S_WORK) { add(&E.focus, Q(0.25)); add(&E.mood, Q(0.02)); work(-1, T_SPARKLE); }
      else if (state == S_DAYDREAM) face(F_SURPRISED, S_REMINDED, T_FADE);
      else if (state == S_SLEEP || state == S_DOZE || state == S_STIR) {
        add(&E.annoy, Q(0.15));
        if (E.energy > Q(0.7) || ++wake_taps >= 3) { wake_taps = 0; wake_groggy(); } else face(F_STIR, S_STIR, T_FADE);
      } else if (state == S_GRUMPY) add(&E.annoy, Q(0.08));
      else if (state == S_EXCITED) work(fav, T_IRIS);
      else if (state == S_MORNING) work(-1, T_IRIS);
      break;
    case G_PRESS:
      if (state == S_WORK) work(-1, T_SPARKLE);
      else if (state == S_DAYDREAM) face(F_SURPRISED, S_REMINDED, T_FADE);
      else if (state == S_SLEEP || state == S_DOZE || state == S_STIR) { add(&E.annoy, Q(0.35)); wake_groggy(); }
      else if (state == S_GRUMPY) add(&E.annoy, Q(0.2));
      break;
    case G_SQUEEZE:
      if (state == S_SLEEP || state == S_DOZE || state == S_STIR) { add(&E.energy, Q(0.2)); add(&E.annoy, Q(0.1)); react(F_SURPRISED); ret_state = S_DAYDREAM; }
      else if (E.annoy >= Q(0.5) || state == S_GRUMPY) { add(&E.annoy, -Q(0.2)); react(F_SQUIRM); }
      else { add(&E.mood, Q(0.25)); add(&E.bond, Q(0.05)); add(&E.excite, Q(0.3)); react(F_HUG); }
      break;
    case G_TICKLE:
      if (++tickles >= 3 || state == S_GRUMPY) { tickles = 0; add(&E.annoy, Q(0.45)); if (state != S_GRUMPY) react(F_SQUIRM); }
      else { add(&E.mood, Q(0.12)); add(&E.excite, Q(0.2)); add(&E.bond, Q(0.02)); react(F_TICKLED); }
      break;
    case G_PET_START:
      if (state != S_SLEEP && state != S_GRUMPY && state != S_REACT) react(F_PET);
      break;
    }
  }
  if (last_touch == 1250) tickles = 0;

  /* ---- behaviour ---- */
  if (E.annoy > Q(0.78) && state != S_GRUMPY && state != S_SLEEP && state != S_STIR && state != S_REACT) { face(F_GRUMPY, S_GRUMPY, T_FADE); return; }
  switch (state) {
  case S_MORNING:
    if (scene_done()) { if (reluctant) work(-1, T_BLINK); else if (E.mood > Q(0.6) && E.energy > Q(0.5)) face(F_EXCITED, S_EXCITED, T_FADE); else work(-1, T_IRIS); }
    break;
  case S_WORK:
    if (next_show) next_show--; else { work(-1, T_RANDOM); break; }
    if (E.energy < Q(0.35) && chance(Q(0.03))) scene_overlay(F_YAWN);
    else if (chance((uint16_t)((Q(1) - E.focus) / 50))) scene_overlay(F_PEEK);
    if (E.energy < Q(0.12)) { face(F_DOZE, S_DOZE, T_BLINK); break; }
    if (E.focus < Q(0.25) && chance(Q(0.12))) daydream(T_JELLY);
    break;
  case S_DAYDREAM:
    dd_t++;
    if (scene_done() || beh_t > beh_dur) next_daydream(T_FADE);
    if (dd_t > dd_limit || E.energy < Q(0.15)) {
      if (E.energy < Q(0.3)) face(F_DOZE, S_DOZE, T_BLINK);
      else if (E.mood > Q(0.65) && E.energy > Q(0.5) && (rnd8() & 1)) face(F_EXCITED, S_EXCITED, T_FADE);
      else if (E.focus > Q(0.6)) face(F_SURPRISED, S_REMINDED, T_FADE);
      else dd_t = dd_limit - 400;
    }
    break;
  case S_DOZE: if (scene_done()) face(F_SLEEP, S_SLEEP, T_BLINK); break;
  case S_STIR: if (scene_done()) face(F_SLEEP, S_SLEEP, T_FADE); break;
  case S_SLEEP: if (E.energy == 65535 && !dark && state_t > 1000) face(F_MORNING, S_MORNING, T_BLINK); break;
  case S_GRUMPY: if (E.annoy < Q(0.3)) daydream(T_FADE); break;
  case S_EXCITED: if (scene_done()) { work(fav, T_IRIS); E.focus = 65535; } break;
  case S_REMINDED: if (scene_done()) { work(-1, T_SPARKLE); E.focus = 65535; } break;
  case S_REACT:
    if (scene_done() || (react_beh == F_PET && !petting)) {
      if (ret_state == S_WORK) work(-1, T_FADE);
      else if (ret_state == S_SLEEP) face(F_SLEEP, S_SLEEP, T_FADE);
      else if (E.annoy > Q(0.75)) face(F_GRUMPY, S_GRUMPY, T_FADE);
      else daydream(T_FADE);
    }
    break;
  }
}
