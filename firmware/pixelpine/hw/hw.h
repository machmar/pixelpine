/* PY32F003 board drivers (this folder). */
#pragma once
#include <stdint.h>

void scan_init(void);
void scan_show(uint8_t (*level)(int));     /* 8-bit BCM level per LED, shown from the next phase on */
void scan_pause(void);
void scan_resume(void);
extern volatile uint32_t scan_frames;

void sense_init(void);
uint16_t adc_read(uint8_t ch, uint8_t smp);
void sense_touch_isr(void);                     /* one touch sample, from the scan interrupt */
uint16_t sense_touch_raw(void);                 /* the touch star over the last frame (lower = more capacitance) */
double sense_touch_level(uint16_t raw);         /* -> the simulator's touch units */
uint16_t store_load_bond(void);                 /* Pine's bond, Q16 */
void store_save_bond(uint16_t bond);
