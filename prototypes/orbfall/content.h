#pragma once
/* Orbfall content: units, leaders, relics, enemies, and the damage pipeline.
 * Pure data + math, no raylib. Every relic/leader/enemy effect flows through
 * OfcRules (game-side) and RzRules (resolver-side) — never through special
 * cases in match or damage code. */

#include "resolve.h"

/* ------------------------------------------------------------- team units */

typedef enum {
    SK_CONVERT,      /* from_color -> to_color                       */
    SK_ENHANCE,      /* enhance all orbs of to_color                 */
    SK_DELAY,        /* enemy intents pushed back `power` turns      */
    SK_SHIELD,       /* `power`% damage reduction for 1 turn         */
    SK_GRAVITY,      /* `power`% of enemy current HP, ignores DEF    */
    SK_NUKE_FIXED,   /* `power` fixed damage, ignores DEF and void   */
    SK_REFRESH,      /* reshuffle the whole board                    */
    SK_HEARTMAKE,    /* from_color -> heart                          */
    SK_UNSEAL,       /* clear all tape/seals                         */
    SK_TIME_BURST,   /* +`power` seconds move time, this turn only   */
} SkillKind;

typedef struct {
    const char *name;
    SkillKind   kind;
    uint8_t     from_color, to_color;
    int         power;
    int         cooldown;
} SkillDef;

typedef struct {
    const char *name;
    uint8_t     color;
    int         atk, hp, rcv;
    SkillDef    skill;
} UnitDef;

#define OFC_UNIT_COUNT 10
extern const UnitDef OFC_UNITS[OFC_UNIT_COUNT];

/* ---------------------------------------------------------------- leaders */

typedef enum {
    LEAD_CONNECTED,   /* ATK scales with largest connected match of color  */
    LEAD_COMBO,       /* ATK scales with total combo count                 */
    LEAD_MONO,        /* flat mult when matching own color; move-time rider*/
    LEAD_RAINBOW,     /* mult scales with distinct colors matched          */
    LEAD_HP_HIGH,     /* mult while HP >= threshold%                       */
    LEAD_HEART_CROSS, /* mult + damage shield on heart-cross turns         */
} LeadKind;

typedef struct {
    const char *name;
    const char *desc;      /* the condition, phrased for the player */
    LeadKind    kind;
    UnitDef     unit;      /* the leader is also a fighting unit    */
    float       base_mult;
    float       step_mult;
    int         threshold;
    float       cap_mult;
    float       shield;      /* heart-cross rider                   */
    float       time_rider;  /* +seconds move time rider            */
    float       rcv_rider;   /* RCV multiplier rider                */
    int         subs[3];     /* starting team, indexes into OFC_UNITS */
} LeaderDef;

#define OFC_LEADER_COUNT 6
extern const LeaderDef OFC_LEADERS[OFC_LEADER_COUNT];

/* ----------------------------------------------------------------- relics */

typedef enum {
    /* match-shape bucket (build-defining) */
    RELIC_TWIN_FANGS,      /* TPA: exactly-4 matches x1.5 (multiplicative) */
    RELIC_ROW_ENGINE,      /* +35% damage per full row of unit's color     */
    RELIC_CROSSCUT,        /* cross matches x2 for that color              */
    RELIC_CORNER_KEY,      /* L matches x1.8 and clear all seals           */
    RELIC_VOIDPIERCER,     /* 3x3 box x3.5, pierces damage void            */
    RELIC_SEVENS_FERVOR,   /* x2 at 7+ combos                              */
    RELIC_DECIMATOR,       /* x5 at 10+ combos                             */
    RELIC_MASS_DOCTRINE,   /* x3 when one match has 12+ orbs               */
    RELIC_SPLIT_DOCTRINE,  /* x1.5 for colors matched 2+ separate times    */
    RELIC_RAINBOW_CORE,    /* +0.35x per distinct attack color at 3+       */
    RELIC_ATTUNED_SKY,     /* +18% skyfall of leader color                 */
    RELIC_ENHANCED_SKY,    /* skyfall orbs 25% chance enhanced             */
    RELIC_STONE_SKY,       /* no skyfall; permanent x1.5 ATK               */
    RELIC_JAMMER_PACT,     /* 8% jammer skyfall; jammer matches x1.6 ATK   */
    RELIC_HEART_WARD,      /* heart cross: 75% damage reduction            */
    RELIC_GUARD_BREAK,     /* ignore DEF when 4+ colors attack             */
    RELIC_ORB_FONT,        /* +25% enhanced spawn, +6%/enhanced orb        */
    RELIC_JAMMER_SEAL,     /* immune to enemy jammer and seal attacks      */
    /* utility bucket (never build-defining, capped small) */
    RELIC_TIME_DILATOR,    /* +1.5s move time                              */
    RELIC_SKILL_BOOST,     /* actives start 3 turns charged                */
    RELIC_VIGOR,           /* +25% max HP                                  */
    RELIC_AUTO_REPAIR,     /* heal 600 every turn you match                */
    RELIC_COUNT
} RelicId;

typedef struct {
    const char *name;
    const char *desc;
    bool        utility;   /* true = stat/QoL bucket */
} RelicDef;

extern const RelicDef OFC_RELICS[RELIC_COUNT];

/* Aggregated game-side rules: sum of leader riders + all held relics. */
typedef struct {
    float tpa_mult;
    float row_bonus;
    float cross_mult;
    float l_mult;
    bool  l_unseals;
    float box_mult;
    bool  vdp_pierce;
    float combo7_mult;
    float combo10_mult;
    float mass12_mult;
    float split_mult;
    float rainbow_step;
    float jammer_match_mult;
    bool  guard_break;
    float heart_cross_shield;
    float enhance_orb_bonus;     /* extra per enhanced orb in a match */
    bool  hazard_immune;
    float flat_atk_mult;
    float time_extend;
    int   skill_boost;
    float hp_mult;
    float rcv_mult;
    int   auto_heal;
} OfcRules;

#define OFC_MAX_RELICS 16

/* Build OfcRules and resolver RzRules from held relics + chosen leader.
 * leader_color feeds ATTUNED_SKY; jammer_debuff_weight is the enemy's
 * skyfall poisoning (0 normally). */
void ofc_build_rules(const RelicId *relics, int relic_count,
                     const LeaderDef *leader, float jammer_debuff_weight,
                     OfcRules *out, RzRules *rz_out);

/* ---------------------------------------------------------------- enemies */

typedef enum {
    INT_ATTACK,   /* value = atk multiplier x100                     */
    INT_MULTI,    /* count hits of value damage each                 */
    INT_BIG,      /* value = atk multiplier x100, flagged dangerous  */
    INT_JAMMERS,  /* convert `count` orbs to jammers                 */
    INT_SEAL,     /* tape `count` orbs                               */
    INT_TIMECUT,  /* -value seconds move time for `count` turns      */
    INT_HEAL,     /* heal self value% of max HP                      */
} IntentKind;

typedef struct {
    IntentKind kind;
    int        value;
    int        count;
} Intent;

#define OFC_MAX_PATTERN 5

typedef struct {
    const char *name;
    uint8_t     color;
    long        hp;
    int         def;
    int         atk;
    Intent      pattern[OFC_MAX_PATTERN];
    int         pattern_len;
    int         combo_shield;   /* 0 = none, else combos required to damage */
    long        void_over;      /* 0 = none, else nullify hits >= this      */
    bool        resolve;        /* survives lethal at 1 HP above 30%        */
    float       enrage_below;   /* HP ratio that triggers enrage, 0 = none  */
    float       enrage_mult;
    bool        is_elite;
    bool        is_boss;
} EnemyDef;

#define OFC_ENEMY_COUNT   6
#define OFC_ELITE_COUNT   2
#define OFC_BOSS_COUNT    1
extern const EnemyDef OFC_ENEMIES[OFC_ENEMY_COUNT];
extern const EnemyDef OFC_ELITES[OFC_ELITE_COUNT];
extern const EnemyDef OFC_BOSS;

/* ---------------------------------------------------------- damage pipeline */

#define OFC_TEAM_SIZE     4   /* leader + 3 subs */
#define OFC_MAX_TURN_MATCHES 64

/* Everything a leader condition or relic can key on, derived once per turn
 * (and per frame mid-drag for the live condition preview). */
typedef struct {
    int  combos;
    int  colors_matched;                     /* distinct attack colors */
    int  matches_of[RZ_NORMAL_COLORS];
    int  largest_of[RZ_NORMAL_COLORS];
    int  rows_of[RZ_NORMAL_COLORS];
    bool heart_cross;
    int  jammer_matches;
    bool board_cleared;
} OfcTurnStats;

void ofc_turn_stats(const RzMatch *matches, int n, OfcTurnStats *out);

/* Current leader multiplier for these stats (1.0 when condition unmet). */
float ofc_leader_mult(const LeaderDef *ld, const OfcTurnStats *st,
                      float hp_ratio);
/* One-line live condition report for the HUD, e.g. "5 combos -> 4.0x". */
const char *ofc_leader_status(const LeaderDef *ld, const OfcTurnStats *st,
                              float hp_ratio, float *mult_out);

typedef struct {
    long   dmg[OFC_TEAM_SIZE];    /* post-DEF damage per unit         */
    bool   mass[OFC_TEAM_SIZE];   /* hits all enemies                 */
    long   total;
    long   heal;
    float  combo_mult;
    float  leader_mult;
    float  shield;                /* damage reduction earned this turn */
} OfcAttack;

/* Full P&D pipeline: per-match orb/shape/enhance math, then combo bonus,
 * leader mult, relic mults, attribute chart, enemy DEF (floor 1). */
void ofc_compute_attack(const RzMatch *matches, int n,
                        const UnitDef *team[OFC_TEAM_SIZE],
                        const LeaderDef *leader, const OfcRules *rules,
                        float hp_ratio, uint8_t enemy_color, int enemy_def,
                        OfcAttack *out);

float ofc_attr_mult(uint8_t attacker, uint8_t defender);

/* 12400 -> "12.4K", 1234567 -> "1.23M"; buf must hold 16. */
void ofc_fmt_num(long v, char *buf);
