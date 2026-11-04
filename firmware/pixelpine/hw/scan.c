/* Charlieplex scan, PY32F003: 16 anode phases x 8 BCM bits, 2 us base slot -> 16 * 255 * 2 us = 8.2 ms = 122 Hz,
 * the simulator's "Raw scan, 8 bit" model. TIM1 interrupts once per bit 2..7; bits 0 and 1 (48 and 96 cycles at
 * 24 MHz, too short for an interrupt each) are shown from the phase-start interrupt with a counted delay. */
#include "pine.h"
#include "regs.h"
#include "hw.h"

#define NOP8 "nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n"   /* gcc sizes inline asm by its lines */
#define SLOT 48                                     /* 2 us at 24 MHz */
static uint16_t masks[16][8];                       /* [anode][bit] = cathode lines pulled low */
static uint16_t cur[8];                             /* the phase on show, latched at its start: no tearing */
static uint8_t ph = 15, bit = 7;
volatile uint32_t scan_frames;                      /* full refreshes, for the debugger */

/* line -> port (A=0, B=1, F=2) << 4 | pin, hardware/scripts/geometry.py LINE_PINS */
static const uint8_t line_pp[16] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x0C, 0x0D, 0x0E, 0x15, 0x16, 0x17,
                                     0x20, 0x21, 0x24 };
/* 8 pin bits -> MODER "01" (output) pairs */
#define S1(x) (((x) & 1) | ((x) & 2) << 1 | ((x) & 4) << 2 | ((x) & 8) << 3 | ((x) & 16) << 4 | ((x) & 32) << 5 | \
               ((x) & 64) << 6 | ((x) & 128) << 7)
#define R4(n) S1(n), S1(n + 1), S1(n + 2), S1(n + 3)
#define R16(n) R4(n), R4(n + 4), R4(n + 8), R4(n + 12)
#define R64(n) R16(n), R16(n + 16), R16(n + 32), R16(n + 48)
static const uint16_t spread8[256] = { R64(0), R64(64), R64(128), R64(192) };

/* lines L0-L6 = PA0-PA6, L7-L9 = PA12-PA14, L10-L12 = PB5-PB7, L13/L14 = PF0/PF1, L15 = PF4 */
#ifdef KEEP_SWD                                     /* bench / calib builds: PA13/PA14 stay SWD, L8/L9 stay dark */
#define LINE_MASK 0xFCFFu
#else
#define LINE_MASK 0xFFFFu
#endif
#define MODA(m) (IDLE_A | spread8[(m) & 0x7Fu] | (uint32_t)spread8[((m) >> 7) & 7u & (uint32_t)(LINE_MASK >> 7)] << 24)
#define MODB(m) (IDLE_B | spread8[(((m) >> 10) & 7u) << 5])
#define MODF(m) (IDLE_F | spread8[(((m) >> 13) & 3u) | (((m) >> 15) & 1u) << 4])

void TIM1_BRK_UP_TRG_COM_IRQHandler(void) {
  TIM1->SR = 0;
  if (++bit < 8) {
    uint32_t m = cur[bit] ? (cur[bit] | 1u << ph) & LINE_MASK : 0;   /* no LED in this slot: anode off too */
    GPIOA->MODER = MODA(m); GPIOB->MODER = MODB(m); GPIOF->MODER = MODF(m);
    TIM1->ARR = (SLOT << bit) - 1;
    if (TIM1->CNT >= TIM1->ARR) TIM1->EGR = 1;     /* ran late: never let the counter wrap (a 2.7 ms flash) */
    return;
  }
  /* next anode: everything floats, move the anode, then bit 0 for one slot and bit 1 */
  bit = 1;
  if (++ph == 16) { ph = 0; scan_frames++; }
  /* latch the whole phase: if the main loop rewrites these masks mid-phase, an LED going 127 -> 128 would
   * otherwise show old bits 0-6 plus new bit 7 = 255 for a refresh (a bright flash) */
  const uint16_t *mk = masks[ph];
  for (int b = 0; b < 8; b++) cur[b] = mk[b];
  uint32_t m0 = cur[0] ? (cur[0] | 1u << ph) & LINE_MASK : 0, m1 = cur[1] ? (cur[1] | 1u << ph) & LINE_MASK : 0;
  uint32_t a0 = MODA(m0), b0 = MODB(m0), f0 = MODF(m0), a1 = MODA(m1), b1 = MODB(m1), f1 = MODF(m1);
  /* An anode driven high charges every floating line through its own LEDs, a faint flash on each of them; so
   * slots with no LED lit don't drive the anode at all (below), and the lines are left at the voltage they
   * settled at rather than drained, which would only make the next anode recharge them through its LEDs. */
  GPIOA->MODER = IDLE_A; GPIOB->MODER = IDLE_B; GPIOF->MODER = IDLE_F;
  sense_touch_isr();                                /* the touch star, while every line floats (~5 us) */
  uint8_t an = line_pp[ph];
  GPIOA->ODR = (an >> 4) == 0 ? 1u << (an & 15) : 0;
  GPIOB->ODR = (an >> 4) == 1 ? 1u << (an & 15) : 0;
  GPIOF->ODR = (an >> 4) == 2 ? 1u << (an & 15) : 0;
  GPIOA->MODER = a0; GPIOB->MODER = b0; GPIOF->MODER = f0;
  __asm__ volatile(NOP8 NOP8 NOP8 NOP8 "nop\n nop\n nop\n nop\n nop\n nop\n");   /* bit 0: ~48 cycles with the stores */
  GPIOA->MODER = a1; GPIOB->MODER = b1; GPIOF->MODER = f1;
  TIM1->ARR = TIM1->CNT + 2 * SLOT - 20;             /* bit 1: 96 cycles, minus the next interrupt's latency */
}

void scan_init(void) {
  GPIOA->OSPEEDR = 0; GPIOB->OSPEEDR = 0; GPIOF->OSPEEDR = 0;   /* slowest edges (EMC) */
  /* No pull resistors on any line. After reset PA13 (SWDIO) has a pull-up and PA14 (SWCLK) a pull-down; as lines
   * L8 / L9 they kept a constant ~30 uA flowing through the L8 -> L9 LED and every L8 -> X -> L9 pair: ~30 LEDs
   * glowing faintly all the time, even with every LED at 0. */
#ifdef KEEP_SWD
  GPIOA->PUPDR &= ~0x00003FFFu;                     /* debug builds keep SWD's own pulls (L8/L9 stay dark anyway) */
#else
  GPIOA->PUPDR &= ~0x3F003FFFu;                     /* PA0-PA6, PA12-PA14 */
#endif
  GPIOB->PUPDR &= ~0x0000FC00u;                     /* PB5-PB7 */
  GPIOF->PUPDR &= ~0x0000030Fu;                     /* PF0, PF1, PF4 */
  GPIOA->MODER = IDLE_A; GPIOB->MODER = IDLE_B; GPIOF->MODER = IDLE_F;   /* SWD released: PA13/PA14 = L8/L9 */
  TIM1->PSC = 0;
  TIM1->ARR = SLOT - 1;
  TIM1->DIER = 1;                                   /* update interrupt */
  TIM1->CR1 = 1;
  NVIC_ISER = 1u << TIM1_IRQn;
}

/* 8-bit levels -> cathode masks, one anode at a time (built aside, copied in with the interrupt held off).
 * level(i) gives LED i's level; each LED is asked once.
 *
 * Current-sharing compensation: the simulator's "Compensated" hardware model (index.html, hardware()). The LEDs lit
 * together in one BCM slot share the anode pin: k of them get 2 / (k + 1) of a lone LED's current each, so a LED
 * that happens to be alone in a slot is up to 7.5x brighter than one of 15 (on the board: LEDs flaring up as the
 * radar sweeps). Every LED is aimed at the brightness 8 sharers reach (cap = 2/9) and its duty pre-scaled for the
 * slots it actually shares; 4 rounds, as in the simulator.
 * Built only with -DSHARE_COMP: on the board it made the levels jitter frame to frame (a visible flicker, ~3x the
 * frame-to-frame wiggle of the raw scan in a PC comparison), and the simulator's own model does the same. */
#ifdef SHARE_COMP
static const uint16_t SHARE[16] = {          /* 2 / (k + 1), Q12 */
  4096, 4096, 2731, 2048, 1638, 1365, 1170, 1024, 910, 819, 745, 683, 630, 585, 546, 512 };
#endif
__attribute__((optimize("O2"))) void scan_show(uint8_t (*level)(int)) {
  const uint8_t *pl = phase_led, *pc = phase_cath;
  for (int a = 0; a < 16; a++, pl += 15, pc += 15) {
    uint8_t t[15], q[15];
    for (int j = 0; j < 15; j++) q[j] = t[j] = level(pl[j]);
#ifdef SHARE_COMP                                   /* off: on the board it made levels jitter frame to frame */
    for (int it = 0; it < 4; it++) {
      uint8_t k[8] = { 0 };
      for (int j = 0; j < 15; j++) for (int b = 0; b < 8; b++) k[b] += (q[j] >> b) & 1u;
      uint8_t changed = 0;
      for (int j = 0; j < 15; j++) {
        if (!q[j]) continue;                                   /* rounded off: stays off, as in the simulator */
        uint32_t S = 0;                                        /* sum over its slots of weight x share, Q12 */
        for (int b = 0; b < 8; b++) if ((q[j] >> b) & 1u) S += (uint32_t)SHARE[k[b]] << b;
        uint32_t d16 = (((uint32_t)t[j] * q[j] * 910u) << 4) / (S >> 4 ? S >> 4 : 1);   /* new duty, x256 */
        uint32_t nq = (d16 + 128) >> 8;
        if (nq > 255) nq = 255;
        if (nq != q[j]) { q[j] = (uint8_t)nq; changed = 1; }
      }
      if (!changed) break;
    }
#endif
    uint16_t m[8] = { 0 };
    for (int j = 0; j < 15; j++) {
      uint16_t cb = (uint16_t)(1u << pc[j]);
      for (int b = 0; b < 8; b++) if ((q[j] >> b) & 1u) m[b] |= cb;
    }
#ifdef KEEP_SWD
    /* Debug builds keep SWDIO/SWCLK (lines L8/L9) on the probe, which holds SWDIO high: any LED from L8 to a line
     * the scan pulls low would light at full current, almost all the time (it happened twice on the board). So
     * these builds never light an LED: the masks stay empty and no line is ever driven. */
    for (int b = 0; b < 8; b++) m[b] = 0;
#endif
    TIM1->DIER = 0;
    for (int b = 0; b < 8; b++) masks[a][b] = m[b];
    TIM1->DIER = 1;
  }
}

/* every line floating, scan stopped (flash writes, sensing) */
void scan_pause(void) { TIM1->DIER = 0; GPIOA->MODER = IDLE_A; GPIOB->MODER = IDLE_B; GPIOF->MODER = IDLE_F; }
void scan_resume(void) { TIM1->DIER = 1; }
