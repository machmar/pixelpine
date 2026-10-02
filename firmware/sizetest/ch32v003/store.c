/* Pine's bond, kept across power cycles in the last 64-byte flash page (16 slots of 32 bits, wear levelled). */
#include "pp.h"
#include "regs.h"

#define PAGE 0x00003FC0u
#define SLOTS 16

static void flash_wait(void) { while (FLASH->STATR & 1u) {} }

uint16_t store_load_bond(void) {
  const volatile uint32_t *p = (const volatile uint32_t *)PAGE;
  uint16_t v = 19660;                               /* default bond 0.3 */
  for (uint8_t i = 0; i < SLOTS; i++) { if (p[i] == 0xFFFFFFFFu) break; v = (uint16_t)p[i]; }
  return v;
}

void store_save_bond(uint16_t bond) {
  volatile uint32_t *p = (volatile uint32_t *)PAGE;
  uint8_t i = 0;
  while (i < SLOTS && p[i] != 0xFFFFFFFFu) i++;
  FLASH->KEYR = 0x45670123u; FLASH->KEYR = 0xCDEF89ABu;
  FLASH->MODEKEYR = 0x45670123u; FLASH->MODEKEYR = 0xCDEF89ABu;
  if (i == SLOTS) {                                 /* page full: fast page erase (64 B) */
    FLASH->CTLR |= 1u << 17; FLASH->ADDR = PAGE; FLASH->CTLR |= 1u << 6; flash_wait();
    FLASH->CTLR &= ~(1u << 17); i = 0;
  }
  FLASH->CTLR |= 1u;                                /* PG */
  ((volatile uint16_t *)&p[i])[0] = bond; flash_wait();
  ((volatile uint16_t *)&p[i])[1] = 0; flash_wait();
  FLASH->CTLR &= ~1u;
  FLASH->CTLR |= 1u << 7;                           /* lock */
}
