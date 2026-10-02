/* Touch star (ADC charge sharing on PA7) + gesture classifier + LED ambient-light sensing, PY32F003.
 * Thresholds mirror the simulator's TH table (x256). NOT tested on hardware. */
#include "pp.h"
#include "regs.h"

#define TH_HOVER 20
#define TH_TOUCH 90
#define TH_PRESS 166
#define TH_SQUEEZE 256

static uint16_t adc_convert(uint8_t ch) {
  ADC1->CHSELR = 1u << ch;
  ADC1->CR |= 1u << 2;                              /* ADSTART */
  while (!(ADC1->ISR & (1u << 2))) {}               /* EOC */
  return (uint16_t)ADC1->DR;                        /* reading DR clears EOC */
}

/* discharge the electrode, precharge the ADC sample cap from Vrefint (ch 12), then let them share charge */
uint16_t touch_sample(void) {
  TIM1->DIER = 0;                                   /* the scan ISR also writes GPIOA->MODER */
  GPIOA->BRR = 1u << 7;
  GPIOA->MODER = (GPIOA->MODER & ~(3u << 14)) | (1u << 14);   /* PA7 output low */
  (void)adc_convert(12);
  GPIOA->MODER |= 3u << 14;                         /* PA7 analog */
  uint16_t v = adc_convert(7);
  TIM1->DIER = 1;
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

/* The LED with anode L0 = PA0 and cathode L1 = PA1 (wherever the arrangement puts it) as a photodiode:
 * reverse-charge, float, time the discharge */
uint8_t ambient_dark(void) {
  TIM1->DIER = 0;
  GPIOB->MODER = IDLE_B; GPIOF->MODER = IDLE_F;
  GPIOA->MODER = IDLE_A | 0x5u;                     /* PA0, PA1 outputs, every other line floats */
  GPIOA->ODR = 1u << 1;                             /* cathode high, anode low = reverse bias */
  for (volatile uint8_t i = 0; i < 20; i++) {}
  GPIOA->MODER = IDLE_A | 0x1u;                     /* cathode floats */
  uint16_t n = 0;
  while ((GPIOA->IDR & (1u << 1)) && n < 4000) n++;
  TIM1->DIER = 1;
  return n > 2500;                                  /* slow discharge = little photocurrent = dark */
}
