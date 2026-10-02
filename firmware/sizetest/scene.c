/* Scenes (a light show, optionally with a face overlay, or a face) and the 6 transitions.
 * RAM trick vs. the simulator: the OUTGOING scene is frozen into `snap` (240 B) instead of running two live
 * scenes, so only one show state (the union) is ever needed. NOT tested on hardware. */
#include "pp.h"

enum { K_SHOW, K_FACE };
static show_state_t st;
static face_state_t face, ovl;
static uint8_t snap[NLED], *last;
static uint8_t kind = K_FACE, show_k, level = 255, has_ovl, done;
static uint8_t trans = 255, tprog, tstep, tseed, last_trans = 255, morph;
static uint16_t t_show;

static void freeze(void) { if (last) for (uint8_t i = 0; i < NLED; i++) snap[i] = last[i]; }
static void begin(uint8_t type) {
  static const uint8_t len[6] = { 50, 70, 80, 45, 120, 70 };    /* frames */
  if (type == T_RANDOM) { do type = rnd8() % 5; while (type == last_trans); }
  last_trans = trans = type; tprog = 0; tstep = (uint8_t)(255 / len[type] + 1); tseed = rnd8();
}

void scene_show(uint8_t k, uint8_t lvl, uint8_t tr) {
  freeze(); begin(tr);
  kind = K_SHOW; show_k = k; level = lvl; has_ovl = 0; t_show = 0; done = 0;
  show_init(k, &st);
}
void scene_face(uint8_t beh, uint8_t tr) {
  if (kind == K_FACE && trans == 255) { freeze(); morph = 255; }    /* face -> face: morph, no wipe */
  else { freeze(); begin(tr); }
  kind = K_FACE; done = 0; face_start(&face, beh);
}
void scene_overlay(uint8_t beh) { if (kind == K_SHOW && !has_ovl && trans == 255) { has_ovl = 1; face_start(&ovl, beh); } }
uint8_t scene_done(void) { return done; }
uint8_t scene_is_show(void) { return kind == K_SHOW; }

static void transition(uint8_t *B) {                 /* snap = A (old, frozen), B = new (live) -> B */
  uint8_t f = tprog;
  switch (trans) {
  case T_IRIS: {
    uint16_t scale = (uint16_t)f * 24;               /* star radius multiplier, x256 */
    for (uint8_t i = 0; i < NLED; i++) {
      uint16_t R4 = (uint16_t)((STAR_R16(led_ang[i]) * scale) >> 10);   /* 0.25 mm */
      uint8_t d = led_ds4[i];
      B[i] = d + 6 < R4 ? B[i] : d < R4 + 2 ? max8(B[i], 230) : snap[i];
    }
    break; }
  case T_BLINK: {
    const int16_t yc = (LED_Y0_4 + LED_Y1_4) / 2, h = (LED_Y1_4 - LED_Y0_4) / 2 + 4;
    for (uint8_t i = 0; i < NLED; i++) {
      int16_t d = (int16_t)led_y4[i] - yc; if (d < 0) d = -d;
      int16_t open = f < 128 ? h - ((h * f) >> 7) : ((h * (f - 128)) >> 7);
      B[i] = d < open ? (f < 128 ? snap[i] : B[i]) : d < open + 4 ? 90 : 0;
    }
    break; }
  case T_SPARKLE:
    for (uint8_t i = 0; i < NLED; i++) { uint8_t h = perm[(uint8_t)(i + tseed)] ^ perm[i]; B[i] = h + 26 < f ? B[i] : h < f ? 255 : snap[i]; }
    break;
  case T_SNOW:
    for (uint8_t i = 0; i < NLED; i++) {
      uint8_t flake = (uint8_t)(115 + (perm[(uint8_t)(i + tseed)] >> 2));
      if (f < 128) B[i] = LV(i) > 255 - f * 2 ? flake : snap[i];
      else B[i] = LV(i) < (f - 128) * 2 ? B[i] : flake;
    }
    break;
  case T_JELLY: {
    uint8_t amp = (uint8_t)(usin8((uint8_t)(f >> 1)) >> 5);       /* 0..3 LEDs */
    for (uint8_t gy = 0; gy < GH; gy++) {
      int8_t sh = (int8_t)((sin8((uint8_t)(gy * 37 + tprog * 4)) * amp) >> 7);
      uint8_t rowB[GW];
      for (uint8_t gx = 0; gx < GW; gx++) { uint8_t i = grid[gy * GW + gx]; rowB[gx] = i == 255 ? 0 : B[i]; }
      for (uint8_t gx = 0; gx < GW; gx++) {
        uint8_t i = grid[gy * GW + gx]; if (i == 255) continue;
        int sa = gx - sh, sb = gx + sh;
        uint8_t a = (sa >= 0 && sa < GW && grid[gy * GW + sa] != 255) ? snap[grid[gy * GW + sa]] : 0;
        uint8_t b = (sb >= 0 && sb < GW) ? rowB[sb] : 0;
        B[i] = (uint8_t)(a + (((int16_t)b - a) * f >> 8));
      }
    }
    break; }
  default:
    for (uint8_t i = 0; i < NLED; i++) B[i] = (uint8_t)(snap[i] + (((int16_t)B[i] - snap[i]) * f >> 8));
  }
}

void scene_step(uint8_t *out) {
  last = out;
  if (kind == K_SHOW) {
    show_frame(show_k, &st, t_show++, out);
    if (level != 255) for (uint8_t i = 0; i < NLED; i++) out[i] = scale8(out[i], level);
    if (has_ovl) {                                    /* face pops into the show; snap is free (no transition) */
      for (uint8_t i = 0; i < NLED; i++) snap[i] = 0;
      uint8_t fin = face_frame(&ovl, snap, 255);
      uint8_t env = ovl.t < 18 ? (uint8_t)(ovl.t * 14) : ovl.t > 92 ? (uint8_t)((110 - ovl.t) * 14) : 255;
      for (uint8_t i = 0; i < NLED; i++) out[i] = max8(scale8(out[i], (uint8_t)(255 - scale8(env, 140))), scale8(snap[i], env));
      if (fin) has_ovl = 0;
    }
  } else {
    for (uint8_t i = 0; i < NLED; i++) out[i] = 0;
    if (face_frame(&face, out, 255)) done = 1;
    if (morph) {                                      /* ease each LED from the old expression */
      for (uint8_t i = 0; i < NLED; i++) out[i] = (uint8_t)(out[i] + (((int16_t)snap[i] - out[i]) * morph >> 8));
      morph = morph > 40 ? morph - 40 : 0;
    }
  }
  if (trans != 255) {
    transition(out);
    uint16_t np = (uint16_t)tprog + tstep;
    if (np >= 255) trans = 255; else tprog = (uint8_t)np;
  }
}
