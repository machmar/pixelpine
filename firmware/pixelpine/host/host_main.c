/* PC test build of the firmware core (no hardware). Writes frames for test/compare.py to check against the
 * simulator's own JavaScript (test/simref.js) fed the same random numbers and the same touch input.
 *
 *   pine_host show <k> <seedCounter> <frames> <out.bin>
 *       one light show alone, seeded like the simulator's instance(k); per frame 240 x uint16 (Q12)
 *   pine_host run <rngSeed> <frames> <clock> <events> <out.bin>
 *       the whole Pine from power-on. events = "frame:gesture,..." (hover tap pet press squeeze tickle,
 *       dark, light). Per frame: 240 x uint16 mix (Q12), uint8 state, 6 x double emotions.
 * The touch sensor is the simulator's model (SCRIPTS + noise), drawing from the same random stream. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pine.h"

static double script_value(const char *g, double t, int *alive) {
  *alive = 1;
  if (!strcmp(g, "hover")) { if (t < 2.2) return 0.22; }
  else if (!strcmp(g, "tap")) { if (t < 0.15) return 0.5; if (t < 0.35) return 0; }
  else if (!strcmp(g, "pet")) { if (t < 2.6) return 0.5 + 0.05 * sin(t * 9); if (t < 2.8) return 0; }
  else if (!strcmp(g, "press")) { if (t < 0.9) return 0.82; if (t < 1.1) return 0; }
  else if (!strcmp(g, "squeeze")) { if (t < 1.0) return 1.32; if (t < 1.2) return 0; }
  else if (!strcmp(g, "tickle")) { if (t < 1.25) return fmod(t, 0.3) < 0.12 ? 0.5 : 0; }
  *alive = 0;
  return 0;
}

int main(int argc, char **argv) {
  if (argc >= 6 && !strcmp(argv[1], "show")) {
    int k = atoi(argv[2]); uint32_t c = (uint32_t)atoi(argv[3]); int frames = atoi(argv[4]);
    FILE *f = fopen(argv[5], "wb");
    static scene_t sc;
    show_init(&sc.sh, (uint8_t)k, 0x9e3779b9u ^ ((uint32_t)k * 7919u + c * 104729u));
    for (int n = 0; n < frames; n++) {
      show_frame(&sc.sh, sc.out);
      uint16_t v[NLED];
      for (int i = 0; i < NLED; i++) v[i] = (uint16_t)show_px(&sc.sh, sc.out, i);
      fwrite(v, 2, NLED, f);
    }
    fclose(f);
    return 0;
  }
  if (argc >= 7 && !strcmp(argv[1], "run")) {
    g_rng = (uint32_t)strtoul(argv[2], 0, 0);
    int frames = atoi(argv[3]);
    double clock = atof(argv[4]);
    char ev[4096]; strncpy(ev, argv[5], sizeof ev - 1); ev[sizeof ev - 1] = 0;
    FILE *f = fopen(argv[6], "wb");
    pine_init(0.3);
    brain.clock = clock;
    const char *script = 0; double script_t = 0, raw = 0;
    uint8_t q[NLED];
    for (int n = 0; n < frames; n++) {
      /* events due at this frame (applied between frames, like a click in the simulator) */
      char *p = ev;
      while (*p) {
        int fr = atoi(p); char *colon = strchr(p, ':'); if (!colon) break;
        char name[16] = {0}; int j = 0; char *s = colon + 1;
        while (*s && *s != ',' && j < 15) name[j++] = *s++;
        if (fr == n) {
          if (!strcmp(name, "dark")) brain_set_ambient(0);
          else if (!strcmp(name, "light")) brain_set_ambient(1);
          else { static char keep[16]; strcpy(keep, name); script = keep; script_t = touch.now; }
        }
        p = *s ? s + 1 : s;
      }
      /* touchTick's sensor model: target, first-order lag, noise from Math.random() */
      double now = touch.now + FW_DT, target = 0;
      if (script) { int alive; double v = script_value(script, now - script_t, &alive); if (alive) target = v; else script = 0; }
      raw += (target - raw) * fmin(1, FW_DT * 30) + (rand01() - 0.5) * 0.008;
      if (raw < 0) raw = 0;
      pine_frame(raw, q);
      fwrite(pine_mix, 2, NLED, f);
      fputc(brain.state, f);
      fwrite(&brain.E, sizeof(double), 6, f);
    }
    fclose(f);
    return 0;
  }
  if (argc >= 4 && !strcmp(argv[1], "math")) {        /* in: pairs (x, y) of doubles; out: sin x, cos x, x % y */
    FILE *f = fopen(argv[2], "rb"), *o = fopen(argv[3], "wb");
    double xy[2];
    while (fread(xy, 8, 2, f) == 2) {
      double r[3] = { jsin(xy[0]), jcos(xy[0]), jfmod(xy[0] < 0 ? -xy[0] : xy[0], xy[1]) };
      fwrite(r, 8, 3, o);
    }
    fclose(f); fclose(o);
    return 0;
  }
  fprintf(stderr, "usage: see the comment at the top of host/host_main.c\n");
  return 1;
}
