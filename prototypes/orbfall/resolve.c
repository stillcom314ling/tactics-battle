#include "resolve.h"

#include <string.h>

#define RZ_EMPTY RZ_COLOR_COUNT

/* ------------------------------------------------------------------- rng */

uint32_t rz_rand(RzRng *rng)
{
    /* xorshift64* — deterministic across platforms */
    uint64_t x = rng->s;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng->s = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1DULL) >> 32);
}

float rz_randf(RzRng *rng)
{
    return (float)(rz_rand(rng) >> 8) / 16777216.0f;
}

int rz_rand_range(RzRng *rng, int n)
{
    return n > 0 ? (int)(rz_rand(rng) % (uint32_t)n) : 0;
}

/* ----------------------------------------------------------------- rules */

void rz_rules_default(RzRules *rules)
{
    memset(rules, 0, sizeof(*rules));
    for (int i = 0; i < RZ_NORMAL_COLORS; i++) rules->color_weight[i] = 1.0f;
    rules->color_weight[RZ_JAMMER] = 0.0f;
}

uint8_t rz_roll_color(RzRng *rng, const RzRules *rules)
{
    float total = 0.0f;
    for (int i = 0; i < RZ_COLOR_COUNT; i++) total += rules->color_weight[i];
    if (total <= 0.0f) return RZ_FIRE;

    float roll = rz_randf(rng) * total;
    for (int i = 0; i < RZ_COLOR_COUNT; i++) {
        roll -= rules->color_weight[i];
        if (roll < 0.0f) return (uint8_t)i;
    }
    return RZ_HEART;
}

static RzOrb roll_orb(RzRng *rng, const RzRules *rules)
{
    RzOrb o = { .color = rz_roll_color(rng, rules) };
    if (o.color < RZ_NORMAL_COLORS &&
        rz_randf(rng) < rules->enhance_chance[o.color])
        o.enhanced = true;
    return o;
}

void rz_fill_no_match(RzBoard *b, RzRng *rng, const RzRules *rules)
{
    for (int r = 0; r < RZ_ROWS; r++) {
        for (int c = 0; c < RZ_COLS; c++) {
            RzOrb o;
            int tries = 0;
            do {
                o = roll_orb(rng, rules);
                tries++;
            } while (tries < 32 &&
                     ((c >= 2 && b->cell[r][c-1].color == o.color
                              && b->cell[r][c-2].color == o.color) ||
                      (r >= 2 && b->cell[r-1][c].color == o.color
                              && b->cell[r-2][c].color == o.color)));
            b->cell[r][c] = o;
        }
    }
}

/* --------------------------------------------------------------- matching */

static bool mask_has(uint32_t m, int r, int c)
{
    if (r < 0 || r >= RZ_ROWS || c < 0 || c >= RZ_COLS) return false;
    return (m & rz_bit(r, c)) != 0;
}

/* Shape flags are derived from the merged cell mask alone. */
static void classify_match(RzMatch *m)
{
    int min_r = RZ_ROWS, max_r = -1, min_c = RZ_COLS, max_c = -1;
    for (int r = 0; r < RZ_ROWS; r++)
        for (int c = 0; c < RZ_COLS; c++)
            if (mask_has(m->cells, r, c)) {
                if (r < min_r) min_r = r;
                if (r > max_r) max_r = r;
                if (c < min_c) min_c = c;
                if (c > max_c) max_c = c;
            }
    int bw = max_c - min_c + 1;
    int bh = max_r - min_r + 1;

    m->is_tpa = (m->count == 4);
    m->mass   = (m->count >= 5);

    /* full horizontal row anywhere inside the match */
    for (int r = min_r; r <= max_r; r++) {
        bool full = true;
        for (int c = 0; c < RZ_COLS; c++)
            if (!mask_has(m->cells, r, c)) { full = false; break; }
        if (full) { m->is_row = true; break; }
    }

    /* + of 5: a cell whose 4 orthogonal neighbours are all in the match */
    if (m->count == 5 && bw == 3 && bh == 3) {
        for (int r = min_r + 1; r <= max_r - 1; r++)
            for (int c = min_c + 1; c <= max_c - 1; c++)
                if (mask_has(m->cells, r, c) &&
                    mask_has(m->cells, r-1, c) && mask_has(m->cells, r+1, c) &&
                    mask_has(m->cells, r, c-1) && mask_has(m->cells, r, c+1))
                    m->is_cross = true;

        /* L of 5: one full edge row + one full edge column of a 3x3 bbox,
         * meeting at a corner */
        if (!m->is_cross) {
            const int corner_r[4] = { min_r, min_r, max_r, max_r };
            const int corner_c[4] = { min_c, max_c, min_c, max_c };
            for (int k = 0; k < 4 && !m->is_l; k++) {
                uint32_t want = 0;
                for (int c = min_c; c <= max_c; c++) want |= rz_bit(corner_r[k], c);
                for (int r = min_r; r <= max_r; r++) want |= rz_bit(r, corner_c[k]);
                if (m->cells == want) m->is_l = true;
            }
        }
    }

    /* 3x3 solid box of 9 */
    m->is_box = (m->count == 9 && bw == 3 && bh == 3);
}

int rz_find_matches(const RzBoard *b, RzMatch out[], int max)
{
    bool hit[RZ_ROWS][RZ_COLS] = { 0 };

    /* pass 1: mark every orb in a 3+ horizontal or vertical run */
    for (int r = 0; r < RZ_ROWS; r++) {
        int c = 0;
        while (c < RZ_COLS) {
            uint8_t col = b->cell[r][c].color;
            int run = 1;
            while (c + run < RZ_COLS && b->cell[r][c + run].color == col) run++;
            if (col < RZ_COLOR_COUNT && run >= 3)
                for (int k = 0; k < run; k++) hit[r][c + k] = true;
            c += run;
        }
    }
    for (int c = 0; c < RZ_COLS; c++) {
        int r = 0;
        while (r < RZ_ROWS) {
            uint8_t col = b->cell[r][c].color;
            int run = 1;
            while (r + run < RZ_ROWS && b->cell[r + run][c].color == col) run++;
            if (col < RZ_COLOR_COUNT && run >= 3)
                for (int k = 0; k < run; k++) hit[r + k][c] = true;
            r += run;
        }
    }

    /* pass 2: flood-fill marked cells of one color into single matches.
     * This merge is the load-bearing P&D rule: a 2x3 block or a plus of two
     * 3-lines is ONE match, one combo. */
    bool seen[RZ_ROWS][RZ_COLS] = { 0 };
    int  count = 0;

    for (int r = 0; r < RZ_ROWS; r++) {
        for (int c = 0; c < RZ_COLS; c++) {
            if (!hit[r][c] || seen[r][c] || count >= max) continue;

            RzMatch m = { .color = b->cell[r][c].color };
            int stack[RZ_CELLS], top = 0;
            stack[top++] = r * RZ_COLS + c;
            seen[r][c] = true;

            while (top > 0) {
                int cell = stack[--top];
                int cr = cell / RZ_COLS, cc = cell % RZ_COLS;
                m.cells |= rz_bit(cr, cc);
                m.count++;
                if (b->cell[cr][cc].enhanced) m.enhanced_count++;

                const int dr[4] = { -1, 1, 0, 0 };
                const int dc[4] = { 0, 0, -1, 1 };
                for (int k = 0; k < 4; k++) {
                    int nr = cr + dr[k], nc = cc + dc[k];
                    if (nr < 0 || nr >= RZ_ROWS || nc < 0 || nc >= RZ_COLS)
                        continue;
                    if (hit[nr][nc] && !seen[nr][nc] &&
                        b->cell[nr][nc].color == m.color) {
                        seen[nr][nc] = true;
                        stack[top++] = nr * RZ_COLS + nc;
                    }
                }
            }

            classify_match(&m);
            out[count++] = m;
        }
    }
    return count;
}

/* ---------------------------------------------------------------- falling */

void rz_remove(RzBoard *b, uint32_t cellmask)
{
    for (int r = 0; r < RZ_ROWS; r++)
        for (int c = 0; c < RZ_COLS; c++)
            if (cellmask & rz_bit(r, c))
                b->cell[r][c] = (RzOrb){ .color = RZ_EMPTY };
}

int rz_gravity(RzBoard *b, uint8_t fall_dist[RZ_ROWS][RZ_COLS])
{
    memset(fall_dist, 0, RZ_CELLS);
    int moved = 0;
    for (int c = 0; c < RZ_COLS; c++) {
        int write = RZ_ROWS - 1;
        for (int r = RZ_ROWS - 1; r >= 0; r--) {
            if (rz_cell_empty(b, r, c)) continue;
            if (write != r) {
                b->cell[write][c] = b->cell[r][c];
                b->cell[r][c] = (RzOrb){ .color = RZ_EMPTY };
                fall_dist[write][c] = (uint8_t)(write - r);
                moved++;
            }
            write--;
        }
    }
    return moved;
}

int rz_skyfall(RzBoard *b, RzRng *rng, const RzRules *rules,
               uint8_t entry_dist[RZ_ROWS][RZ_COLS])
{
    memset(entry_dist, 0, RZ_CELLS);
    if (rules->no_skyfall) return 0;

    int spawned = 0;
    for (int c = 0; c < RZ_COLS; c++) {
        int empties = 0;
        for (int r = 0; r < RZ_ROWS; r++)
            if (rz_cell_empty(b, r, c)) empties++;

        int drop = 0;
        for (int r = 0; r < RZ_ROWS; r++) {
            if (!rz_cell_empty(b, r, c)) continue;
            b->cell[r][c] = roll_orb(rng, rules);
            /* orb enters from just above the board, stacked in spawn order */
            entry_dist[r][c] = (uint8_t)(r + empties - drop);
            drop++;
            spawned++;
        }
    }
    return spawned;
}
