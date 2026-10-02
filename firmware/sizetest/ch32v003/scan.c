/* Charlieplex scan: 16 anode phases x 8 BCM bit slots, driven by TIM2. NOT tested on hardware. */
#include "pp.h"
#include "regs.h"

#define SLOT_TICKS 96                               /* 2 us at 48 MHz */
static uint16_t masks[NLINES][8];                   /* cathode lines to pull low per (phase, bit) */
static uint8_t ph, bit;

/* line -> port (A=0, C=1, D=2) << 4 | pin */
static const uint8_t line_pp[NLINES] = {
  0x01, 0x02, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x20, 0x22, 0x23, 0x27, 0x25, 0x26,
};
/* 4 pins -> 16-bit CFGLR slice: active pin = push-pull output 2 MHz (0x2), idle = floating input (0x4) */
static const uint16_t spread[16] = {
  0x4444, 0x4442, 0x4424, 0x4422, 0x4244, 0x4242, 0x4224, 0x4222,
  0x2444, 0x2442, 0x2424, 0x2422, 0x2244, 0x2242, 0x2224, 0x2222,
};
static inline uint32_t cfg8(uint8_t pins) { return spread[pins & 15] | ((uint32_t)spread[pins >> 4] << 16); }

__attribute__((interrupt)) void TIM2_IRQHandler(void) {
  TIM2->INTFR = 0;
  if (++bit == 8) { bit = 0; if (++ph == NLINES) ph = 0; }
  uint16_t act = masks[ph][bit] | (1u << ph);       /* lines that drive: cathodes low + the anode high */
  uint8_t pa = 0, pc = 0, pd = 0;
  pa = (uint8_t)((act & 3u) << 1);
  pc = (uint8_t)(act >> 2);
  pd = (uint8_t)(((act >> 10) & 1u) | (((act >> 11) & 1u) << 2) | (((act >> 12) & 1u) << 3) |
                 (((act >> 14) & 1u) << 5) | (((act >> 15) & 1u) << 6) | (((act >> 13) & 1u) << 7));
  /* all floating first (no ghosting), then set the anode level, then enable the drivers */
  GPIOA->CFGLR = 0x44444444u; GPIOC->CFGLR = 0x44444444u; GPIOD->CFGLR = 0x44404444u;
  uint8_t an = line_pp[ph];
  GPIOA->OUTDR = (an >> 4) == 0 ? 1u << (an & 15) : 0;
  GPIOC->OUTDR = (an >> 4) == 1 ? 1u << (an & 15) : 0;
  GPIOD->OUTDR = (an >> 4) == 2 ? 1u << (an & 15) : 0;
  GPIOA->CFGLR = cfg8(pa);
  GPIOC->CFGLR = cfg8(pc);
  GPIOD->CFGLR = (cfg8(pd) & ~0x000F00F0u) | 0x00000040u;   /* PD1 = SWIO untouched, PD4 = analog touch */
  TIM2->ATRLR = (SLOT_TICKS << bit) - 1;
}

void scan_init(void) {
  TIM2->PSC = 0;
  TIM2->ATRLR = SLOT_TICKS - 1;
  TIM2->DMAINTENR = 1;                              /* update interrupt */
  TIM2->CTLR1 = 1;
  PFIC_IENR2 = 1u << (TIM2_IRQn - 32);
}

/* frame (perceived 0..255) -> gamma -> BCM cathode masks. phase_led / phase_cath (tables.c, generated from the LED
 * arrangement in hardware/led_assignment.json) list the 15 LEDs of each anode phase and their cathode lines, so each
 * phase is built in a 16-byte local and swapped in with the IRQ briefly off. */
void build_masks(const uint8_t *fr) {
  const uint8_t *pl = phase_led, *pc = phase_cath;
  for (uint8_t a = 0; a < NLINES; a++) {
    uint16_t m[8] = {0};
    for (uint8_t j = 0; j < 15; j++, pl++, pc++) {
      uint8_t lvl = GAMMA(fr[*pl]);
      if (!lvl) continue;
      uint16_t cb = (uint16_t)(1u << *pc);
      for (uint8_t b = 0; b < 8; b++) if (lvl & (1u << b)) m[b] |= cb;
    }
    TIM2->DMAINTENR = 0;
    for (uint8_t b = 0; b < 8; b++) masks[a][b] = m[b];
    TIM2->DMAINTENR = 1;
  }
}
