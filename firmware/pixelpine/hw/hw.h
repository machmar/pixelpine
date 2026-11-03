/* PY32F003 board drivers (this folder). */
#pragma once
#include <stdint.h>

void scan_init(void);
void scan_show(const uint8_t *q);          /* 8-bit BCM level per LED, shown from the next phase on */
void scan_pause(void);
void scan_resume(void);
extern volatile uint32_t scan_frames;
