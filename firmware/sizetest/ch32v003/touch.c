/* Touch star (ADC charge sharing on PD4) + gesture classifier + LED ambient-light sensing.
 * Thresholds mirror the simulator's TH table (x256). NOT tested on hardware. */
#include "pp.h"
#include "regs.h"

#define TH_HOVER 20
#define TH_TOUCH 90
#define TH_PRESS 166
#define TH_SQUEEZE 256

static uint16_t adc_convert(uint8_t ch) {
  ADC1->RSQR3 = ch;
  ADC1->CTLR2 |= 1u << 22;                          /* SWSTART */
  while (!(ADC1->STATR & 2u)) {}
  return (uint16_t)ADC1->RDATAR;
}

/* discharge the electrode, precharge the ADC sample cap from Vrefint, then let them share charge */
uint16_t touch_sample(void) {
  TIM2->DMAINTENR = 0;                              /* the scan ISR also writes GPIOD->CFGLR */
  GPIOD->CFGLR = (GPIOD->CFGLR & ~(0xFu << 16)) | (0x2u << 16);
  GPIOD->BCR = 1u << 4;
  (void)adc_convert(8);
  GPIOD->CFGLR &= ~(0xFu << 16);                    /* analog input */
  uint16_t v = adc_convert(7);
  TIM2->DMAINTENR = 1;
  return v;
}

static uint32_t base;                               /* baseline, x16 */
static uint16_t touch_t0, now, hover_t, off_t, taps[3];
static uint8_t touching, pressed, squeezed, petting_, hovering, ntaps;

uint8_t touch_tick(uint8_t *petting) {
  now++;
  uint16_t raw = touch_sample();
  if (!base) base = (uint32_t)raw << 4;
  int32_t d = (int32_t)(base >> 4) - raw;           /* more capacitance -> lower shared voltage */
  if (d < 0) d = 0;
  uint16_t lvl = (uint16_t)(d > 400 ? 400 : d);     /* ~256 = full fingertip */
  if (lvl < TH_HOVER) base += (int32_t)raw - (int32_t)(base >> 4);   /* track drift only when idle */
  uint8_t g = G_NONE;
  if (!touching && lvl >= TH_TOUCH) { touching = 1; touch_t0 = now; pressed = squeezed = petting_ = 0; }
  if (touching) {
    uint16_t dur = now - touch_t0;
    if (lvl >= TH_SQUEEZE && !squeezed && dur > 4) { squeezed = 1; g = G_SQUEEZE; }
    else if (lvl >= TH_PRESS && lvl < TH_SQUEEZE && !pressed && !squeezed && dur > 8) { pressed = 1; g = G_PRESS; }
    if (!pressed && !squeezed && !petting_ && dur > 30) { petting_ = 1; g = G_PET_START; }
    if (lvl < TH_TOUCH * 4 / 5) {
      touching = 0;
      if (petting_) { petting_ = 0; g = G_PET_END; }
      else if (!pressed && !squeezed && dur < 23) {
        if (ntaps && now - taps[0] > 70) ntaps = 0;
        taps[ntaps++] = now;
        if (ntaps >= 3) { ntaps = 0; g = G_TICKLE; } else g = G_TAP;
      }
    }
    hover_t = 0;
  } else if (lvl >= TH_HOVER) {
    off_t = 0;
    if (++hover_t > 15 && !hovering) { hovering = 1; g = G_HOVER; }
  } else if (++off_t > 50) { hovering = 0; hover_t = 0; }
  *petting = petting_;
  return g;
}

/* The LED with anode L0 = PA1 and cathode L1 = PA2 (wherever the arrangement puts it) as a photodiode:
 * reverse-charge, float, time the discharge */
uint8_t ambient_dark(void) {
  TIM2->DMAINTENR = 0;
  GPIOA->CFGLR = 0x44444224u;                       /* PA1, PA2 outputs */
  GPIOA->OUTDR = 1u << 2;                           /* cathode high, anode low = reverse bias */
  for (volatile uint8_t i = 0; i < 20; i++) {}
  GPIOA->CFGLR = 0x44444424u;                       /* cathode floats */
  uint16_t n = 0;
  while ((GPIOA->INDR & (1u << 2)) && n < 4000) n++;
  TIM2->DMAINTENR = 1;
  return n > 2500;                                  /* slow discharge = little photocurrent = dark */
}
