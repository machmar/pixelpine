/* PixelPine sizing prototype: startup, clocks, 50 fps main loop. NOT tested on hardware. */
#include "pp.h"
#include "regs.h"

extern uint32_t _data_start, _data_end, _data_load, _bss_start, _bss_end;
int main(void);
void c_startup(void);
void TIM2_IRQHandler(void);
void SysTick_Handler(void);

__asm__(".section .init,\"ax\"\n"
        ".global reset_handler\n"
        "reset_handler:\n"
        "  la sp, _stack_top\n"
        "  j c_startup\n");

__attribute__((used)) void c_startup(void) {
  uint32_t *s = &_data_load, *d = &_data_start;
  while (d < &_data_end) *d++ = *s++;
  for (d = &_bss_start; d < &_bss_end; d++) *d = 0;
  main();
  for (;;) {}
}

static void default_handler(void) { for (;;) {} }
typedef void (*vec_t)(void);
__attribute__((section(".vector"), used)) static const vec_t vectors[39] = {
  [0] = default_handler, [SYSTICK_IRQn] = SysTick_Handler, [TIM2_IRQn] = TIM2_IRQHandler,
};

static volatile uint8_t tick;
__attribute__((interrupt)) void SysTick_Handler(void) { SYSTICK->SR = 0; tick = 1; }

static uint32_t rng = 0x9e3779b9u;
uint32_t rnd32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void clock_init(void) {
  FLASH->ACTLR = 1;                                  /* 1 wait state for 48 MHz */
  RCC_CFGR0 &= ~(1u << 16);                          /* PLL source = HSI (24 MHz) */
  RCC_CTLR |= 1u << 24;                              /* PLL on (x2 -> 48 MHz) */
  while (!(RCC_CTLR & (1u << 25))) {}
  RCC_CFGR0 = (RCC_CFGR0 & ~3u) | 2u;                /* SYSCLK = PLL */
  while ((RCC_CFGR0 & 0xC) != 0x8) {}
  RCC_APB2PCENR |= (1u << 2) | (1u << 4) | (1u << 5) | (1u << 9) | 1u;   /* GPIOA/C/D, ADC1, AFIO */
  RCC_APB1PCENR |= 1u;                               /* TIM2 */
}

static void adc_init(void) {
  ADC1->CTLR2 = 1u | (7u << 17) | (1u << 20);        /* ADON, software trigger */
  ADC1->SAMPTR2 = 7u << 21;                          /* long sample time on channel 7 (PD4) */
  ADC1->CTLR2 |= 1u << 3; while (ADC1->CTLR2 & (1u << 3)) {}   /* calibrate */
}

static uint8_t frame[NLED];

int main(void) {
  clock_init();
  adc_init();
  scan_init();
  rng ^= touch_sample() * 2654435761u;               /* seed personality from ADC noise */
  SYSTICK->CMP = 48000000u / 8 / FPS - 1;            /* HCLK/8, 50 Hz */
  SYSTICK->CTLR = 0x0B;                              /* enable, IRQ, auto-reload */
  PFIC_IENR1 = 1u << SYSTICK_IRQn;
  brain_init();
  uint8_t sec = 0, dark = 0;
  for (;;) {
    while (!tick) __asm__ volatile("wfi");
    tick = 0;
    uint8_t petting;
    uint8_t g = touch_tick(&petting);
    if (++sec == FPS) { sec = 0; dark = ambient_dark(); }
    brain_tick(g, petting, dark);
    scene_step(frame);
    build_masks(frame);
  }
}
