/** @file
 *  Firmware initialization, input and timekeeping interface.
 */
#pragma once
#include <stdint.h>
/** Initialize USART2 and the millisecond tick at the reset HSI clock. */
void platform_init(void);
/** Return an unsigned byte or -1 when the UART has no pending input. */
int console_read(void);
/** Elapsed milliseconds modulo 2^32. */
uint32_t platform_ms(void);
