#pragma once

#include "game.h"
#include "layout.h"

/** Number of progressively larger fields in the campaign. */
enum { MINES_LEVELS = 3 };

/** How a run was started and whether it has a next level. */
typedef enum { MINES_CAMPAIGN, MINES_CUSTOM, MINES_SHARED } MinesMode;

/** Controller states presented to the terminal renderer. */
typedef enum {
    MINES_PLAY, MINES_SEARCH, MINES_PAUSE, MINES_WIN, MINES_LOSS,
    MINES_SIZE_FORM, MINES_COUNT_FORM, MINES_LOAD_FORM, MINES_EXIT
} MinesScreen;

/** Progress that remains intact when a game is suspended in RAM. */
typedef struct {
    MinesBoard board;          /**< Current field and its marks. */
    MinesMode mode;            /**< Campaign, custom game, or shared layout. */
    GameDifficulty difficulty; /**< Density preset for campaign levels. */
    unsigned level;            /**< Zero-based index in the campaign. */
    uint8_t cursor;            /**< Selected cell as a row-major index. */
    uint8_t first;             /**< First cell of this exact layout. */
    uint32_t seed;             /**< Reproducible generator seed. */
    MinesLayout layout;        /**< Reproducible generator mode. */
    uint32_t played_ms;        /**< Active play time since first open. */
    bool logical;              /**< Verify the generated field with the solver. */
} MinesRun;

/** Forget which cells the terminal contains before changing external screens. */
void mines_view_invalidate(void);
/** Briefly tint newly opened terrain after a field's first move. */
void mines_view_first_reveal(void);
/** Advance visual feedback and return true when it expires. */
bool mines_view_tick(uint32_t elapsed_ms);
/** Draw the board, coordinates, and status, replacing only changed cells where possible. */
void mines_view_board(const MinesRun *run, MinesScreen screen, bool can_resume, bool search_failed);
/** Update the clock in the side panel without clearing the board. */
void mines_view_hud(const MinesRun *run);
/** Hide the field and show the pause controls and frozen time. */
void mines_view_pause(const MinesRun *run);
/** Draw a size, mine count, or layout code form and any validation message. */
void mines_view_form(MinesScreen screen, unsigned side, const char *input, bool invalid);
