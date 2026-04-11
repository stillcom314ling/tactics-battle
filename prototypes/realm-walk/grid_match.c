#include "grid_match.h"
#include <stdbool.h>
#include <string.h>

/* -------------------------------------------------------- match_pattern */

int match_pattern(const Board *b, const Pattern *p, int x, int y)
{
    const Tile *anchor = &b->tiles[y * b->width + x];
    const Tile *matched[MAX_PATTERN_CELLS];

    for (int i = 0; i < p->cell_count; i++) {
        const PatternCell *c = &p->cells[i];

        int tx = x + c->dx;
        int ty = y + c->dy;

        if (tx < 0 || ty < 0 || tx >= b->width || ty >= b->height)
            return 0;

        const Tile *t = &b->tiles[ty * b->width + tx];
        matched[i] = t;

        switch (c->cond) {
            case COND_ANY:
                break;

            case COND_HAS_TAG:
                if ((t->tags & c->tag_mask) != c->tag_mask) return 0;
                break;

            case COND_NOT_TAG:
                if (t->tags & c->tag_mask) return 0;
                break;

            case COND_SAME_AS_ANCHOR:
                if (t->id != anchor->id) return 0;
                break;

            case COND_SAME_AS_CELL:
                if (t->id != matched[c->ref_index]->id) return 0;
                break;
        }
    }

    return 1;
}

/* ----------------------------------------------------- find_all_matches */

void find_all_matches(const Board *b, const Pattern *patterns,
                      int pattern_count, MatchResult *result)
{
    result->count = 0;

    for (int y = 0; y < b->height; y++) {
        for (int x = 0; x < b->width; x++) {
            for (int p = 0; p < pattern_count; p++) {
                const Pattern *pat = &patterns[p];

                if (x + pat->min_dx < 0 || x + pat->max_dx >= b->width)
                    continue;
                if (y + pat->min_dy < 0 || y + pat->max_dy >= b->height)
                    continue;

                if (match_pattern(b, pat, x, y)) {
                    if (result->count < MAX_MATCHES) {
                        result->entries[result->count++] = (MatchEntry){
                            .x = x, .y = y, .pattern_index = p
                        };
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------ find_matches_in_region */

void find_matches_in_region(const Board *b, const Pattern *patterns,
                            int pattern_count, MatchResult *result,
                            const int *scan_cols, int col_count,
                            const int *scan_rows, int row_count)
{
    result->count = 0;

    /*
     * Build a visited bitmap so we don't test the same (x, y, pattern)
     * twice when a position is reachable from both a visited column and
     * a visited row.
     *
     * One bit per cell: visited[y * width + x].  We only need to track
     * whether (x,y) was already used as an anchor for *any* pattern —
     * the inner loop tests all patterns at each anchor, so a single bit
     * per cell is enough.
     */
    bool visited[4096];  /* 30 * 20 = 600 — fits easily */
    memset(visited, 0, (size_t)(b->width * b->height) * sizeof(bool));

    /* Scan positions reachable from visited columns (full column sweep) */
    for (int ci = 0; ci < col_count; ci++) {
        int col = scan_cols[ci];
        for (int y = 0; y < b->height; y++) {
            if (visited[y * b->width + col]) continue;
            visited[y * b->width + col] = true;

            for (int p = 0; p < pattern_count; p++) {
                const Pattern *pat = &patterns[p];

                if (col + pat->min_dx < 0 || col + pat->max_dx >= b->width)
                    continue;
                if (y + pat->min_dy < 0 || y + pat->max_dy >= b->height)
                    continue;

                if (match_pattern(b, pat, col, y)) {
                    if (result->count < MAX_MATCHES) {
                        result->entries[result->count++] = (MatchEntry){
                            .x = col, .y = y, .pattern_index = p
                        };
                    }
                }
            }
        }
    }

    /* Scan positions reachable from visited rows (full row sweep) */
    for (int ri = 0; ri < row_count; ri++) {
        int row = scan_rows[ri];
        for (int x = 0; x < b->width; x++) {
            if (visited[row * b->width + x]) continue;
            visited[row * b->width + x] = true;

            for (int p = 0; p < pattern_count; p++) {
                const Pattern *pat = &patterns[p];

                if (x + pat->min_dx < 0 || x + pat->max_dx >= b->width)
                    continue;
                if (row + pat->min_dy < 0 || row + pat->max_dy >= b->height)
                    continue;

                if (match_pattern(b, pat, x, row)) {
                    if (result->count < MAX_MATCHES) {
                        result->entries[result->count++] = (MatchEntry){
                            .x = x, .y = row, .pattern_index = p
                        };
                    }
                }
            }
        }
    }
}
