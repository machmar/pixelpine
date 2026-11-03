/* Minimal PY32F003 register map (from Puya's py32f003xx.h CMSIS header): just what the firmware touches. */
#pragma once
#include <stdint.h>
#define REG32(a) (*(volatile uint32_t *)(a))

typedef struct { volatile uint32_t CR, ICSCR, CFGR, rsv0, ECSCR, rsv1, CIER, CIFR, CICR, IOPRSTR, AHBRSTR,
                 APBRSTR1, APBRSTR2, IOPENR, AHBENR, APBENR1, APBENR2; } rcc_t;
#define RCC ((rcc_t *)0x40021000u)

typedef struct { volatile uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, LCKR, AFR[2], BRR; } gpio_t;
#define GPIOA ((gpio_t *)0x50000000u)
#define GPIOB ((gpio_t *)0x50000400u)
#define GPIOF ((gpio_t *)0x50001400u)

typedef struct { volatile uint32_t CR1, CR2, SMCR, DIER, SR, EGR, CCMR1, CCMR2, CCER, CNT, PSC, ARR, RCR; } tim_t;
#define TIM1 ((tim_t *)0x40012C00u)

typedef struct { volatile uint32_t ISR, IER, CR, CFGR1, CFGR2, SMPR, rsv1[2], TR, rsv2, CHSELR, rsv3[5], DR, CCSR; } adc_t;
#define ADC1 ((adc_t *)0x40012400u)
#define ADC_CCR REG32(0x40012708u)

typedef struct { volatile uint32_t ACR, rsv1, KEYR, OPTKEYR, SR, CR, rsv2[2], OPTR, SDKR, rsv3, WRPR,
                 rsv4[(0x90 - 0x2C) / 4 - 1], STCR, rsv5[(0x100 - 0x90) / 4 - 1],
                 TS0, TS1, TS2P, TPS3, TS3, PERTPE, SMERTPE, PRGTPE, PRETPE; } flash_t;
#define FLASH ((flash_t *)0x40022000u)

/* factory data in the system area */
#define HSI_TRIM_24MHZ   (*(const volatile uint32_t *)0x1FFF0F10u)   /* HSI trim for the 24 MHz range */
#define FLASH_TIMING_24M ((const volatile uint32_t *)0x1FFF0F6Cu)    /* EPPARA0..4: erase/program timing @ 24 MHz */

typedef struct { volatile uint32_t CSR, RVR, CVR, CALIB; } systick_t;
#define SYSTICK ((systick_t *)0xE000E010u)
#define NVIC_ISER REG32(0xE000E100u)
#define NVIC_IPR(n) REG32(0xE000E400u + 4u * ((n) >> 2))

#define TIM1_IRQn 13                                   /* TIM1 break / update / trigger / commutation */

/* board: MODER with every charlieplex line a floating input (00) and every other pin analog (11) -
 * PA7 = touch star (ADC_IN7), PF2 = NRST, the rest are not bonded out on TSSOP-20 */
#ifdef KEEP_SWD                                        /* bench / calib builds: PA13/PA14 stay SWD (AF) */
#define IDLE_A 0xE8FFC000u
#else
#define IDLE_A 0xC0FFC000u                             /* lines PA0-PA6, PA12-PA14 */
#endif
#define IDLE_B 0xFFFF03FFu                             /* lines PB5-PB7 */
#define IDLE_F 0xFFFFFCF0u                             /* lines PF0, PF1, PF4 */

#define SYSCFG_CFGR1 REG32(0x40010000u)
#define DBGMCU_IDCODE REG32(0x40015800u)
#define SCB_AIRCR REG32(0xE000ED0Cu)
