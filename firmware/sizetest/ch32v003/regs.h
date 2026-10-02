/* Minimal CH32V003 register map (from the CH32V003 reference manual), just what the prototype touches. */
#pragma once
#include <stdint.h>
#define REG32(a) (*(volatile uint32_t *)(a))

#define RCC_BASE    0x40021000u
#define RCC_CTLR    REG32(RCC_BASE + 0x00)
#define RCC_CFGR0   REG32(RCC_BASE + 0x04)
#define RCC_APB2PCENR REG32(RCC_BASE + 0x18)
#define RCC_APB1PCENR REG32(RCC_BASE + 0x1C)

typedef struct { volatile uint32_t CFGLR, rsv, INDR, OUTDR, BSHR, BCR, LCKR; } gpio_t;
#define GPIOA ((gpio_t *)0x40010800u)
#define GPIOC ((gpio_t *)0x40011000u)
#define GPIOD ((gpio_t *)0x40011400u)

typedef struct { volatile uint32_t CTLR1, CTLR2, SMCFGR, DMAINTENR, INTFR, SWEVGR, CHCTLR1, CHCTLR2, CCER, CNT, PSC, ATRLR; } tim_t;
#define TIM2 ((tim_t *)0x40000000u)

typedef struct { volatile uint32_t STATR, CTLR1, CTLR2, SAMPTR1, SAMPTR2, IOFR[4], WDHTR, WDLTR, RSQR1, RSQR2, RSQR3, ISQR, IDATAR[4], RDATAR; } adc_t;
#define ADC1 ((adc_t *)0x40012400u)

typedef struct { volatile uint32_t ACTLR, KEYR, OBKEYR, STATR, CTLR, ADDR, rsv, OBR, WPR, MODEKEYR; } flash_t;
#define FLASH ((flash_t *)0x40022000u)

typedef struct { volatile uint32_t CTLR, SR, CNT, rsv, CMP, rsv2; } systick_t;
#define SYSTICK ((systick_t *)0xE000F000u)
#define PFIC_IENR1 REG32(0xE000E100u)
#define PFIC_IENR2 REG32(0xE000E104u)

#define TIM2_IRQn 38
#define SYSTICK_IRQn 12
