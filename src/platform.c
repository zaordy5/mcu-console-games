#include "game.h"
#include "platform.h"

/** Access a memory-mapped 32-bit peripheral register. */
#define REG(address) (*(volatile uint32_t *)(address))
/** Millisecond counter shared with the SysTick interrupt. */
static volatile uint32_t milliseconds;

/** Advance the monotonic counter once per millisecond. */
void SysTick_Handler(void) { ++milliseconds; }

uint32_t platform_ms(void) { return milliseconds; }

void platform_init(void)
{
    REG(0xe000ed08) = 0x08000000; /* Vector table in flash */
    REG(0x40023830) |= 1;         /* GPIOA clock */
    REG(0x40023840) |= 1u << 17;  /* USART2 clock */
    REG(0x40020000) = (REG(0x40020000) & ~0xf0u) | 0xa0u;
    REG(0x40020020) = (REG(0x40020020) & ~0xff00u) | 0x7700u; /* PA2/3 AF7 */
    REG(0x40004408) = 139;       /* 16 MHz / 115200 baud */
    REG(0x4000440c) = (1u << 13) | (1u << 3) | (1u << 2);
    REG(0xe000e014) = 15999;
    REG(0xe000e018) = 0;
    REG(0xe000e010) = 7;
}

int console_read(void)
{
    return (REG(0x40004400) & (1u << 5)) ? (int)(REG(0x40004404) & 255u) : -1;
}

void console_write(const char *text)
{
    while (*text) {
        while (!(REG(0x40004400) & (1u << 7))) { }
        REG(0x40004404) = (unsigned char)*text++;
    }
}
