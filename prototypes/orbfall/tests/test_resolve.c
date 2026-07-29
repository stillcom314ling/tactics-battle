/* Native unit tests for the Orbfall resolver.
 * Build & run:  make test-orbfall
 * Encodes the resolver test cases from the P&D mechanics reference. */

#include "../resolve.h"

#include <stdio.h>
#include <string.h>

static int s_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        s_failures++; \
    } \
} while (0)

/* Build a board from 5 strings of 6 chars: F W G L D H J, '.' = filler that
 * never matches (unique-ish colors laid so no runs form). */
static void board_from(RzBoard *b, const char *rows[RZ_ROWS])
{
    static const char *codes = "FWGLDH J";
    for (int r = 0; r < RZ_ROWS; r++) {
        for (int c = 0; c < RZ_COLS; c++) {
            char ch = rows[r][c];
            RzOrb o = { 0 };
            if (ch == '.') {
                /* checkerboard of two colors can still run vertically with
                 * period 2? no: alternation r+c parity prevents any 2 in a
                 * row, so no 3-run can form among filler. But filler can
                 * extend an adjacent real run — tests avoid that. */
                o.color = ((r + c) % 2) ? RZ_LIGHT : RZ_DARK;
            } else {
                const char *p = strchr(codes, ch);
                o.color = (uint8_t)(p - codes);
                if (ch == 'J') o.color = RZ_JAMMER;
            }
            b->cell[r][c] = o;
        }
    }
}

static void test_block_is_one_match(void)
{
    /* 2x3 same-color block = ONE match, one combo */
    const char *rows[] = {
        "FFF...",
        "FFF...",
        "......",
        "......",
        "......",
    };
    RzBoard b; board_from(&b, rows);
    RzMatch m[RZ_MAX_MATCHES];
    int n = rz_find_matches(&b, m, RZ_MAX_MATCHES);
    CHECK(n == 1);
    CHECK(m[0].count == 6);
    CHECK(m[0].color == RZ_FIRE);
    CHECK(m[0].mass);
    CHECK(!m[0].is_tpa);
}

static void test_plus_is_one_match_of_5(void)
{
    /* plus of two intersecting 3-lines = ONE match of 5, mass attack, cross */
    const char *rows[] = {
        ".W....",
        "WWW...",
        ".W....",
        "......",
        "......",
    };
    RzBoard b; board_from(&b, rows);
    RzMatch m[RZ_MAX_MATCHES];
    int n = rz_find_matches(&b, m, RZ_MAX_MATCHES);
    CHECK(n == 1);
    CHECK(m[0].count == 5);
    CHECK(m[0].mass);
    CHECK(m[0].is_cross);
    CHECK(!m[0].is_l);
}

static void test_l_shape(void)
{
    const char *rows[] = {
        "G.....",
        "G.....",
        "GGG...",
        "......",
        "......",
    };
    RzBoard b; board_from(&b, rows);
    RzMatch m[RZ_MAX_MATCHES];
    int n = rz_find_matches(&b, m, RZ_MAX_MATCHES);
    CHECK(n == 1);
    CHECK(m[0].count == 5);
    CHECK(m[0].is_l);
    CHECK(!m[0].is_cross);
}

static void test_tpa_and_row(void)
{
    const char *rows[] = {
        "FFFF..",
        "......",
        "WWWWWW",
        "......",
        "......",
    };
    RzBoard b; board_from(&b, rows);
    RzMatch m[RZ_MAX_MATCHES];
    int n = rz_find_matches(&b, m, RZ_MAX_MATCHES);
    CHECK(n == 2);
    int fire = m[0].color == RZ_FIRE ? 0 : 1;
    int water = 1 - fire;
    CHECK(m[fire].is_tpa);
    CHECK(!m[fire].mass);
    CHECK(m[water].is_row);
    CHECK(m[water].count == 6);
    CHECK(m[water].mass);
}

static void test_box(void)
{
    const char *rows[] = {
        "DDD...",
        "DDD...",
        "DDD...",
        "......",
        "......",
    };
    /* '.' filler uses LIGHT/DARK — swap the box to fire to avoid contact */
    RzBoard b; board_from(&b, rows);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            b.cell[r][c].color = RZ_FIRE;
    RzMatch m[RZ_MAX_MATCHES];
    int n = rz_find_matches(&b, m, RZ_MAX_MATCHES);
    CHECK(n == 1);
    CHECK(m[0].count == 9);
    CHECK(m[0].is_box);
}

static void test_separate_matches_same_color(void)
{
    /* two disconnected 3-runs of one color = TWO matches (two combos) */
    const char *rows[] = {
        "FFF...",
        "......",
        "FFF...",
        "......",
        "......",
    };
    RzBoard b; board_from(&b, rows);
    RzMatch m[RZ_MAX_MATCHES];
    int n = rz_find_matches(&b, m, RZ_MAX_MATCHES);
    CHECK(n == 2);
    CHECK(m[0].count == 3 && m[1].count == 3);
}

static void test_two_orbs_never_match(void)
{
    const char *rows[] = {
        "FF....",
        "......",
        "......",
        "......",
        "......",
    };
    RzBoard b; board_from(&b, rows);
    RzMatch m[RZ_MAX_MATCHES];
    CHECK(rz_find_matches(&b, m, RZ_MAX_MATCHES) == 0);
}

static void test_jammers_match_too(void)
{
    const char *rows[] = {
        "JJJ...",
        "......",
        "......",
        "......",
        "......",
    };
    RzBoard b; board_from(&b, rows);
    RzMatch m[RZ_MAX_MATCHES];
    int n = rz_find_matches(&b, m, RZ_MAX_MATCHES);
    CHECK(n == 1);
    CHECK(m[0].color == RZ_JAMMER);
}

static void test_gravity_and_skyfall(void)
{
    RzRules rules; rz_rules_default(&rules);
    RzRng rng = { .s = 12345 };
    RzBoard b;
    rz_fill_no_match(&b, &rng, &rules);

    RzMatch m[RZ_MAX_MATCHES];
    CHECK(rz_find_matches(&b, m, RZ_MAX_MATCHES) == 0);

    /* knock out the bottom row, everything falls exactly 1 */
    uint32_t mask = 0;
    for (int c = 0; c < RZ_COLS; c++) mask |= rz_bit(RZ_ROWS - 1, c);
    rz_remove(&b, mask);

    uint8_t dist[RZ_ROWS][RZ_COLS];
    int moved = rz_gravity(&b, dist);
    CHECK(moved == RZ_CELLS - RZ_COLS);
    CHECK(dist[RZ_ROWS - 1][0] == 1);
    for (int c = 0; c < RZ_COLS; c++) CHECK(rz_cell_empty(&b, 0, c));

    uint8_t entry[RZ_ROWS][RZ_COLS];
    int spawned = rz_skyfall(&b, &rng, &rules, entry);
    CHECK(spawned == RZ_COLS);
    for (int c = 0; c < RZ_COLS; c++) {
        CHECK(!rz_cell_empty(&b, 0, c));
        CHECK(entry[0][c] == 1);
    }
}

static void test_no_skyfall_rule(void)
{
    RzRules rules; rz_rules_default(&rules);
    rules.no_skyfall = true;
    RzRng rng = { .s = 999 };
    RzBoard b;
    rz_fill_no_match(&b, &rng, &rules);
    rz_remove(&b, rz_bit(0, 0) | rz_bit(0, 1));

    uint8_t dist[RZ_ROWS][RZ_COLS];
    rz_gravity(&b, dist);
    uint8_t entry[RZ_ROWS][RZ_COLS];
    CHECK(rz_skyfall(&b, &rng, &rules, entry) == 0);
    CHECK(rz_cell_empty(&b, 0, 0));
}

static void test_determinism(void)
{
    RzRules rules; rz_rules_default(&rules);
    RzRng a = { .s = 777 }, c = { .s = 777 };
    RzBoard ba, bc;
    rz_fill_no_match(&ba, &a, &rules);
    rz_fill_no_match(&bc, &c, &rules);
    CHECK(memcmp(&ba, &bc, sizeof(ba)) == 0);
}

static void test_skyfall_weights(void)
{
    /* all weight on fire → every skyfall orb is fire */
    RzRules rules; rz_rules_default(&rules);
    for (int i = 0; i < RZ_COLOR_COUNT; i++) rules.color_weight[i] = 0.0f;
    rules.color_weight[RZ_FIRE] = 1.0f;

    RzRng rng = { .s = 31337 };
    RzBoard b;
    memset(&b, 0, sizeof(b));
    for (int r = 0; r < RZ_ROWS; r++)
        for (int c = 0; c < RZ_COLS; c++)
            b.cell[r][c].color = RZ_COLOR_COUNT; /* empty */

    uint8_t entry[RZ_ROWS][RZ_COLS];
    CHECK(rz_skyfall(&b, &rng, &rules, entry) == RZ_CELLS);
    for (int r = 0; r < RZ_ROWS; r++)
        for (int c = 0; c < RZ_COLS; c++)
            CHECK(b.cell[r][c].color == RZ_FIRE);
}

int main(void)
{
    test_block_is_one_match();
    test_plus_is_one_match_of_5();
    test_l_shape();
    test_tpa_and_row();
    test_box();
    test_separate_matches_same_color();
    test_two_orbs_never_match();
    test_jammers_match_too();
    test_gravity_and_skyfall();
    test_no_skyfall_rule();
    test_determinism();
    test_skyfall_weights();

    if (s_failures == 0) {
        printf("resolver: all tests passed\n");
        return 0;
    }
    printf("resolver: %d FAILURES\n", s_failures);
    return 1;
}
