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
  main();
  for (;;) {}
}

static void default_handler(void) { for (;;) {} }
typedef void (*vec_t)(void);
/* Cortex-M0+: initial stack pointer, 15 system exceptions, then the 32 peripheral interrupts */
__attribute__((section(".isr_vector"), used)) static const vec_t vectors[16 + 32] = {
  [0] = (vec_t)&_stack_top, [1] = Reset_Handler, [2] = default_handler, [3] = default_handler,
  [15] = SysTick_Handler, [16 + TIM1_IRQn] = TIM1_BRK_UP_TRG_COM_IRQHandler,
};

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
int main(void) {
  clock_init();
  scan_init();
  SYSTICK->RVR = 24000000u / FPS - 1; SYSTICK->CVR = 0; SYSTICK->CSR = 7;
  pine_init(0.3);
  /* One 50 fps tick = one simulator frame, always. If a frame ran late (two heavy shows in a transition), the
   * next one updates the logic only and skips building the LED levels, so animation speed never drifts; the LEDs
   * then keep the previous levels for one more frame. */
  for (;;) {
    while (!pending) __asm__ volatile("wfi");
    __asm__ volatile("cpsid i"); pending--; __asm__ volatile("cpsie i");
    uint32_t c0 = SYSTICK->CVR;
    pine_begin(0);
    if (!pending) scan_show(pine_level);
    else frames_late++;
    pine_end();
    frame_cycles = c0 - SYSTICK->CVR; frame_no++;
    if (frame_cycles > frame_cycles_max) frame_cycles_max = frame_cycles;
  }
}
#endif
