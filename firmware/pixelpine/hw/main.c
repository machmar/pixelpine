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

/* a crash: every line floating first, so a frozen scan can't hold an LED on; the watchdog then resets the chip.
 * crash_rec survives the reset (.noinit): magic, the faulting PC and LR, a count. */
__attribute__((section(".noinit"))) volatile uint32_t crash_rec[4];
static void default_handler(void) {
  GPIOA->MODER = IDLE_A; GPIOB->MODER = IDLE_B; GPIOF->MODER = IDLE_F;
  for (;;) {}
}
void fault_c(uint32_t *sp) {
  GPIOA->MODER = IDLE_A; GPIOB->MODER = IDLE_B; GPIOF->MODER = IDLE_F;
  if (crash_rec[0] != 0xC4A5C4A5u) { crash_rec[0] = 0xC4A5C4A5u; crash_rec[3] = 0; }
  crash_rec[1] = sp[6]; crash_rec[2] = sp[5]; crash_rec[3]++;
  for (;;) {}
}
__attribute__((naked)) static void hardfault_handler(void) {
  __asm__ volatile("mrs r0, msp\n b fault_c");
}
typedef void (*vec_t)(void);
/* Cortex-M0+: initial stack pointer, 15 system exceptions, then the 32 peripheral interrupts */
__attribute__((section(".isr_vector"), used)) static const vec_t vectors[16 + 32] = {
  [0] = (vec_t)&_stack_top, [1] = Reset_Handler, [2] = default_handler, [3] = hardfault_handler,
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
        scan_show(pine_level10);
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
static uint16_t diag_level(int i) {
  switch (pat) {
  case 1: return i == 1 ? 1023 : 0;
  case 2: return i == 1 ? 64 : 0;
  case 3: return 256;
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
#elif defined(SHOWCYCLE)
/* bring-up build: every light show for 8 s in turn, straight from shows.c */
static scene_t demo;
static uint16_t demo_level(int i) { return gamma10(show_px(&demo.sh, demo.out, i)); }
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

#ifdef TOUCHREC
/* Touch recorder (python build.py touchrec, read with test/touchrec.py): the product firmware, LEDs lit, logging
 * every touch episode (level >= touch .. release). After REC_S seconds the LEDs go dark and PA13/PA14 return to
 * SWD with their pulls: plug the probe in then (never before: in this build they are LED lines L8/L9). */
#define REC_S 120
typedef struct { uint8_t t0, dur, peak, f_press, f_sq, evg[3], evf[3]; } ep_t;   /* 11 B */
#define NEP 8
volatile ep_t rec_ep[NEP];   /* t0 in s; dur, f_* and evf in frames from touch start; peak level x128; evg = gesture + 1 */
volatile uint8_t rec_n, rec_frozen;
volatile uint32_t rec_gpio_cycles;                   /* 64 GPIO MODER stores, in CPU cycles: IOPORT = 64 */
extern volatile uint32_t touch_events;
extern volatile uint8_t touch_event_last;
extern volatile q12 touch_level_last;
static void rec_tick(void) {
  static uint8_t on;
  static uint32_t ev0;
  q12 l = touch_level_last;
  uint8_t l7 = l >= 255 * 32 ? 255 : (uint8_t)(l >> 5);
  volatile ep_t *e = &rec_ep[rec_n < NEP ? rec_n : 0];
  if (!on && touch.touching && rec_n < NEP) {
    on = 1;
    e->t0 = (uint8_t)(frame_no / FPS); e->dur = e->peak = 0; e->f_press = e->f_sq = 255;
    for (int k = 0; k < 3; k++) e->evg[k] = e->evf[k] = 0;
  }
  if (on) {
    if (l7 > e->peak) e->peak = l7;
    if (e->f_press == 255 && l >= QC(TH_PRESS)) e->f_press = e->dur;
    if (e->f_sq == 255 && l >= QC(TH_SQUEEZE)) e->f_sq = e->dur;
    if (touch_events != ev0)
      for (int k = 0; k < 3; k++) if (!e->evg[k]) { e->evg[k] = (uint8_t)(touch_event_last + 1); e->evf[k] = e->dur; break; }
    if (e->dur < 255) e->dur++;
    if (!touch.touching) { on = 0; rec_n++; }
  }
  ev0 = touch_events;
  if (frame_no >= REC_S * FPS) {
    scan_pause();                                    /* every line floating, LEDs dark */
    GPIOA->PUPDR |= 0x24000000u;                     /* SWDIO pull-up, SWCLK pull-down, as after reset */
    GPIOA->MODER = (GPIOA->MODER & ~0x3C000000u) | 0x28000000u;   /* PA13 / PA14 back to SWD (AF0) */
    rec_frozen = 1;
    for (;;) { IWDG_KR = 0xAAAAu; __asm__ volatile("wfi"); }   /* SysTick still wakes it every 20 ms */
  }
}
#endif

int main(void) {
  clock_init();
  scan_init();
  sense_init();
#ifdef TOUCHREC
  SYSTICK->RVR = 0xFFFFFFu; SYSTICK->CVR = 0; SYSTICK->CSR = 5;
  { uint32_t c0 = SYSTICK->CVR;
    __asm__ volatile(".rept 64\n str %1, [%0]\n .endr" ::"l"(&GPIOB->MODER), "l"(IDLE_B) : "memory");
    rec_gpio_cycles = (c0 - SYSTICK->CVR) & 0xFFFFFFu; }
#endif
  uint32_t seed = 0x9e3779b9u;                       /* each tree its own personality: ADC noise */
  for (int k = 0; k < 16; k++) {
    for (volatile int w = 0; w < 3000; w++) {}
    seed = (seed ^ sense_touch_raw()) * 2654435761u;
  }
  g_rng = seed ? seed : 1;
  bond_saved = store_load_bond();
  SYSTICK->RVR = 24000000u / FPS - 1; SYSTICK->CVR = 0; SYSTICK->CSR = 7;
  pine_init(bond_saved / 65535.0);
  /* independent watchdog, ~1 s on the 32 kHz LSI, in every build: a frozen or halted chip resets, and reset turns
   * every pin off */
  RCC_CSR |= 1u;                                     /* LSI on: the watchdog's clock */
  for (uint32_t n = 0; !(RCC_CSR & 2u) && n < 100000u; n++) {}
  IWDG_KR = 0xCCCCu; IWDG_KR = 0x5555u; IWDG_PR = 4u; IWDG_RLR = 500u;
  for (uint32_t n = 0; (IWDG_SR & 3u) && n < 100000u; n++) {}
  IWDG_KR = 0xAAAAu;
  /* One 50 fps tick = one simulator frame, always. If a frame ran late (two heavy shows in a transition), the
   * next one updates the logic only and skips building the LED levels, so animation speed never drifts; the LEDs
   * then keep the previous levels for one more frame. */
  for (;;) {
    while (!pending) __asm__ volatile("wfi");
    __asm__ volatile("cpsid i"); pending--; __asm__ volatile("cpsie i");
    uint32_t c0 = SYSTICK->CVR;
    pine_begin(sense_touch_level(sense_touch_raw()));
    if (!pending) scan_show(pine_level10);
    else frames_late++;
    pine_end();
    bond_tick();
#ifdef TOUCHREC
    rec_tick();
#endif
#ifdef WDT_TEST
    while (debug_hang) {}
#endif
    IWDG_KR = 0xAAAAu;
    frame_cycles = c0 - SYSTICK->CVR; frame_no++;
    if (frame_cycles > frame_cycles_max) frame_cycles_max = frame_cycles;
  }
}
#endif
