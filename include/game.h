#pragma once
#include <stdbool.h>
#include <stdint.h>

/** Starting difficulty chosen before entering a game. */
typedef enum { GAME_EASY, GAME_NORMAL, GAME_HARD } GameDifficulty;

/** Single active game. Callbacks run synchronously on the main loop. */
typedef struct {
    const char *name;                       /**< Menu label. */
    void (*start)(GameDifficulty level);    /**< Reset state for a new session. */
    void (*key)(char input);                /**< Handle one UART byte. */
    void (*tick)(uint32_t elapsed_ms);      /**< Advance by elapsed milliseconds. */
    void (*draw)(void);                     /**< Paint pending changes to the terminal. */
    bool (*finished)(void);                 /**< Return true to go back to the menu. */
} Game;

/** Write a NUL-terminated string, including ANSI sequences, to USART2. */
void console_write(const char *text);
