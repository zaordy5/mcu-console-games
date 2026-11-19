/** @file tetris.h
 *  Registration of the falling-blocks game with the firmware menu.
 */
#ifndef TETRIS_H
/** Include guard for the firmware game interface. */
#define TETRIS_H

#include "game.h"

/** Falling-blocks callbacks for a single active session on USART2. */
extern const Game tetris_game;

#endif
