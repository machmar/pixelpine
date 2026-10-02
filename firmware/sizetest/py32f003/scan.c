/* Charlieplex scan on the PY32F003: 16 anode phases x 8 BCM bit slots, driven by TIM1. NOT tested on hardware.
 * At 24 MHz a 2 us slot is 48 cycles, about one interrupt entry + exit, so bits 0 and 1 share one interrupt
 * (bit 0 is timed with a short counted delay) and only bits 2..7 get their own. Refresh stays at 122 Hz. */
#include "pp.h"
#include "regs.h"

#define SLOT_TICKS 48                               /* 2 us at 24 MHz */
static uint16_t masks[NLINES][8];                   /* cathode lines to pull low per (phase, bit) */
static uint8_t ph, bit = 7;

/* line -> port (A=0, B=1, F=2) << 4 | pin, from hardware/scripts/geometry.py LINE_PINS */
static const uint8_t line_pp[NLINES] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x0C, 0x0D, 0x0E, 0x15, 0x16, 0x17, 0x20, 0x21, 0x24,
};

/* 8 pin bits -> MODER "01" (output) pairs */
#define S1(x) (((x) & 1) | ((x) & 2) << 1 | ((x) & 4) << 2 | ((x) & 8) << 3 | ((x) & 16) << 4 | ((x) & 32) << 5 | \
               ((x) & 64) << 6 | ((x) & 128) << 7)
#define R4(n) S1(n), S1(n + 1), S1(n + 2), S1(n + 3)
#define R16(n) R4(n), R4(n + 4), R4(n + 8), R4(n + 12)
#define R64(n) R16(n), R16(n + 16), R16(n + 32), R16(n + 48)
static const uint16_t spread8[256] = { R64(0), R64(64), R64(128), R64(192) };

/* lines L0-L6 = PA0-PA6, L7-L9 = PA12-PA14, L10-L12 = PB5-PB7, L13/L14 = PF0/PF1, L15 = PF4 */
static inline void drive(uint16_t act) {
  GPIOA->MODER = IDLE_A | spread8[act & 0x7Fu] | (uint32_t)spread8[(act >> 7) & 7u] << 24;
  GPIOB->MODER = IDLE_B | spread8[((act >> 10) & 7u) << 5];
  GPIOF->MODER = IDLE_F | spread8[((act >> 13) & 3u) | ((act >> 15) & 1u) << 4];
}

void TIM1_BRK_UP_TRG_COM_IRQHandler(void) {
  TIM1->SR = 0;
  if (++bit == 8) {                                 /* next anode: float everything, move the anode, show bit 0 */
    bit = 1;
    if (++ph == NLINES) ph = 0;
    GPIOA->MODER = IDLE_A; GPIOB->MODER = IDLE_B; GPIOF->MODER = IDLE_F;
    uint8_t an = line_pp[ph];
    GPIOA->ODR = (an >> 4) == 0 ? 1u << (an & 15) : 0;
    GPIOB->ODR = (an >> 4) == 1 ? 1u << (an & 15) : 0;
    GPIOF->ODR = (an >> 4) == 2 ? 1u << (an & 15) : 0;
    drive(masks[ph][0] | (1u << ph));
    for (volatile uint8_t i = 0; i < 6; i++) {}     /* ~2 us */
  }
  drive(masks[ph][bit] | (1u << ph));
  TIM1->ARR = (SLOT_TICKS << bit) - 1;
}

void scan_init(void) {
  GPIOA->OSPEEDR = 0; GPIOB->OSPEEDR = 0; GPIOF->OSPEEDR = 0;   /* slowest edges (EN IEC 55015) */
  GPIOA->MODER = IDLE_A; GPIOB->MODER = IDLE_B; GPIOF->MODER = IDLE_F;   /* SWD released: PA13/PA14 = L8/L9 */
  TIM1->PSC = 0;
  TIM1->ARR = SLOT_TICKS - 1;
  TIM1->DIER = 1;                                   /* update interrupt */
  TIM1->CR1 = 1;
  NVIC_ISER = 1u << TIM1_IRQn;
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
    TIM1->DIER = 0;
    for (uint8_t b = 0; b < 8; b++) masks[a][b] = m[b];
    TIM1->DIER = 1;
  }
}
