/* PixelPine sizing prototype, PY32F003: startup, clocks, 50 fps main loop. NOT tested on hardware. */
#include "pp.h"
#include "regs.h"

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

static volatile uint8_t tick;
void SysTick_Handler(void) { tick = 1; }

static uint32_t rng = 0x9e3779b9u;
uint32_t rnd32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* The F003 has no PLL: without a crystal the fastest clock is the internal oscillator at 24 MHz.
 * 0 flash wait states are fine up to 24 MHz, so ACR stays at its reset value. */
static void clock_init(void) {
  RCC->ICSCR = (RCC->ICSCR & ~0xFFFFu) | (4u << 13) | (HSI_TRIM_24MHZ & 0x1FFFu);   /* HSI 8 -> 24 MHz */
  while (!(RCC->CR & (1u << 10))) {}                 /* HSIRDY */
  RCC->IOPENR |= (1u << 0) | (1u << 1) | (1u << 5);  /* GPIOA, GPIOB, GPIOF */
  RCC->APBENR2 |= (1u << 11) | (1u << 20);           /* TIM1, ADC */
}

static void adc_init(void) {
  ADC_CCR |= 1u << 22;                               /* VREFEN: the reference precharges the sample cap */
  ADC1->CFGR2 = 1u << 28;                            /* ADC clock = PCLK / 2 = 12 MHz */
  ADC1->CR = 1u << 31; while (ADC1->CR & (1u << 31)) {}   /* calibrate (ADEN = 0) */
  ADC1->SMPR = 7u;                                   /* longest sample time */
  ADC1->CR = 1u;                                     /* ADEN */
}

static uint8_t frame[NLED];

int main(void) {
  clock_init();
  adc_init();
  scan_init();
  rng ^= touch_sample() * 2654435761u;               /* seed personality from ADC noise */
  SYSTICK->RVR = 24000000u / FPS - 1;                /* core clock, 50 Hz */
  SYSTICK->CVR = 0;
  SYSTICK->CSR = 7;                                  /* enable, IRQ, processor clock */
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
