/* Pine's bond, kept across power cycles, PY32F003. The PY32 programs flash a whole 128-byte page at a time, so
 * saves rotate through the last 8 pages (1 KB): each save erases the next page and writes {magic, seq, bond}.
 * Sequence follows Puya's HAL (py32f0xx_hal_flash.c). NOT tested on hardware. */
#include "pp.h"
#include "regs.h"

#define STORE_BASE 0x0800FC00u
#define PAGES 8
#define PAGE_WORDS 32
#define MAGIC 0x656E6950u                           /* "Pine" */

static const volatile uint32_t *page(uint8_t i) { return (const volatile uint32_t *)(STORE_BASE + i * 4u * PAGE_WORDS); }
static uint32_t last_seq;
static uint8_t last_pg = PAGES - 1;

uint16_t store_load_bond(void) {
  uint16_t v = 19660;                               /* default bond 0.3 */
  for (uint8_t i = 0; i < PAGES; i++) {
    const volatile uint32_t *p = page(i);
    if (p[0] == MAGIC && p[1] >= last_seq) { last_seq = p[1]; last_pg = i; v = (uint16_t)p[2]; }
  }
  return v;
}

/* erase/program timings for the 24 MHz HSI, from the factory table (HAL: __HAL_FLASH_TIMMING_SEQUENCE_CONFIG) */
static void flash_timing_24m(void) {
  const volatile uint32_t *t = FLASH_TIMING_24M;
  FLASH->TS0 = t[0] & 0xFFu; FLASH->TS1 = (t[0] >> 16) & 0x1FFu; FLASH->TS3 = (t[0] >> 8) & 0xFFu;
  FLASH->TS2P = t[1] & 0xFFu; FLASH->TPS3 = (t[1] >> 16) & 0x7FFu;
  FLASH->PERTPE = t[2] & 0x1FFFFu; FLASH->SMERTPE = t[3] & 0x1FFFFu;
  FLASH->PRGTPE = t[4] & 0xFFFFu; FLASH->PRETPE = (t[4] >> 16) & 0x3FFFu;
}

static void flash_wait(void) { while (FLASH->SR & (1u << 16)) {} FLASH->SR = FLASH->SR; }   /* BSY, then clear */

/* runs from RAM: the page buffer is filled word by word and flash can't be read meanwhile */
__attribute__((section(".ramfunc"), noinline, long_call))
static void page_program(uint32_t addr, const uint32_t *src) {
  FLASH->CR |= 1u;                                  /* PG */
  for (uint8_t i = 0; i < PAGE_WORDS; i++) {
    if (i == PAGE_WORDS - 1) FLASH->CR |= 1u << 19; /* PGSTRT before the last word */
    ((volatile uint32_t *)addr)[i] = src[i];
  }
  while (FLASH->SR & (1u << 16)) {}
  FLASH->CR &= ~1u;
}

void store_save_bond(uint16_t bond) {
  uint32_t buf[PAGE_WORDS];
  for (uint8_t i = 0; i < PAGE_WORDS; i++) buf[i] = 0xFFFFFFFFu;
  buf[0] = MAGIC; buf[1] = ++last_seq; buf[2] = bond;
  last_pg = (uint8_t)((last_pg + 1) % PAGES);
  uint32_t addr = (uint32_t)page(last_pg);
  flash_timing_24m();
  FLASH->KEYR = 0x45670123u; FLASH->KEYR = 0xCDEF89ABu;
  FLASH->CR |= 1u << 1;                             /* PER: the next write erases the page (LEDs pause ~4 ms) */
  *(volatile uint32_t *)addr = 0xFF;
  flash_wait();
  FLASH->CR &= ~(1u << 1);
  __asm__ volatile("cpsid i");
  page_program(addr, buf);
  __asm__ volatile("cpsie i");
  flash_wait();
  FLASH->CR |= 1u << 31;                            /* lock */
}
