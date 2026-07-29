#include "content.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ units */

const UnitDef OFC_UNITS[OFC_UNIT_COUNT] = {
    { "Cinder Adept",  RZ_FIRE,  1500, 3200, 150,
      { "Kindle",      SK_CONVERT,    RZ_WATER, RZ_FIRE,  0,  8 } },
    { "Tide Caller",   RZ_WATER, 1200, 3600, 300,
      { "Undertow",    SK_DELAY,      0, 0,              2, 12 } },
    { "Grove Warden",  RZ_WOOD,  1350, 3800, 250,
      { "Overgrow",    SK_CONVERT,    RZ_FIRE,  RZ_WOOD, 0,  8 } },
    { "Sun Herald",    RZ_LIGHT, 1600, 3000, 200,
      { "Radiance",    SK_ENHANCE,    0, RZ_LIGHT,       0,  6 } },
    { "Night Blade",   RZ_DARK,  1750, 2800, 120,
      { "Ruin Pulse",  SK_GRAVITY,    0, 0,             25, 15 } },
    { "Cleric of Dawn",RZ_LIGHT,  900, 3400, 500,
      { "Benediction", SK_HEARTMAKE,  RZ_DARK, 0,        0,  7 } },
    { "Stone Sentinel",RZ_WOOD,  1000, 5000, 100,
      { "Bulwark",     SK_SHIELD,     0, 0,             75, 10 } },
    { "Storm Djinn",   RZ_WATER, 1550, 2900, 180,
      { "Tempest",     SK_REFRESH,    0, 0,              0,  9 } },
    { "Ember Fox",     RZ_FIRE,  1300, 2600, 220,
      { "Foxfire",     SK_NUKE_FIXED, 0, 0,           5000,  9 } },
    { "Grave Doll",    RZ_DARK,  1450, 3100, 150,
      { "Severance",   SK_UNSEAL,     0, 0,              0,  6 } },
};

/* ---------------------------------------------------------------- leaders */

const LeaderDef OFC_LEADERS[OFC_LEADER_COUNT] = {
    { "Emberline Saint",
      "2x Fire ATK, +0.75x per orb past 4 in your biggest Fire match (max 6.5x)",
      LEAD_CONNECTED,
      { "Emberline Saint", RZ_FIRE, 1400, 3300, 200,
        { "Ignite", SK_ENHANCE, 0, RZ_FIRE, 0, 6 } },
      2.0f, 0.75f, 0, 6.5f, 0, 0, 1.0f, { 0, 8, 5 } },

    { "Cascade Sovereign",
      "3.5x ATK at 5+ combos, +0.5x per extra combo (max 6.5x)",
      LEAD_COMBO,
      { "Cascade Sovereign", RZ_WATER, 1350, 3200, 250,
        { "Flowstate", SK_TIME_BURST, 0, 0, 4, 8 } },
      3.5f, 0.5f, 5, 6.5f, 0, 0, 1.0f, { 1, 7, 3 } },

    { "Verdant Warlord",
      "3.5x ATK when matching Wood. Move time +2s",
      LEAD_MONO,
      { "Verdant Warlord", RZ_WOOD, 1450, 3500, 180,
        { "Bloom", SK_CONVERT, RZ_LIGHT, RZ_WOOD, 0, 8 } },
      3.5f, 0, 0, 3.5f, 0, 2.0f, 1.0f, { 2, 6, 5 } },

    { "Prism Empress",
      "2x ATK at 3 colors matched, 3.5x at 4, 5x at 5",
      LEAD_RAINBOW,
      { "Prism Empress", RZ_LIGHT, 1500, 3000, 220,
        { "Spectrum", SK_REFRESH, 0, 0, 0, 10 } },
      2.0f, 1.5f, 3, 5.0f, 0, 0, 1.0f, { 3, 4, 1 } },

    { "Duskmourn Regent",
      "4.5x ATK while HP is 80% or higher. RCV x1.5",
      LEAD_HP_HIGH,
      { "Duskmourn Regent", RZ_DARK, 1600, 2900, 150,
        { "Night Veil", SK_SHIELD, 0, 0, 50, 8 } },
      4.5f, 0, 80, 4.5f, 0, 0, 1.5f, { 4, 9, 5 } },

    { "Heartbound Oracle",
      "3x ATK and 60% damage reduction on heart-cross turns",
      LEAD_HEART_CROSS,
      { "Heartbound Oracle", RZ_LIGHT, 1300, 3400, 350,
        { "Communion", SK_HEARTMAKE, RZ_DARK, 0, 0, 7 } },
      3.0f, 0, 0, 3.0f, 0.60f, 0, 1.0f, { 5, 6, 8 } },
};

/* ----------------------------------------------------------------- relics */

const RelicDef OFC_RELICS[RELIC_COUNT] = {
    [RELIC_TWIN_FANGS]     = { "Twin Fangs",
        "Matches of exactly 4 deal 1.5x (stacks multiplicatively)", false },
    [RELIC_ROW_ENGINE]     = { "Row Engine",
        "Full 6-orb rows: +35% damage for that color per row", false },
    [RELIC_CROSSCUT]       = { "Crosscut",
        "Cross (+) matches deal 2x", false },
    [RELIC_CORNER_KEY]     = { "Corner Key",
        "L-shaped matches deal 1.8x and break every seal", false },
    [RELIC_VOIDPIERCER]    = { "Voidpiercer",
        "3x3 box matches deal 3.5x and pierce damage voids", false },
    [RELIC_SEVENS_FERVOR]  = { "Seven's Fervor",
        "2x damage at 7+ combos", false },
    [RELIC_DECIMATOR]      = { "Decimator",
        "5x damage at 10+ combos", false },
    [RELIC_MASS_DOCTRINE]  = { "Mass Doctrine",
        "3x damage when a single match has 12+ orbs", false },
    [RELIC_SPLIT_DOCTRINE] = { "Split Doctrine",
        "Colors matched 2+ separate times deal 1.5x", false },
    [RELIC_RAINBOW_CORE]   = { "Rainbow Core",
        "+35% damage per distinct color when 3+ colors attack", false },
    [RELIC_ATTUNED_SKY]    = { "Attuned Sky",
        "+18% skyfall chance for your leader's color", false },
    [RELIC_ENHANCED_SKY]   = { "Enhanced Sky",
        "Skyfall orbs have 25% chance to fall enhanced", false },
    [RELIC_STONE_SKY]      = { "Stone Sky",
        "No skyfall. Permanent 1.5x ATK", false },
    [RELIC_JAMMER_PACT]    = { "Jammer Pact",
        "8% jammer skyfall on YOUR board; each jammer match: 1.6x ATK", false },
    [RELIC_HEART_WARD]     = { "Heart Ward",
        "Heart-cross turns reduce damage taken by 75%", false },
    [RELIC_GUARD_BREAK]    = { "Guard Break",
        "Ignore enemy DEF when 4+ colors attack", false },
    [RELIC_ORB_FONT]       = { "Orb Font",
        "+20% enhanced orb spawns; enhanced orbs +12% damage each", false },
    [RELIC_JAMMER_SEAL]    = { "Hazard Seal",
        "Immune to enemy jammer and seal attacks", false },
    [RELIC_TIME_DILATOR]   = { "Time Dilator",
        "+1.5s orb move time", true },
    [RELIC_SKILL_BOOST]    = { "Skill Boost",
        "Active skills start 3 turns charged", true },
    [RELIC_VIGOR]          = { "Vigor Stone",
        "+25% max HP", true },
    [RELIC_AUTO_REPAIR]    = { "Auto-Repair",
        "Recover 600 HP every turn you match orbs", true },
};

/* share -> relative weight against `others` orbs of weight 1 */
static float share_weight(float share, int others)
{
    if (share >= 0.95f) share = 0.95f;
    if (share <= 0.0f) return 0.0f;
    return (float)others * share / (1.0f - share);
}

void ofc_build_rules(const RelicId *relics, int relic_count,
                     const LeaderDef *leader, float jammer_debuff_weight,
                     OfcRules *out, RzRules *rz_out)
{
    OfcRules r = {
        .tpa_mult = 1, .cross_mult = 1, .l_mult = 1, .box_mult = 1,
        .combo7_mult = 1, .combo10_mult = 1, .mass12_mult = 1,
        .split_mult = 1, .jammer_match_mult = 1, .flat_atk_mult = 1,
        .enhance_orb_bonus = 0.06f,
        .hp_mult = 1, .rcv_mult = 1,
    };
    r.time_extend = leader->time_rider;
    r.rcv_mult    = leader->rcv_rider;
    if (leader->kind == LEAD_HEART_CROSS)
        r.heart_cross_shield = leader->shield;

    rz_rules_default(rz_out);

    float leader_sky_share = 0.0f;
    float jammer_share     = 0.0f;
    float enhance_add      = 0.0f;

    for (int i = 0; i < relic_count; i++) {
        switch (relics[i]) {
        case RELIC_TWIN_FANGS:     r.tpa_mult   *= 1.5f;  break;
        case RELIC_ROW_ENGINE:     r.row_bonus  += 0.35f; break;
        case RELIC_CROSSCUT:       r.cross_mult *= 2.0f;  break;
        case RELIC_CORNER_KEY:     r.l_mult *= 1.8f; r.l_unseals = true; break;
        case RELIC_VOIDPIERCER:    r.box_mult *= 3.5f; r.vdp_pierce = true; break;
        case RELIC_SEVENS_FERVOR:  r.combo7_mult  *= 2.0f; break;
        case RELIC_DECIMATOR:      r.combo10_mult *= 5.0f; break;
        case RELIC_MASS_DOCTRINE:  r.mass12_mult  *= 3.0f; break;
        case RELIC_SPLIT_DOCTRINE: r.split_mult   *= 1.5f; break;
        case RELIC_RAINBOW_CORE:   r.rainbow_step += 0.35f; break;
        case RELIC_ATTUNED_SKY:    leader_sky_share += 0.18f; break;
        case RELIC_ENHANCED_SKY:   enhance_add += 0.25f; break;
        case RELIC_STONE_SKY:      rz_out->no_skyfall = true;
                                   r.flat_atk_mult *= 1.5f; break;
        case RELIC_JAMMER_PACT:    jammer_share += 0.08f;
                                   r.jammer_match_mult *= 1.6f; break;
        case RELIC_HEART_WARD:
            if (r.heart_cross_shield < 0.75f) r.heart_cross_shield = 0.75f;
            break;
        case RELIC_GUARD_BREAK:    r.guard_break = true; break;
        case RELIC_ORB_FONT:       enhance_add += 0.20f;
                                   r.enhance_orb_bonus += 0.06f; break;
        case RELIC_JAMMER_SEAL:    r.hazard_immune = true; break;
        case RELIC_TIME_DILATOR:   r.time_extend += 1.5f; break;
        case RELIC_SKILL_BOOST:    r.skill_boost += 3; break;
        case RELIC_VIGOR:          r.hp_mult *= 1.25f; break;
        case RELIC_AUTO_REPAIR:    r.auto_heal += 600; break;
        default: break;
        }
    }

    if (leader_sky_share > 0.0f) {
        float base = 1.0f / RZ_NORMAL_COLORS;
        rz_out->color_weight[leader->unit.color] =
            share_weight(base + leader_sky_share, RZ_NORMAL_COLORS - 1);
    }
    if (!r.hazard_immune) jammer_share += jammer_debuff_weight;
    if (jammer_share > 0.0f)
        rz_out->color_weight[RZ_JAMMER] =
            share_weight(jammer_share, RZ_NORMAL_COLORS);
    for (int i = 0; i < RZ_NORMAL_COLORS; i++)
        rz_out->enhance_chance[i] = enhance_add > 1.0f ? 1.0f : enhance_add;

    *out = r;
}

/* ---------------------------------------------------------------- enemies */

const EnemyDef OFC_ENEMIES[OFC_ENEMY_COUNT] = {
    { "Cindling", RZ_FIRE, 18000, 0, 1900,
      { { INT_ATTACK, 100 }, { INT_ATTACK, 100 }, { INT_BIG, 170 } }, 3 },
    { "Mire Toad", RZ_WATER, 22000, 0, 1600,
      { { INT_JAMMERS, 0, 3 }, { INT_ATTACK, 100 }, { INT_ATTACK, 120 } }, 3 },
    { "Thorn Cat", RZ_WOOD, 16000, 0, 2100,
      { { INT_ATTACK, 100 }, { INT_MULTI, 1100, 2 } }, 2 },
    { "Gloom Moth", RZ_DARK, 20000, 0, 1700,
      { { INT_SEAL, 0, 3 }, { INT_ATTACK, 100 },
        { INT_TIMECUT, 2, 2 }, { INT_ATTACK, 120 } }, 4 },
    { "Shard Golem", RZ_LIGHT, 60, 4000, 2500,
      { { INT_ATTACK, 100 }, { INT_SEAL, 0, 2 } }, 2 },
    { "Leech Lord", RZ_DARK, 26000, 0, 1800,
      { { INT_ATTACK, 100 }, { INT_HEAL, 20 }, { INT_BIG, 160 } }, 3 },
};

const EnemyDef OFC_ELITES[OFC_ELITE_COUNT] = {
    { "Twin Ogre", RZ_FIRE, 70000, 0, 2400,
      { { INT_MULTI, 1500, 2 }, { INT_JAMMERS, 0, 4 }, { INT_BIG, 200 } }, 3,
      0, 0, false, 0, 0, true, false },
    { "Combo Warden", RZ_LIGHT, 60000, 0, 2200,
      { { INT_ATTACK, 100 }, { INT_SEAL, 0, 4 }, { INT_BIG, 180 } }, 3,
      4, 0, false, 0, 0, true, false },
};

const EnemyDef OFC_BOSS = {
    "The Hollow King", RZ_DARK, 300000, 500, 3000,
    { { INT_BIG, 150 }, { INT_JAMMERS, 0, 5 },
      { INT_TIMECUT, 2, 2 }, { INT_MULTI, 1300, 3 } }, 4,
    0, 80000, true, 0.40f, 1.6f, false, true,
};

/* ------------------------------------------------------------- turn stats */

void ofc_turn_stats(const RzMatch *matches, int n, OfcTurnStats *out)
{
    memset(out, 0, sizeof(*out));
    out->combos = n;
    for (int i = 0; i < n; i++) {
        const RzMatch *m = &matches[i];
        if (m->color == RZ_JAMMER) { out->jammer_matches++; continue; }
        if (m->color == RZ_HEART) {
            if (m->is_cross) out->heart_cross = true;
            continue;
        }
        if (m->color >= RZ_NORMAL_COLORS) continue;
        int c = m->color;
        out->matches_of[c]++;
        if (m->count > out->largest_of[c]) out->largest_of[c] = m->count;
        if (m->is_row) out->rows_of[c]++;
    }
    for (int c = 0; c < RZ_NORMAL_COLORS - 1; c++)   /* hearts don't attack */
        if (c != RZ_HEART && out->matches_of[c] > 0) out->colors_matched++;
}

/* ---------------------------------------------------------- leader skills */

float ofc_leader_mult(const LeaderDef *ld, const OfcTurnStats *st,
                      float hp_ratio)
{
    float m = 1.0f;
    switch (ld->kind) {
    case LEAD_CONNECTED: {
        int n = st->largest_of[ld->unit.color];
        if (n >= 3) {
            m = ld->base_mult + ld->step_mult * (float)(n > 4 ? n - 4 : 0);
            if (m > ld->cap_mult) m = ld->cap_mult;
        }
        break;
    }
    case LEAD_COMBO:
        if (st->combos >= ld->threshold) {
            m = ld->base_mult + ld->step_mult * (float)(st->combos - ld->threshold);
            if (m > ld->cap_mult) m = ld->cap_mult;
        }
        break;
    case LEAD_MONO:
        if (st->matches_of[ld->unit.color] > 0) m = ld->base_mult;
        break;
    case LEAD_RAINBOW:
        if (st->colors_matched >= ld->threshold) {
            m = ld->base_mult
              + ld->step_mult * (float)(st->colors_matched - ld->threshold);
            if (m > ld->cap_mult) m = ld->cap_mult;
        }
        break;
    case LEAD_HP_HIGH:
        if (hp_ratio * 100.0f >= (float)ld->threshold - 0.01f)
            m = ld->base_mult;
        break;
    case LEAD_HEART_CROSS:
        if (st->heart_cross) m = ld->base_mult;
        break;
    }
    return m;
}

static const char *color_name(uint8_t c)
{
    static const char *names[] =
        { "Fire", "Water", "Wood", "Light", "Dark", "Heart", "Jammer" };
    return c < RZ_COLOR_COUNT ? names[c] : "?";
}

const char *ofc_leader_status(const LeaderDef *ld, const OfcTurnStats *st,
                              float hp_ratio, float *mult_out)
{
    static char buf[64];
    float m = ofc_leader_mult(ld, st, hp_ratio);
    if (mult_out) *mult_out = m;

    switch (ld->kind) {
    case LEAD_CONNECTED: {
        int n = st->largest_of[ld->unit.color];
        if (n >= 3) snprintf(buf, sizeof(buf), "%d %s linked: %.2fx",
                             n, color_name(ld->unit.color), m);
        else snprintf(buf, sizeof(buf), "link 3+ %s orbs",
                      color_name(ld->unit.color));
        break;
    }
    case LEAD_COMBO:
        if (st->combos >= ld->threshold)
            snprintf(buf, sizeof(buf), "%d combos: %.1fx", st->combos, m);
        else snprintf(buf, sizeof(buf), "%d/%d combos", st->combos, ld->threshold);
        break;
    case LEAD_MONO:
        snprintf(buf, sizeof(buf), m > 1.0f ? "%s matched: %.1fx"
                                            : "match %s orbs",
                 color_name(ld->unit.color), m);
        break;
    case LEAD_RAINBOW:
        if (m > 1.0f)
            snprintf(buf, sizeof(buf), "%d colors: %.1fx", st->colors_matched, m);
        else snprintf(buf, sizeof(buf), "%d/%d colors",
                      st->colors_matched, ld->threshold);
        break;
    case LEAD_HP_HIGH:
        snprintf(buf, sizeof(buf), m > 1.0f ? "HP %.0f%%: %.1fx"
                                            : "HP %.0f%% (need 80%%)",
                 hp_ratio * 100.0f, m);
        break;
    case LEAD_HEART_CROSS:
        snprintf(buf, sizeof(buf),
                 m > 1.0f ? "HEART CROSS: %.1fx + shield"
                          : "form a heart cross (%.1fx)",
                 m > 1.0f ? m : ld->base_mult);
        break;
    }
    return buf;
}

/* --------------------------------------------------------------- pipeline */

float ofc_attr_mult(uint8_t a, uint8_t d)
{
    if ((a == RZ_LIGHT && d == RZ_DARK) || (a == RZ_DARK && d == RZ_LIGHT))
        return 2.0f;
    if ((a == RZ_FIRE && d == RZ_WOOD) || (a == RZ_WOOD && d == RZ_WATER) ||
        (a == RZ_WATER && d == RZ_FIRE))
        return 2.0f;
    if ((a == RZ_WOOD && d == RZ_FIRE) || (a == RZ_WATER && d == RZ_WOOD) ||
        (a == RZ_FIRE && d == RZ_WATER))
        return 0.5f;
    return 1.0f;
}

static float orb_bonus(int count)
{
    return 1.0f + 0.25f * (float)(count - 3);
}

void ofc_compute_attack(const RzMatch *matches, int n,
                        const UnitDef *team[OFC_TEAM_SIZE],
                        const LeaderDef *leader, const OfcRules *rules,
                        float hp_ratio, uint8_t enemy_color, int enemy_def,
                        OfcAttack *out)
{
    memset(out, 0, sizeof(*out));

    OfcTurnStats st;
    ofc_turn_stats(matches, n, &st);

    out->combo_mult  = n > 0 ? 1.0f + 0.25f * (float)(n - 1) : 0.0f;
    out->leader_mult = ofc_leader_mult(leader, &st, hp_ratio);

    /* global multipliers shared by every unit */
    float global = out->combo_mult * out->leader_mult * rules->flat_atk_mult;
    if (n >= 7)  global *= rules->combo7_mult;
    if (n >= 10) global *= rules->combo10_mult;
    if (st.colors_matched >= 3 && rules->rainbow_step > 0.0f)
        global *= 1.0f + rules->rainbow_step * (float)st.colors_matched;
    for (int j = 0; j < st.jammer_matches; j++)
        global *= rules->jammer_match_mult;

    bool ignore_def = rules->guard_break && st.colors_matched >= 4;

    for (int u = 0; u < OFC_TEAM_SIZE; u++) {
        const UnitDef *unit = team[u];
        if (!unit) continue;
        uint8_t col = unit->color;

        double sum = 0.0;
        bool mass = false;
        for (int i = 0; i < n; i++) {
            const RzMatch *m = &matches[i];
            if (m->color != col) continue;
            double part = (double)unit->atk * orb_bonus(m->count);
            if (m->is_tpa)   part *= rules->tpa_mult;
            if (m->is_cross) part *= rules->cross_mult;
            if (m->is_l)     part *= rules->l_mult;
            if (m->is_box)   part *= rules->box_mult;
            part *= 1.0 + (double)rules->enhance_orb_bonus
                          * (double)m->enhanced_count;
            if (m->mass) mass = true;
            sum += part;
        }
        if (sum <= 0.0) continue;

        if (st.rows_of[col] > 0)
            sum *= 1.0 + (double)rules->row_bonus * (double)st.rows_of[col];
        if (st.matches_of[col] >= 2) sum *= rules->split_mult;
        if (st.largest_of[col] >= 12) sum *= rules->mass12_mult;

        sum *= global;
        sum *= ofc_attr_mult(col, enemy_color);

        long dmg = (long)sum;
        if (!ignore_def) {
            dmg -= enemy_def;
            if (dmg < 1) dmg = 1;
        }
        out->dmg[u]  = dmg;
        out->mass[u] = mass;
        out->total  += dmg;
    }

    /* healing: hearts run the identical orb/combo math on team RCV */
    long rcv_total = 0;
    for (int u = 0; u < OFC_TEAM_SIZE; u++)
        if (team[u]) rcv_total += team[u]->rcv;
    double heal = 0.0;
    for (int i = 0; i < n; i++)
        if (matches[i].color == RZ_HEART)
            heal += (double)rcv_total * orb_bonus(matches[i].count);
    heal *= out->combo_mult * rules->rcv_mult;
    out->heal = (long)heal;
    if (n > 0) out->heal += rules->auto_heal;

    if (st.heart_cross) out->shield = rules->heart_cross_shield;
}

/* ------------------------------------------------------------- formatting */

void ofc_fmt_num(long v, char *buf)
{
    if (v < 0) v = 0;
    if (v < 10000) { sprintf(buf, "%ld", v); return; }

    double d;
    char suffix;
    if (v < 1000000L)           { d = v / 1e3; suffix = 'K'; }
    else if (v < 1000000000L)   { d = v / 1e6; suffix = 'M'; }
    else                        { d = v / 1e9; suffix = 'B'; }

    if (d < 10.0)       sprintf(buf, "%.2f%c", d, suffix);
    else if (d < 100.0) sprintf(buf, "%.1f%c", d, suffix);
    else                sprintf(buf, "%.0f%c", d, suffix);
}
