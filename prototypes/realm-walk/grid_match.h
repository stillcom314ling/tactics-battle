#pragma once
#include <stdint.h>

/* ------------------------------------------------------------------ tags */

typedef uint32_t TagMask;

#define TAG_PLAINS   (1u << 0)
#define TAG_FOREST   (1u << 1)
#define TAG_MOUNTAIN (1u << 2)
#define TAG_CITY     (1u << 3)
#define TAG_WATER    (1u << 4)

/* ------------------------------------------------------------------ tile */

typedef struct {
    int     id;
    TagMask tags;
} Tile;

/* ----------------------------------------------------------------- board */

typedef struct {
    int   width;
    int   height;
    Tile *tiles;   /* row-major: tiles[y * width + x] */
} Board;

/* ----------------------------------------------------------- conditions */

typedef enum {
    COND_ANY,
    COND_HAS_TAG,
    COND_NOT_TAG,
    COND_SAME_AS_ANCHOR,
    COND_SAME_AS_CELL
} ConditionType;

/* --------------------------------------------------------- pattern cell */

typedef struct {
    int           dx, dy;
    ConditionType cond;
    TagMask       tag_mask;   /* HAS_TAG / NOT_TAG */
    int           ref_index;  /* SAME_AS_CELL      */
} PatternCell;

#define MAX_PATTERN_CELLS 16

/* ------------------------------------------------------------- pattern */

typedef struct {
    int         cell_count;
    PatternCell cells[MAX_PATTERN_CELLS];
    int         anchor_index;
    int         min_dx, max_dx;
    int         min_dy, max_dy;
} Pattern;

/* -------------------------------------------------------- match result */

typedef struct {
    int x, y;
    int pattern_index;
} MatchEntry;

#define MAX_MATCHES 128

typedef struct {
    MatchEntry entries[MAX_MATCHES];
    int        count;
} MatchResult;

/* -------------------------------------------------------------- API */

int  match_pattern(const Board *b, const Pattern *p, int x, int y);

void find_all_matches(const Board *b, const Pattern *patterns,
                      int pattern_count, MatchResult *result);

void find_matches_in_region(const Board *b, const Pattern *patterns,
                            int pattern_count, MatchResult *result,
                            const int *scan_cols, int col_count,
                            const int *scan_rows, int row_count);
