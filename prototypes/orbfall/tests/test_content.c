/* Native tests for the damage pipeline (P&D reference test cases). */

#include "../content.h"

#include <stdio.h>
#include <string.h>

static int s_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        s_failures++; \
    } \
} while (0)

#define NEAR(a, b) ((a) > (b) * 0.999 - 1 && (a) < (b) * 1.001 + 1)

static const UnitDef s_fire_unit =
    { "T", RZ_FIRE, 1000, 1000, 100, { "s", SK_SHIELD, 0, 0, 0, 5 } };

static void base_rules(OfcRules *r)
{
    RelicId none[1] = { 0 };
    RzRules rz;
    ofc_build_rules(none, 0, &OFC_LEADERS[0], 0, r, &rz);
}

static RzMatch mk(uint8_t color, int count)
{
    RzMatch m = { .color = color, .count = (uint8_t)count };
    m.is_tpa = (count == 4);
    m.mass   = (count >= 5);
    return m;
}

static void test_tpa_math(void)
{
    /* 4-orb match with TPA relic: ATK x 1.25 x 1.5 */
    OfcRules r; base_rules(&r);
    r.tpa_mult = 1.5f;

    const UnitDef *team[OFC_TEAM_SIZE] = { &s_fire_unit, 0, 0, 0 };
    RzMatch m[1] = { mk(RZ_FIRE, 4) };

    /* wood leader so leader condition (fire) is off -> mult 1 */
    OfcAttack a;
    ofc_compute_attack(m, 1, team, &OFC_LEADERS[2], &r, 1.0f,
                       RZ_WATER, 0, &a);
    /* neutral would be fire vs water = 0.5x; use wood enemy = 2x?
     * fire vs wood is 2x advantage — pick a light enemy for 1.0x */
    ofc_compute_attack(m, 1, team, &OFC_LEADERS[2], &r, 1.0f,
                       RZ_LIGHT, 0, &a);
    CHECK(NEAR(a.dmg[0], (long)(1000 * 1.25 * 1.5)));
}

static void test_combo_bonus_applies_to_all(void)
{
    /* 3 combos, one 5-connected: 1.5x combo bonus on all, mass from the 5 */
    OfcRules r; base_rules(&r);
    const UnitDef *team[OFC_TEAM_SIZE] = { &s_fire_unit, 0, 0, 0 };
    RzMatch m[3] = { mk(RZ_FIRE, 5), mk(RZ_WATER, 3), mk(RZ_WOOD, 3) };

    /* HP-conditional leader at 50% HP: condition unmet, leader mult 1x */
    OfcAttack a;
    ofc_compute_attack(m, 3, team, &OFC_LEADERS[4], &r, 0.5f,
                       RZ_LIGHT, 0, &a);
    CHECK(NEAR(a.combo_mult, 1.5f));
    CHECK(a.mass[0]);
    /* 1000 * 1.5 (5 orbs) * 1.5 (3 combos) */
    CHECK(NEAR(a.dmg[0], 2250));
}

static void test_def_floor(void)
{
    /* 9000 damage into DEF 10000 = 1 */
    OfcRules r; base_rules(&r);
    UnitDef u = s_fire_unit; u.atk = 9000;
    const UnitDef *team[OFC_TEAM_SIZE] = { &u, 0, 0, 0 };
    RzMatch m[1] = { mk(RZ_FIRE, 3) };

    OfcAttack a;
    ofc_compute_attack(m, 1, team, &OFC_LEADERS[2], &r, 1.0f,
                       RZ_LIGHT, 10000, &a);
    CHECK(a.dmg[0] == 1);
}

static void test_attribute_chart(void)
{
    CHECK(ofc_attr_mult(RZ_FIRE, RZ_WOOD) == 2.0f);
    CHECK(ofc_attr_mult(RZ_WOOD, RZ_FIRE) == 0.5f);
    CHECK(ofc_attr_mult(RZ_LIGHT, RZ_DARK) == 2.0f);
    CHECK(ofc_attr_mult(RZ_DARK, RZ_LIGHT) == 2.0f);
    CHECK(ofc_attr_mult(RZ_FIRE, RZ_FIRE) == 1.0f);
}

static void test_leader_combo_condition(void)
{
    /* Cascade Sovereign: 3.5x at 5 combos, +0.5/extra, cap 6.5 */
    const LeaderDef *ld = &OFC_LEADERS[1];
    OfcTurnStats st = { 0 };
    st.combos = 4;
    CHECK(ofc_leader_mult(ld, &st, 1.0f) == 1.0f);
    st.combos = 5;
    CHECK(NEAR(ofc_leader_mult(ld, &st, 1.0f), 3.5f));
    st.combos = 8;
    CHECK(NEAR(ofc_leader_mult(ld, &st, 1.0f), 5.0f));
    st.combos = 20;
    CHECK(NEAR(ofc_leader_mult(ld, &st, 1.0f), 6.5f));
}

static void test_skyfall_condition_retrigger(void)
{
    /* combo-count leader condition keys off total combos incl skyfall:
     * stats built from the accumulated match list */
    RzMatch m[6];
    for (int i = 0; i < 6; i++) m[i] = mk(RZ_WATER, 3);
    OfcTurnStats st;
    ofc_turn_stats(m, 6, &st);
    CHECK(st.combos == 6);
    CHECK(NEAR(ofc_leader_mult(&OFC_LEADERS[1], &st, 1.0f), 4.0f));
}

static void test_heal_pipeline(void)
{
    OfcRules r; base_rules(&r);
    UnitDef u = s_fire_unit; /* rcv 100 */
    const UnitDef *team[OFC_TEAM_SIZE] = { &u, &u, 0, 0 };
    RzMatch m[2] = { mk(RZ_HEART, 4), mk(RZ_FIRE, 3) };

    OfcAttack a;
    ofc_compute_attack(m, 2, team, &OFC_LEADERS[2], &r, 1.0f,
                       RZ_LIGHT, 0, &a);
    /* 200 rcv * 1.25 (4 orbs) * 1.25 (2 combos) */
    CHECK(NEAR(a.heal, 312));
}

static void test_fmt_num(void)
{
    char b[16];
    ofc_fmt_num(9999, b);     CHECK(strcmp(b, "9999") == 0);
    ofc_fmt_num(12400, b);    CHECK(strcmp(b, "12.4K") == 0);
    ofc_fmt_num(1230000, b);  CHECK(strcmp(b, "1.23M") == 0);
    ofc_fmt_num(999999, b);   CHECK(strcmp(b, "1000.0K") != 0); /* stays K */
}

static void test_rules_aggregation(void)
{
    RelicId relics[3] = { RELIC_TWIN_FANGS, RELIC_TWIN_FANGS, RELIC_VIGOR };
    OfcRules r; RzRules rz;
    ofc_build_rules(relics, 3, &OFC_LEADERS[0], 0, &r, &rz);
    CHECK(NEAR(r.tpa_mult, 2.25f));      /* multiplicative stacking */
    CHECK(NEAR(r.hp_mult, 1.25f));

    RelicId stone[1] = { RELIC_STONE_SKY };
    ofc_build_rules(stone, 1, &OFC_LEADERS[0], 0, &r, &rz);
    CHECK(rz.no_skyfall);
    CHECK(NEAR(r.flat_atk_mult, 1.5f));
}

int main(void)
{
    test_tpa_math();
    test_combo_bonus_applies_to_all();
    test_def_floor();
    test_attribute_chart();
    test_leader_combo_condition();
    test_skyfall_condition_retrigger();
    test_heal_pipeline();
    test_fmt_num();
    test_rules_aggregation();

    if (s_failures == 0) {
        printf("content: all tests passed\n");
        return 0;
    }
    printf("content: %d FAILURES\n", s_failures);
    return 1;
}
