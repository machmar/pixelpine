/* One 50 fps frame, as the simulator's main loop: touchTick, brain.tick, engineTick, then the hardware model's
 * gamma 2.2 + 8-bit quantisation (the scan driver turns the 8-bit levels into BCM slots). */
#include "pine.h"

uint32_t g_rng = 1;
#ifdef PINE_HOST
uint16_t pine_mix[NLED];
#endif

void pine_init(double bond) {
  engine_reset();
  brain_init(bond);
  go_face(F_MORNING, T_FADE, -1);                   /* brain.face("morning", "morning") */
}

void pine_begin(double touch_raw) {
  touch_tick(touch_raw, FW_DT);
  brain_tick(FW_DT);
  engine_tick(FW_DT);
}
uint8_t pine_level(int i) {
  q12 v = qclamp(engine_px(i));
#ifdef PINE_HOST
  pine_mix[i] = (uint16_t)v;
#endif
  return gamma8(v);
}
void pine_end(void) { engine_end_frame(); }

void pine_frame(double touch_raw, uint8_t *q) {
  pine_begin(touch_raw);
  for (int i = 0; i < NLED; i++) q[i] = pine_level(i);
  pine_end();
}
