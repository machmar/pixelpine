/* The touch star's gesture classifier: touchTick() of simulator/index.html, after the sensor model.
 * raw = signal in the simulator's units (hover 0.08 < touch 0.35 < press 0.65 < squeeze 1.0); the board's
 * driver (hw/sense.c) and the PC test build (host/) each supply it. */
#include "pine.h"

touch_t touch;
volatile uint32_t touch_events;                 /* gesture count and last gesture, for the debugger */
volatile uint8_t touch_event_last;

static void emit(uint8_t g) {
  touch.last = g; touch.last_t = touch.now;
  touch_events++; touch_event_last = g;
  brain_gesture(g);
}

void touch_tick(double raw, double dt) {
  touch.now += dt;
  touch.raw = raw;
  double r = raw, now = touch.now;
  if (!touch.touching && r >= TH_TOUCH) {
    touch.touching = 1; touch.t0 = now; touch.pressed = touch.squeezed = touch.petting = touch.deep = 0;
  }
  if (touch.touching) {
    double d = now - touch.t0;
    /* A real squeeze climbs through the press band for up to 1.3 s before it reaches squeeze level (recorded on the
     * board, 2026-11-04: all 8 squeezes fired "press" first, which Pine answers like a tap). So a press is only
     * decided on release, or after 1.5 s held in the band; a touch that went that deep is never a pet. */
    if (r >= TH_PRESS && !touch.deep) { touch.deep = 1; touch.deep_t = now; }
    if (r >= TH_SQUEEZE && !touch.squeezed && d > 0.08) { touch.squeezed = 1; emit(G_SQUEEZE); }
    else if (r >= TH_PRESS && r < TH_SQUEEZE && !touch.pressed && !touch.squeezed && !touch.petting &&
             now - touch.deep_t > 1.5) { touch.pressed = 1; emit(G_PRESS); }
    if (!touch.pressed && !touch.squeezed && !touch.petting && !touch.deep && d > 0.6) { touch.petting = 1; emit(G_PET_START); }
    if (r < TH_TOUCH * 0.8) {
      touch.touching = 0;
      if (touch.petting) { touch.petting = 0; emit(G_PET_END); }
      else if (touch.deep && !touch.pressed && !touch.squeezed && d > 0.15) { touch.pressed = 1; emit(G_PRESS); }
      else if (!touch.pressed && !touch.squeezed && d < 0.45) {
        uint8_t n = 0;
        for (uint8_t k = 0; k < touch.ntaps; k++) if (now - touch.taps[k] < 1.4) touch.taps[n++] = touch.taps[k];
        touch.ntaps = n;
        if (touch.ntaps < 8) touch.taps[touch.ntaps++] = now;
        if (touch.ntaps >= 3) { touch.ntaps = 0; emit(G_TICKLE); } else emit(G_TAP);
      }
    }
    touch.hover_t = 0;
  } else if (r >= TH_HOVER) {
    touch.hover_t += dt; touch.off_t = 0;
    if (touch.hover_t > 0.3 && !touch.hovering) { touch.hovering = 1; emit(G_HOVER); }
  } else {
    touch.off_t += dt;
    if (touch.off_t > 1) { touch.hovering = 0; touch.hover_t = 0; }
  }
}
