#pragma once
/* Orbfall match/cascade resolver.
 * Pure and deterministic: no raylib, no globals, seeded RNG passed in.
 * The game animates between steps; tests drive it to completion. */

#include <stdint.h>
#include <stdbool.h>

#define RZ_COLS  6
#define RZ_ROWS  5
#define RZ_CELLS (RZ_COLS * RZ_ROWS)

#define RZ_MAX_MATCHES RZ_CELLS

typedef enum {
    RZ_FIRE, RZ_WATER, RZ_WOOD, RZ_LIGHT, RZ_DARK, RZ_HEART,
    RZ_JAMMER,
    RZ_COLOR_COUNT
} RzColor;

#define RZ_NORMAL_COLORS 6   /* fire..heart; jammer is a hazard orb */

typedef struct {
    uint8_t color;
    bool    enhanced;  /* +orb: adds damage when matched            */
    bool    sealed;    /* tape: cannot be moved, still matchable    */
} RzOrb;

typedef struct { uint64_t s; } RzRng;

/* Board-rules struct: the single mechanism every relic, leader rider and
 * enemy debuff writes through. The resolver reads ONLY this — no effect
 * gets a special-case branch in the match code. */
typedef struct {
    float color_weight[RZ_COLOR_COUNT]; /* skyfall spawn weights          */
    float enhance_chance[RZ_NORMAL_COLORS];
    bool  no_skyfall;                   /* consistency-trade relics       */
} RzRules;

typedef struct {
    uint8_t  color;
    uint8_t  count;
    uint8_t  enhanced_count;
    bool     is_tpa;    /* exactly 4 connected                      */
    bool     is_row;    /* contains a full horizontal row           */
    bool     is_cross;  /* 5 orbs in a + shape                      */
    bool     is_l;      /* 5 orbs in an L shape                     */
    bool     is_box;    /* 3x3 block of 9                           */
    bool     mass;      /* 5+ connected: hits all enemies           */
    uint32_t cells;     /* bitmask, bit = r * RZ_COLS + c           */
} RzMatch;

typedef struct { RzOrb cell[RZ_ROWS][RZ_COLS]; } RzBoard;

static inline uint32_t rz_bit(int r, int c) { return 1u << (r * RZ_COLS + c); }

uint32_t rz_rand(RzRng *rng);
float    rz_randf(RzRng *rng);                 /* [0,1) */
int      rz_rand_range(RzRng *rng, int n);     /* [0,n) */

void rz_rules_default(RzRules *rules);

/* Weighted color roll using rules->color_weight. */
uint8_t rz_roll_color(RzRng *rng, const RzRules *rules);

/* Fill the whole board with no pre-existing 3-runs. */
void rz_fill_no_match(RzBoard *b, RzRng *rng, const RzRules *rules);

/* Find all matches on the board. P&D rule: an orb is matched if part of a
 * 3+ run (h or v); touching matched orbs of the same color merge into ONE
 * match. Returns match count. */
int rz_find_matches(const RzBoard *b, RzMatch out[], int max);

/* Remove every orb whose bit is set in cellmask. Removed cells become
 * RZ_COLOR_COUNT (empty sentinel in color field). */
void rz_remove(RzBoard *b, uint32_t cellmask);

/* Drop orbs into empty cells below them. fall_dist[r][c] = cells the orb now
 * at (r,c) fell (0 if it did not move). Returns number of moved orbs. */
int rz_gravity(RzBoard *b, uint8_t fall_dist[RZ_ROWS][RZ_COLS]);

/* Fill remaining empties from above. entry_dist[r][c] = rows above the board
 * the new orb starts at (for the drop animation); untouched cells get 0.
 * Honors rules->no_skyfall (leaves cells empty). Returns orbs spawned. */
int rz_skyfall(RzBoard *b, RzRng *rng, const RzRules *rules,
               uint8_t entry_dist[RZ_ROWS][RZ_COLS]);

static inline bool rz_cell_empty(const RzBoard *b, int r, int c)
{
    return b->cell[r][c].color >= RZ_COLOR_COUNT;
}
