/* Pine's sense of touch on the PY32F003: the star on PA7, read by ADC charge sharing between LED scan phases. */
#include "pine.h"
#include "regs.h"
#include "hw.h"


uint16_t adc_read(uint8_t ch, uint8_t smp) {
  ADC1->SMPR = smp;
  ADC1->CHSELR = 1u << ch;
  ADC1->CR = 1u;                                    /* ADEN: this ADC clears it after every conversion */
  for (volatile int i = 0; i < 4; i++) {}
  ADC1->CR = 1u | 1u << 2;                          /* ADSTART */
  for (uint32_t n = 0; !(ADC1->ISR & (1u << 2)); n++)   /* EOC; never hang Pine on a sensor */
    if (n > 2000) return 0;
  return (uint16_t)ADC1->DR;                        /* reading DR clears EOC */
}


void sense_init(void) {
  ADC_CCR |= 1u << 22;                              /* VREFEN: the reference precharges the sample capacitor */
  ADC1->CFGR2 = 1u << 28;                           /* ADC clock = PCLK / 2 = 12 MHz */
  ADC1->CR = 1u << 31;                              /* calibrate (ADEN = 0) */
  for (uint32_t n = 0; (ADC1->CR & (1u << 31)) && n < 100000; n++) {}
}

/* Charge sharing: discharge the star, charge the ADC's sample capacitor from Vrefint, then connect the two. A finger
 * adds capacitance to the star, so the shared voltage drops. A touching finger also brings mains hum (the board's
 * USB supply floats relative to you), so one sample is taken at every anode change of the LED scan, ~39 spread over
 * each 20 ms frame, and the frame uses their mean: the hum averages out. Runs inside the scan interrupt with every
 * line floating. */
static volatile uint32_t acc, cnt;
void sense_touch_isr(void) {
  GPIOA->BRR = 1u << 7;
  GPIOA->MODER = (IDLE_A & ~(3u << 14)) | (1u << 14);           /* PA7 output low: star discharged */
  (void)adc_read(12, 3);                                         /* sample cap <- Vrefint (13.5 cycles) */
  GPIOA->MODER = IDLE_A;                                         /* PA7 analog, floating */
  acc += adc_read(7, 0);                                         /* share, 3.5-cycle sample */
  cnt++;
}
uint16_t sense_touch_raw(void) {                                 /* mean of the last frame's samples, x8 */
  static uint16_t last;
  __asm__ volatile("cpsid i");
  uint32_t a = acc, n = cnt;
  acc = cnt = 0;
  __asm__ volatile("cpsie i");
  if (n) last = (uint16_t)(a * 8 / n);
  return last;
}

/* the touch level in the simulator's units, from r = reading / untouched baseline. Measured on the first board
 * (2026-11-03, README.md "Touch calibration"): a resting finger settles near r = 0.44, a firm press near 0.27, a
 * pinch through front and back near 0.20; a finger 1 cm away doesn't register. Piecewise-linear onto the
 * simulator's scale: its thresholds (touch 0.35, press 0.65, squeeze 1.0) sit halfway between those, and the touch
 * threshold sits at r = 0.82 because contact takes a few hundred ms to build up and a quick tap must still count. */
static const struct { uint16_t r; uint16_t lvl; } MAP[] = {   /* r and level in Q12, r falling */
  { 4096, 0 }, { QC(0.82), QC(0.35) }, { QC(0.44), QC(0.5) }, { QC(0.35), QC(0.65) }, { QC(0.27), QC(0.82) },
  { QC(0.235), QC(1.0) }, { QC(0.19), QC(1.32) }, { 0, QC(1.5) },
};
static int32_t base8;                               /* untouched reading, x256 */
static uint16_t hist[3], still_frames;
volatile uint16_t touch_raw_last, touch_min, touch_max;
volatile int32_t touch_base_last;
volatile q12 touch_level_last;
static uint16_t med3(uint16_t a, uint16_t b, uint16_t c) {
  return a > b ? (b > c ? b : a > c ? c : a) : (a > c ? a : b > c ? c : b);
}
double sense_touch_level(uint16_t raw) {
  static uint8_t n;
  if (++n >= 50) { n = 0; touch_min = 65535; touch_max = 0; }
  if (raw < touch_min) touch_min = raw;
  if (raw > touch_max) touch_max = raw;
  if (!base8) { base8 = (int32_t)raw << 8; hist[0] = hist[1] = hist[2] = raw; }
  hist[0] = hist[1]; hist[1] = hist[2]; hist[2] = raw;
  uint16_t m = med3(hist[0], hist[1], hist[2]);
  touch_raw_last = m;
  int32_t r = (int32_t)(((int64_t)m << 20) / base8);             /* reading / baseline, Q12 */
  q12 lvl = 0;
  if (r < 4096) {
    int k = 1;
    while (MAP[k].r > r) k++;
    lvl = MAP[k - 1].lvl + (MAP[k].lvl - MAP[k - 1].lvl) * (MAP[k - 1].r - r) / (MAP[k - 1].r - MAP[k].r);
  }
  int32_t m8 = (int32_t)m << 8;
  if (m8 >= base8) base8 += (m8 - base8) >> 8;                   /* above the baseline: drift, follow in ~5 s */
  else if (lvl < QC(0.04)) base8 += (m8 - base8) >> 9;           /* untouched: follow drift in ~10 s */
  int32_t step = (int32_t)hist[2] - hist[1];
  still_frames = (lvl >= QC(TH_TOUCH) && step < 40 && step > -40) ? still_frames + 1 : 0;
  if (still_frames > 30 * FPS) { base8 = m8; still_frames = 0; lvl = 0; }   /* something resting on the star */
  touch_base_last = base8 >> 8;
  touch_level_last = lvl;
  return fxd(lvl);
}
