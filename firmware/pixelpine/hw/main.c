/* PY32F003 startup, clocks and the 50 fps main loop. */
#include "pine.h"
#include "regs.h"
#include "hw.h"

extern uint32_t _data_start, _data_end, _data_load, _bss_start, _bss_end, _stack_top;
int main(void);
void Reset_Handler(void);
void TIM1_BRK_UP_TRG_COM_IRQHandler(void);
void SysTick_Handler(void);

void Reset_Handler(void) {
  uint32_t *s = &_data_load, *d = &_data_start;
  while (d < &_data_end) *d++ = *s++;                /* also copies the RAM-resident flash routine */
  for (d = &_bss_start; d < &_bss_end; d++) *d = 0;
  uint32_t *sp;
  __asm__ volatile("mov %0, sp" : "=r"(sp));
  for (d = &_bss_end; d < sp - 16; d++) *d = 0x57AC57ACu;   /* stack paint: the debugger reads the high-water mark */
  main();
  for (;;) {}
}

/* a crash: every line floating first, so a frozen scan can't hold an LED on; the watchdog then resets the chip */
static void default_handler(void) {
  GPIOA->MODER = IDLE_A; GPIOB->MODER = IDLE_B; GPIOF->MODER = IDLE_F;
  for (;;) {}
}
typedef void (*vec_t)(void);
/* Cortex-M0+: initial stack pointer, 15 system exceptions, then the 32 peripheral interrupts */
__attribute__((section(".isr_vector"), used)) static const vec_t vectors[16 + 32] = {
  [0] = (vec_t)&_stack_top, [1] = Reset_Handler, [2] = default_handler, [3] = default_handler,
  [15] = SysTick_Handler, [16 + TIM1_IRQn] = TIM1_BRK_UP_TRG_COM_IRQHandler,
};

/* firmware version, at a fixed address right after the vector table (technical file item 14: readable over SWD) */
__attribute__((section(".fw_version"), used)) const char fw_version[32] = "PixelPine PP-240-WW fw 0.1.0";

static volatile uint8_t tick, pending;
volatile uint32_t frame_no, frame_cycles, frame_cycles_max, frames_late;   /* read with the debugger */
void SysTick_Handler(void) { tick = 1; if (pending < 10) pending++; }

/* The F003 has no PLL: without a crystal the fastest clock is the internal oscillator at 24 MHz.
 * 0 flash wait states are fine up to 24 MHz, so ACR stays at its reset value. */
static void clock_init(void) {
  RCC->ICSCR = (RCC->ICSCR & ~0xFFFFu) | (4u << 13) | (HSI_TRIM_24MHZ & 0x1FFFu);   /* HSI 8 -> 24 MHz */
  while (!(RCC->CR & (1u << 10))) {}                 /* HSIRDY */
  RCC->IOPENR |= (1u << 0) | (1u << 1) | (1u << 5);  /* GPIOA, GPIOB, GPIOF */
  RCC->APBENR2 |= (1u << 11) | (1u << 20);           /* TIM1, ADC */
}


#ifdef BENCH
/* CPU-time bench (built with KEEP_SWD so the debugger can read the results while it runs): every show is
 * faded in with the jelly transition (two live scenes, the dearest transition) under a face overlay, frames run
 * back to back. bench_max[k] = worst frame in cycles (24 MHz; a 50 fps frame has 480 000). */
volatile uint32_t bench_max[NSHOWS], bench_done, bench_show[NSHOWS], bench_part[4], bench_prim[8];
#define CYC(c0) ((c0 - SYSTICK->CVR) & 0xFFFFFFu)
int main(void) {
  clock_init();
  scan_init();
  SYSTICK->RVR = 0xFFFFFFu; SYSTICK->CVR = 0; SYSTICK->CSR = 5;   /* free-running, no interrupt */
  for (int k = 0; k < NSHOWS; k++) {                  /* each show's frame() alone */
    engine_reset();
    scene_t *bsc = scene_new_show((uint8_t)k, 1, 1);
    for (int n = 0; n < 100; n++) { uint32_t c0 = SYSTICK->CVR; show_frame(&bsc->sh, bsc->out); uint32_t c = CYC(c0); if (c > bench_show[k]) bench_show[k] = c; }
  }
  { uint32_t c0 = SYSTICK->CVR; for (int i = 0; i < NLED; i++) (void)gamma8((q12)(i * 17)); bench_part[0] = CYC(c0); }
  { volatile q12 acc = 0; uint32_t c0;            /* primitives, cycles per call x 1000 / 1000 */
    c0 = SYSTICK->CVR; for (int i = 0; i < 1000; i++) acc += noise2(i * 47906, 123456); bench_prim[0] = CYC(c0);
    c0 = SYSTICK->CVR; for (int i = 0; i < 1000; i++) acc += qsin((uint32_t)i * 2654435761u); bench_prim[1] = CYC(c0);
    c0 = SYSTICK->CVR; for (int i = 0; i < 100; i++) acc += (q12)jsin(i * 0.37 + 1); bench_prim[2] = CYC(c0) * 10;
    noise_row_t nr; noise_row(&nr, 123456); c0 = SYSTICK->CVR; for (int i = 0; i < 1000; i++) acc += noise2r(&nr, i * 47906); bench_prim[3] = CYC(c0);
    c0 = SYSTICK->CVR; for (int i = 0; i < 1000; i++) acc += qgauss(i * 13); bench_prim[4] = CYC(c0);
    volatile double d = 1.234; c0 = SYSTICK->CVR; for (int i = 0; i < 1000; i++) d = d * 1.0001 + 0.5; bench_prim[5] = CYC(c0);
    scene_t *bs = scene_new_show(0, 1, 1);
    c0 = SYSTICK->CVR; for (int i = 0; i < 100; i++) splat(bs->out, 0, 30 * 4096, ONE, QC(2.65)); bench_prim[6] = CYC(c0) * 10;
    c0 = SYSTICK->CVR; for (int i = 0; i < 1000; i++) acc += qm(acc, 3000); bench_prim[7] = CYC(c0); }
  { scene_t *bsc = scene_now(); uint32_t c0 = SYSTICK->CVR; for (int i = 0; i < NLED; i++) (void)show_px(&bsc->sh, bsc->out, i); bench_part[1] = CYC(c0); }
  engine_reset();
  brain_init(0.3);
  go_show(scene_new_show(NSHOWS - 1, 1, 1), T_FADE, -1);
  for (int pass = 0;; pass++) {
    for (int k = 0; k < NSHOWS; k++) {
      go_show(scene_new_show((uint8_t)k, 1, 1), T_JELLY, 3.0);
      scene_overlay(F_TICKLED, 0);
      for (int n = 0; n < 150; n++) {
        uint32_t c0 = SYSTICK->CVR;
        engine_tick(FW_DT);
        uint32_t c1 = CYC(c0);
        scan_show(pine_level);
        engine_end_frame();
        uint32_t c = CYC(c0);
        if (c1 > bench_part[2]) bench_part[2] = c1;
        if (c - c1 > bench_part[3]) bench_part[3] = c - c1;
        if (c > bench_max[k]) bench_max[k] = c;
      }
    }
    bench_done = (uint32_t)pass + 1;
  }
}
#elif defined(DIAG)
/* ghosting diagnosis: 6 s each, spoken order: all off / top LED full / top LED dim / whole tree evenly at 64 */
static volatile uint8_t pat;
static uint8_t diag_level(int i) {
  switch (pat) {
  case 1: return i == 1 ? 255 : 0;
  case 2: return i == 1 ? 16 : 0;
  case 3: return 64;
  default: return 0;
  }
}
int main(void) {
  clock_init();
  scan_init();
  sense_init();
  SYSTICK->RVR = 24000000u / FPS - 1; SYSTICK->CVR = 0; SYSTICK->CSR = 7;
  uint16_t t = 0;
  for (;;) {
    while (!pending) __asm__ volatile("wfi");
    pending = 0;
    if (++t == 6 * FPS) { t = 0; pat = (uint8_t)((pat + 1) % 4); }
    scan_show(diag_level);
  }
}
#elif defined(TOUCHFIELD)
/* experiment 3: mains hum on the LED lines as a touch field. No LED is ever driven: every line not being measured
 * is held at 0 V (the SWD lines L8/L9 excepted: the probe drives them). Per 800 ms round, for each of the 14 other
 * lines: the share of time the floating line reads digital high, listened for 2, 5 and 20 ms (x255); and for the
 * ADC lines L0-L6 the spread of 16 ADC samples over 20 ms. 125 rounds (100 s), then it stops; read after halting. */
#define NR 125
typedef struct { uint8_t dig[3][14]; uint8_t adc[7]; } rnd_t;
volatile rnd_t rec[NR];
volatile uint32_t nrec;
static const uint8_t LPORT[14] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2 };    /* L0-L7, L10-L15 */
static const uint8_t LPIN[14] = { 0, 1, 2, 3, 4, 5, 6, 12, 5, 6, 7, 0, 1, 4 };
static gpio_t *const PORT[3] = { GPIOA, GPIOB, GPIOF };
static void hold_low(void) {
  GPIOA->ODR = 0; GPIOB->ODR = 0; GPIOF->ODR = 0;
  GPIOA->MODER = (IDLE_A & ~0x0300FFFFu) | 0x01005555u;   /* PA0-PA7, PA12 out low (PA13/14 stay SWD) */
  GPIOB->MODER = (IDLE_B & ~0x0000FC00u) | 0x00005400u;   /* PB5-PB7 out low */
  GPIOF->MODER = (IDLE_F & ~0x0000030Fu) | 0x00000105u;   /* PF0, PF1, PF4 out low */
}
static uint8_t listen(int k, uint32_t ms) {               /* share of samples high, x255; samples every ~40 us */
  hold_low();
  gpio_t *g = PORT[LPORT[k]];
  uint32_t b = LPIN[k], hi = 0, n = ms * 25;
  g->MODER &= ~(3u << (2 * b));                           /* floating input */
  for (uint32_t i = 0; i < n; i++) {
    hi += (g->IDR >> b) & 1u;
    for (volatile int w = 0; w < 60; w++) {}
  }
  hold_low();
  return (uint8_t)(hi * 255 / n);
}
int main(void) {
  clock_init();
  sense_init();
  SYSTICK->RVR = 0xFFFFFFu; SYSTICK->CVR = 0; SYSTICK->CSR = 5;
  static const uint8_t WIN[3] = { 2, 5, 20 };
  while (nrec < NR) {
    uint32_t t0 = SYSTICK->CVR;
    for (int k = 0; k < 14; k++)
      for (int w = 0; w < 3; w++) rec[nrec].dig[w][k] = listen(k, WIN[w]);
    for (int k = 0; k < 7; k++) {
      uint32_t mn = 4095, mx = 0;
      hold_low();
      GPIOA->MODER |= 3u << (2 * k);
      for (int n = 0; n < 16; n++) {
        uint32_t v = adc_read((uint8_t)k, 3);
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        for (volatile int w = 0; w < 1500; w++) {}
      }
      rec[nrec].adc[k] = (uint8_t)((mx - mn) > 1020 ? 255 : (mx - mn) / 4);
    }
    hold_low();
    nrec++;
    for (uint32_t el = 0, prev = t0; el < 24000u * 800u;) {        /* 800 ms per round (SysTick wraps at 0.7 s) */
      uint32_t now = SYSTICK->CVR;
      el += (prev - now) & 0xFFFFFFu; prev = now;
    }
  }
  hold_low();
  for (;;) __asm__ volatile("wfi");
}
#elif defined(SHOWCYCLE)
/* bring-up build: every light show for 8 s in turn, straight from shows.c */
static scene_t demo;
static uint8_t demo_level(int i) { return gamma8(show_px(&demo.sh, demo.out, i)); }
int main(void) {
  clock_init();
  scan_init();
  SYSTICK->RVR = 24000000u / FPS - 1; SYSTICK->CVR = 0; SYSTICK->CSR = 7;
  uint8_t k = 0; uint16_t t = 0;
  show_init(&demo.sh, k, 1);
  for (;;) {
    while (!tick) __asm__ volatile("wfi");
    tick = 0;
    uint32_t c0 = SYSTICK->CVR;
    if (++t == 8 * FPS) { t = 0; k = (uint8_t)((k + 1) % NSHOWS); show_init(&demo.sh, k, 1); }
    show_frame(&demo.sh, demo.out);
    scan_show(demo_level);
    frame_cycles = c0 - SYSTICK->CVR; frame_no++;
    if (frame_cycles > frame_cycles_max) frame_cycles_max = frame_cycles;
  }
}
#else
#ifdef LISTEN
/* experiment 4: touch-field listening while Pine runs. Every few frames the LED scan pauses for a short gap, the
 * 7 ADC lines L0-L6 float (every other line at 0 V) and are sampled round-robin; per line the spread of its samples
 * (mains hum from a finger) goes into lbin[bin][line] (max over each 3 s, /16). 0-45 s: 4 ms every 100 ms,
 * 45-90 s: 8 ms every 250 ms. At 90 s every LED goes off and it idles, so the debugger can halt it safely. */
#define LSECS 31                                       /* 3 s bins */
volatile uint8_t lbin[LSECS][7];                       /* [3-second bin][line] */
volatile uint32_t lframe, ldone;
static void listen_gap(uint32_t ms) {
  scan_pause();
  GPIOA->ODR = 0; GPIOB->ODR = 0; GPIOF->ODR = 0;
  GPIOA->MODER = (IDLE_A & ~0x0300C000u) | 0x01004000u | 0x3FFFu;   /* PA0-PA6 analog, PA7 / PA12 low */
  GPIOB->MODER = (IDLE_B & ~0x0000FC00u) | 0x00005400u;
  GPIOF->MODER = (IDLE_F & ~0x0000030Fu) | 0x00000105u;
  uint16_t mn[7], mx[7];
  for (int k = 0; k < 7; k++) { mn[k] = 4095; mx[k] = 0; }
  uint32_t c0 = SYSTICK->CVR, top = SYSTICK->RVR + 1, need = ms * 24000u;
  for (;;) {
    for (int k = 0; k < 7; k++) {
      uint16_t v = adc_read((uint8_t)k, 3);
      if (v < mn[k]) mn[k] = v;
      if (v > mx[k]) mx[k] = v;
    }
    uint32_t now = SYSTICK->CVR, el = now <= c0 ? c0 - now : c0 + top - now;
    if (el >= need) break;
  }
  GPIOA->MODER = IDLE_A; GPIOB->MODER = IDLE_B; GPIOF->MODER = IDLE_F;
  scan_resume();
  uint32_t sec = lframe / (3 * FPS);
  if (sec < LSECS)
    for (int k = 0; k < 7; k++) {
      uint32_t v = (uint32_t)(mx[k] - mn[k]) / 16;
      if (v > lbin[sec][k]) lbin[sec][k] = (uint8_t)v;
    }
}
static void listen_tick(void) {
  uint32_t f = ++lframe;
  if (f < 45u * FPS) { if (f % 5 == 0) listen_gap(4); }
  else if (f < 90u * FPS) { if (f % 12 == 0) listen_gap(8); }
  else { scan_pause(); ldone = 1; for (;;) { IWDG_KR = 0xAAAAu; __asm__ volatile("wfi"); } }
}
#endif
/* Room light: rev A can't sense it (README.md, "Room light"), so Pine stays in the simulator's default "lights on". */

/* Pine's bond in flash: saved when it moved by >= 0.02, at most every 15 minutes (flash wear) */
static uint16_t bond_saved;
static uint32_t bond_frames;
volatile uint8_t debug_save_now;                     /* set from the debugger to test the flash store */
#ifdef WDT_TEST
volatile uint8_t debug_hang;                         /* set from the debugger: the watchdog must reset Pine */
#endif
static void bond_tick(void) {
  uint16_t b = (uint16_t)(brain.E.bond * 65535.0 + 0.5);
  if (debug_save_now) { debug_save_now = 0; store_save_bond(b); bond_saved = b; bond_frames = 0; return; }
  if (++bond_frames < 15u * 60u * FPS) return;
  if ((b > bond_saved ? b - bond_saved : bond_saved - b) < 1311) return;   /* 0.02 */
  store_save_bond(b);
  bond_saved = b; bond_frames = 0;
}

int main(void) {
  clock_init();
  scan_init();
  sense_init();
  uint32_t seed = 0x9e3779b9u;                       /* each tree its own personality: ADC noise */
  for (int k = 0; k < 16; k++) {
    for (volatile int w = 0; w < 3000; w++) {}
    seed = (seed ^ sense_touch_raw()) * 2654435761u;
  }
  g_rng = seed ? seed : 1;
  bond_saved = store_load_bond();
  SYSTICK->RVR = 24000000u / FPS - 1; SYSTICK->CVR = 0; SYSTICK->CSR = 7;
  pine_init(bond_saved / 65535.0);
#if 1                                             /* independent watchdog, ~1 s on the 32 kHz LSI, in every build:
                                                    * a frozen or halted chip resets, and reset turns every pin off */
  RCC_CSR |= 1u;                                     /* LSI on: the watchdog's clock */
  for (uint32_t n = 0; !(RCC_CSR & 2u) && n < 100000u; n++) {}
  IWDG_KR = 0xCCCCu; IWDG_KR = 0x5555u; IWDG_PR = 4u; IWDG_RLR = 500u;
  for (uint32_t n = 0; (IWDG_SR & 3u) && n < 100000u; n++) {}
  IWDG_KR = 0xAAAAu;
#endif
  /* One 50 fps tick = one simulator frame, always. If a frame ran late (two heavy shows in a transition), the
   * next one updates the logic only and skips building the LED levels, so animation speed never drifts; the LEDs
   * then keep the previous levels for one more frame. */
  for (;;) {
    while (!pending) __asm__ volatile("wfi");
    __asm__ volatile("cpsid i"); pending--; __asm__ volatile("cpsie i");
    uint32_t c0 = SYSTICK->CVR;
    pine_begin(sense_touch_level(sense_touch_raw()));
    if (!pending) scan_show(pine_level);
    else frames_late++;
    pine_end();
    bond_tick();
#ifdef LISTEN
    listen_tick();
#endif
#ifdef WDT_TEST
    while (debug_hang) {}
#endif
#if 1
    IWDG_KR = 0xAAAAu;
#endif
    frame_cycles = c0 - SYSTICK->CVR; frame_no++;
    if (frame_cycles > frame_cycles_max) frame_cycles_max = frame_cycles;
  }
}
#endif
