/* Charlieplex scan, PY32F003: 16 anode phases x 10-bit BCM, 1 us base slot -> 16 * 512 * 1 us = 8.2 ms = 122 Hz.
 * Level bits 1..9 get slots of 1, 2, 4 .. 256 us. Level bit 0 gets a 1 us slot of its own on every other refresh
 * only (61 Hz), so on average it weighs 0.5 us: the 10th bit costs no refresh rate.
 * Bits 0-3 (24, 24, 48 and 96 cycles at 24 MHz, too short for an interrupt each) are shown from the phase-start
 * interrupt: bits 0-2 with counted delays, bit 3 by setting the timer; TIM1 interrupts once per bit 4..9.
 * The simulator's model is 8 bits. 10 bits make the first glow 4x dimmer (1/1023 instead of 1/255 duty), so pixels
 * fading in and out at the dim end glide instead of popping on and off (seen on the board, 2026-11-04). */
#include "pine.h"
#include "regs.h"
#include "hw.h"

#define SLOT 24                                     /* 1 us at 24 MHz: level bit 1 */
#define NBIT 10
static uint16_t masks[16][NBIT];                    /* [anode][bit] = cathode lines pulled low */
static uint16_t cur[NBIT];                          /* the phase on show, latched at its start: no tearing */
static uint32_t seq[12];                            /* MODER A/B/F for bits 0-3 of the phase starting */
static uint8_t ph = 15, bit = NBIT - 1;
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

/* Bits 0-3 switched on back to back: ldm (4 cycles) + 3 single-cycle IOPORT stores + nops between the stores of one
 * port = 24, 24 and 48 cycles. Every port sees the same sequence, so each gets the exact slot lengths. */
#define NOP1 "nop\n"
#define NOP4 NOP1 NOP1 NOP1 NOP1
#define NOP16 NOP4 NOP4 NOP4 NOP4
#define ST3 "str r1, [%[a]]\n str r2, [%[b]]\n str r3, [%[f]]\n"
#define LD3 "ldm %[s]!, {r1, r2, r3}\n"
static inline __attribute__((always_inline)) void burst(const uint32_t *s) {
  __asm__ volatile(
    LD3 ST3                                         /* bit 0 on (odd refreshes; even ones: all off) */
    LD3 NOP16 NOP1 ST3                              /* 4 + 17 + 3 = 24 cycles later: bit 1 */
    LD3 NOP16 NOP1 ST3                              /* 24 cycles later: bit 2 */
    LD3 NOP16 NOP16 NOP4 NOP4 NOP1 ST3              /* 4 + 41 + 3 = 48 cycles later: bit 3 */
    : [s] "+l"(s)
    : [a] "l"(&GPIOA->MODER), [b] "l"(&GPIOB->MODER), [f] "l"(&GPIOF->MODER)
    : "r1", "r2", "r3", "memory");
}

void TIM1_BRK_UP_TRG_COM_IRQHandler(void) {
  TIM1->SR = 0;
  if (++bit < NBIT) {
    uint32_t m = cur[bit] ? (cur[bit] | 1u << ph) & LINE_MASK : 0;   /* no LED in this slot: anode off too */
    GPIOA->MODER = MODA(m); GPIOB->MODER = MODB(m); GPIOF->MODER = MODF(m);
    TIM1->ARR = (SLOT << (bit - 1)) - 1;
    if (TIM1->CNT >= TIM1->ARR) TIM1->EGR = 1;     /* ran late: never let the counter wrap (a 2.7 ms flash) */
    return;
  }
  /* next anode: everything floats, move the anode, then bits 0-3 */
  bit = 3;
  if (++ph == 16) { ph = 0; scan_frames++; }
  /* latch the whole phase: if the main loop rewrites these masks mid-phase, an LED going 511 -> 512 would
   * otherwise show old bits 0-8 plus new bit 9 = 1023 for a refresh (a bright flash) */
  const uint16_t *mk = masks[ph];
  for (int b = 0; b < NBIT; b++) cur[b] = mk[b];
  if (!(scan_frames & 1u)) cur[0] = 0;             /* bit 0: every other refresh */
  for (int b = 0; b < 4; b++) {
    uint32_t m = cur[b] ? (cur[b] | 1u << ph) & LINE_MASK : 0;
    seq[3 * b] = MODA(m); seq[3 * b + 1] = MODB(m); seq[3 * b + 2] = MODF(m);
  }
  /* An anode driven high charges every floating line through its own LEDs, a faint flash on each of them; so
   * slots with no LED lit don't drive the anode at all (above), and the lines are left at the voltage they
   * settled at rather than drained, which would only make the next anode recharge them through its LEDs. */
  GPIOA->MODER = IDLE_A; GPIOB->MODER = IDLE_B; GPIOF->MODER = IDLE_F;
  sense_touch_isr();                                /* the touch star, while every line floats (~5 us) */
  uint8_t an = line_pp[ph];
  GPIOA->ODR = (an >> 4) == 0 ? 1u << (an & 15) : 0;
  GPIOB->ODR = (an >> 4) == 1 ? 1u << (an & 15) : 0;
  GPIOF->ODR = (an >> 4) == 2 ? 1u << (an & 15) : 0;
  burst(seq);
  TIM1->ARR = TIM1->CNT + 4 * SLOT - 20;             /* bit 3: 96 cycles, minus the next interrupt's latency */
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

/* 10-bit levels -> cathode masks, one anode at a time (built aside, copied in with the interrupt held off).
 * level(i) gives LED i's level 0..1023; each LED is asked once.
 * (Current-sharing compensation, the simulator's "Compensated" model, was tried here and dropped: it made the levels
 * jitter frame to frame, a visible flicker. It is in git history before 2026-11-04.) */
__attribute__((optimize("O2"))) void scan_show(uint16_t (*level)(int)) {
  const uint8_t *pl = phase_led, *pc = phase_cath;
  for (int a = 0; a < 16; a++, pl += 15, pc += 15) {
    uint16_t m[NBIT] = { 0 };
    for (int j = 0; j < 15; j++) {
      uint32_t q = level(pl[j]);
      uint16_t cb = (uint16_t)(1u << pc[j]);
      for (int b = 0; b < NBIT; b++) if ((q >> b) & 1u) m[b] |= cb;
    }
#ifdef KEEP_SWD
    /* Debug builds keep SWDIO/SWCLK (lines L8/L9) on the probe, which holds SWDIO high: any LED from L8 to a line
     * the scan pulls low would light at full current, almost all the time (it happened twice on the board). So
     * these builds never light an LED: the masks stay empty and no line is ever driven. */
    for (int b = 0; b < NBIT; b++) m[b] = 0;
#endif
    TIM1->DIER = 0;
    for (int b = 0; b < NBIT; b++) masks[a][b] = m[b];
    TIM1->DIER = 1;
  }
}

/* every line floating, scan stopped (flash writes, sensing) */
void scan_pause(void) { TIM1->DIER = 0; GPIOA->MODER = IDLE_A; GPIOB->MODER = IDLE_B; GPIOF->MODER = IDLE_F; }
void scan_resume(void) { TIM1->DIER = 1; }
