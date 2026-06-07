#include "drag_disp_draft.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================ constants */
#define COLS          6
#define ROWS          5
#define NUM_ORB_TYPES 6      /* Fire Water Wood Light Dark Heart */
#define NUM_ELEMENTS  5      /* Fire..Dark — team-scoring colours  */
#define DRAG_SECS     3.0f
#define LERP_SPD      16.0f
#define FLASH_SECS    0.35f
#define CLEAR_SECS    0.25f
#define FALL_SECS     0.32f

#define MAX_MATCHES   32
#define NUM_LEADERS   4
#define NUM_ACTIVES   4
#define TEAM_SIZE     6
#define RUN_TURNS     5

/* ============================================================ types */
typedef enum { ORB_FIRE, ORB_WATER, ORB_WOOD, ORB_LIGHT, ORB_DARK, ORB_HEART } OrbType;

typedef struct {
    OrbType type;
    Vector2 vis;
    float   alpha;
    bool    alive;
    bool    matched;
} Orb;

typedef enum {
    PHASE_IDLE, PHASE_DRAGGING, PHASE_FLASH, PHASE_CLEAR, PHASE_FALL, PHASE_REFILL,
} Phase;

typedef enum { LS_STEADY_HAND, LS_COMBO_MASTER, LS_TWO_PRONGED, LS_FLAME_HEART } LeaderSkillId;
typedef enum { AS_BURST, AS_DAMPENER, AS_ELEMENT_LOCK, AS_COMBO_BOOST } ActiveSkillId;

typedef struct {
    OrbType       element;     /* Fire..Dark */
    int           atk;         /* 500..2000  */
    LeaderSkillId leader;
    ActiveSkillId active;
    int           lock_color;  /* param for Element Lock */
    int           cd;          /* active cooldown remaining (0 = ready) */
} Monster;

typedef struct {
    Monster m[TEAM_SIZE];
    int     leader_idx;
} Team;

typedef struct { int color; int size; } Match;

typedef struct {
    Match matches[MAX_MATCHES];
    int   match_count;
    int   combo_count;   /* match groups across all cascades (incl. hearts) */
    int   heart_count;   /* heart match groups → active-skill charges        */
} BoardResult;

typedef enum { SCREEN_DRAFT, SCREEN_ASSIGN, SCREEN_RUN, SCREEN_RESULTS } Screen;
typedef enum { TURN_PLAN, TURN_RESOLVING, TURN_SCORED } TurnPhase;

typedef struct { int a; int b; } DraftOpt; /* chassis: a=element,b=atk; skill: a=id */

/* ============================================================ skill tables */
typedef struct { const char *name; const char *desc; } LeaderDef;
typedef struct { const char *name; const char *desc; int cd_max; } ActiveDef;

static const LeaderDef LEADER_DEFS[NUM_LEADERS] = {
    { "Steady Hand",  "Flat x1.8 ATK, always."        },
    { "Combo Master", "x1.5/2.5/3.5 at 5/7/9+ combos." },
    { "Two-Pronged",  "x2 ATK with exactly 2 colors."  },
    { "Flame Heart",  "x2.5 Fire when 4+ Fire orbs."   },
};

static const ActiveDef ACTIVE_DEFS[NUM_ACTIVES] = {
    { "Burst",        "TurnScore x1.5.",              4 },
    { "Dampener",     "TurnScore x0.5.",              3 },
    { "Element Lock", "Only locked color scores x3.", 5 },
    { "Combo Boost",  "+2 combos this turn.",         5 },
};

static const char *ELEM_NAME[NUM_ORB_TYPES] = {
    "Fire", "Water", "Wood", "Light", "Dark", "Heart"
};

static const int TARGETS[RUN_TURNS] = { 3000, 6000, 12000, 20000, 35000 };

/* ============================================================ module state */
/* board */
static Orb   s_grid[ROWS][COLS];
static Phase s_phase;
static float s_phase_t;
static bool    s_dragging;
static OrbType s_held_type;
static int     s_held_r, s_held_c;
static Vector2 s_held_vis;
static float   s_drag_timer;
static bool    s_held_moved;
static int     s_prev_tc;

/* meta game */
static Screen      s_screen;
static Team        s_team;
static int         s_pick;            /* 0..17 */
static DraftOpt    s_opts[3];
static int         s_turn;            /* 0..4 */
static int         s_run_points;
static int         s_turn_pts[RUN_TURNS];
static TurnPhase   s_turn_phase;
static bool        s_armed[TEAM_SIZE];
static BoardResult s_turn_board;
static int         s_last_score, s_last_target, s_last_points;

/* ============================================================ palette */
static const Color ORB_COL[NUM_ORB_TYPES] = {
    { 255, 110,  80, 255 }, /* fire  */
    {  80, 170, 255, 255 }, /* water */
    {  90, 200, 120, 255 }, /* wood  */
    { 255, 215,  70, 255 }, /* light */
    { 170,  90, 230, 255 }, /* dark  */
    { 240, 120, 170, 255 }, /* heart */
};

#define BG_COL   (Color){ 245, 240, 230, 255 }
#define INK_COL  (Color){  80,  70,  60, 255 }
#define BTN_COL  (Color){  70, 110, 180, 255 }
#define BTN_HOT  (Color){  95, 140, 215, 255 }
#define BTN_OFF  (Color){ 165, 160, 150, 255 }

/* ============================================================ board layout */
static float cell_sz(void) { return (float)GetScreenWidth() / (COLS + 1); }
static float grid_ox(void) { return ((float)GetScreenWidth() - COLS * cell_sz()) * 0.5f; }
static float grid_oy(void) { return (float)GetScreenHeight() * 0.42f; }

static Vector2 cell_center(int r, int c)
{
    float cs = cell_sz();
    return (Vector2){ grid_ox() + c * cs + cs * 0.5f,
                      grid_oy() + r * cs + cs * 0.5f };
}

static void cell_at(Vector2 p, int *out_r, int *out_c)
{
    float cs = cell_sz();
    int c = (int)((p.x - grid_ox()) / cs);
    int r = (int)((p.y - grid_oy()) / cs);
    if (r < 0 || r >= ROWS || c < 0 || c >= COLS) { *out_r = -1; *out_c = -1; return; }
    *out_r = r;
    *out_c = c;
}

/* ============================================================ input */
static Vector2 input_pos(void)
{
    if (GetTouchPointCount() > 0) return GetTouchPosition(0);
    return GetMousePosition();
}

static bool input_just_pressed(void)
{
    int tc = GetTouchPointCount();
    if (tc > 0 && s_prev_tc == 0) return true;
    if (tc == 0) return IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    return false;
}

static bool input_just_released(void)
{
    int tc = GetTouchPointCount();
    if (tc == 0 && s_prev_tc > 0) return true;
    if (tc == 0) return IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    return false;
}

static bool tapped(Rectangle r)
{
    return input_just_pressed() && CheckCollisionPointRec(input_pos(), r);
}

/* ============================================================ small draw helpers */
static void draw_panel(Rectangle r, Color c)
{
    DrawRectangleRounded(r, 0.16f, 8, c);
}

static void draw_text_centered(const char *s, float cx, float y, int fs, Color c)
{
    int w = MeasureText(s, fs);
    DrawText(s, (int)(cx - w * 0.5f), (int)y, fs, c);
}

static void draw_button(Rectangle r, const char *label, bool enabled)
{
    bool hot = enabled && CheckCollisionPointRec(input_pos(), r);
    draw_panel(r, enabled ? (hot ? BTN_HOT : BTN_COL) : BTN_OFF);
    int fs = (int)(r.height * 0.36f); if (fs < 12) fs = 12;
    draw_text_centered(label, r.x + r.width * 0.5f, r.y + (r.height - fs) * 0.5f, fs, RAYWHITE);
}

/* ============================================================ board logic */
static void add_match(int color, int size)
{
    if (s_turn_board.match_count < MAX_MATCHES) {
        s_turn_board.matches[s_turn_board.match_count].color = color;
        s_turn_board.matches[s_turn_board.match_count].size  = size;
        s_turn_board.match_count++;
    }
    s_turn_board.combo_count++;
    if (color == ORB_HEART) s_turn_board.heart_count++;
}

static int find_matches(void)
{
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            s_grid[r][c].matched = false;

    /* horizontal runs */
    for (int r = 0; r < ROWS; r++) {
        int c = 0;
        while (c < COLS) {
            if (!s_grid[r][c].alive) { c++; continue; }
            int run = 1;
            while (c + run < COLS && s_grid[r][c + run].alive &&
                   s_grid[r][c + run].type == s_grid[r][c].type)
                run++;
            if (run >= 3) {
                for (int k = 0; k < run; k++) s_grid[r][c + k].matched = true;
                add_match(s_grid[r][c].type, run);
            }
            c += run;
        }
    }

    /* vertical runs */
    for (int c = 0; c < COLS; c++) {
        int r = 0;
        while (r < ROWS) {
            if (!s_grid[r][c].alive) { r++; continue; }
            int run = 1;
            while (r + run < ROWS && s_grid[r + run][c].alive &&
                   s_grid[r + run][c].type == s_grid[r][c].type)
                run++;
            if (run >= 3) {
                for (int k = 0; k < run; k++) s_grid[r + k][c].matched = true;
                add_match(s_grid[r][c].type, run);
            }
            r += run;
        }
    }

    int count = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (s_grid[r][c].matched) count++;
    return count;
}

static void compact_columns(void)
{
    for (int c = 0; c < COLS; c++) {
        int write = ROWS - 1;
        for (int r = ROWS - 1; r >= 0; r--) {
            if (!s_grid[r][c].alive) continue;
            if (write != r) {
                s_grid[write][c]   = s_grid[r][c];
                s_grid[r][c].alive = false;
            }
            write--;
        }
        for (int r = write; r >= 0; r--) s_grid[r][c].alive = false;
    }
}

static void refill_columns(void)
{
    float cs = cell_sz();
    for (int c = 0; c < COLS; c++) {
        int empty = 0;
        for (int r = 0; r < ROWS; r++) if (!s_grid[r][c].alive) empty++;
        int drop = 0;
        for (int r = 0; r < ROWS; r++) {
            if (s_grid[r][c].alive) continue;
            s_grid[r][c] = (Orb){
                .type    = (OrbType)GetRandomValue(0, NUM_ORB_TYPES - 1),
                .alive   = true,
                .matched = false,
                .alpha   = 1.0f,
                .vis     = { cell_center(r, c).x,
                             grid_oy() - (float)(empty - drop) * cs },
            };
            drop++;
        }
    }
}

static void generate_board(void)
{
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            OrbType t;
            int tries = 0;
            do {
                t = (OrbType)GetRandomValue(0, NUM_ORB_TYPES - 1);
                tries++;
            } while (tries < 20 &&
                     ((c >= 2 && s_grid[r][c-1].type == t && s_grid[r][c-2].type == t) ||
                      (r >= 2 && s_grid[r-1][c].type == t && s_grid[r-2][c].type == t)));
            s_grid[r][c] = (Orb){ .type=t, .vis=cell_center(r,c), .alpha=1.0f, .alive=true };
        }
    }
}

static void release_orb(void)
{
    s_grid[s_held_r][s_held_c] = (Orb){
        .type = s_held_type, .alive = true, .alpha = 1.0f, .vis = s_held_vis,
    };
    s_dragging   = false;
    s_drag_timer = 0.0f;

    int matched = find_matches();
    s_phase   = (matched > 0) ? PHASE_FLASH : PHASE_IDLE;
    s_phase_t = 0.0f;
}

/* ============================================================ scoring */
static int calc_turn_score(const BoardResult *b, const Team *t)
{
    long color_atk[NUM_ELEMENTS] = { 0 };
    for (int i = 0; i < TEAM_SIZE; i++)
        if (t->m[i].element < NUM_ELEMENTS)
            color_atk[t->m[i].element] += t->m[i].atk;

    /* armed actives */
    bool burst = false, dampen = false, combo_boost = false;
    int  lock_color = -1;
    for (int i = 0; i < TEAM_SIZE; i++) {
        if (!s_armed[i]) continue;
        switch (t->m[i].active) {
            case AS_BURST:        burst = true; break;
            case AS_DAMPENER:     dampen = true; break;
            case AS_COMBO_BOOST:  combo_boost = true; break;
            case AS_ELEMENT_LOCK: lock_color = t->m[i].lock_color; break;
        }
    }

    /* per-colour multipliers from matches */
    double color_mult[NUM_ELEMENTS] = { 0 };
    bool   present[NUM_ELEMENTS]    = { false };
    int    fire_orbs = 0, colors_matched = 0;
    for (int i = 0; i < b->match_count; i++) {
        int c = b->matches[i].color;
        if (c == ORB_HEART || c >= NUM_ELEMENTS) continue;   /* hearts don't score */
        int sz = b->matches[i].size;
        double f = (sz >= 5) ? 1.5 : (sz == 4) ? 1.25 : 1.0;
        if (!present[c]) { present[c] = true; color_mult[c] = f; colors_matched++; }
        else             { color_mult[c] *= 1.5; }
        if (c == ORB_FIRE) fire_orbs += sz;
    }

    LeaderSkillId leader = t->m[t->leader_idx].leader;

    double base = 0.0;
    for (int c = 0; c < NUM_ELEMENTS; c++) {
        if (!present[c]) continue;
        if (lock_color >= 0 && c != lock_color) continue;    /* element lock */
        double cs = (double)color_atk[c] * color_mult[c];
        if (lock_color == c) cs *= 3.0;
        if (c == ORB_FIRE && leader == LS_FLAME_HEART && fire_orbs >= 4) cs *= 2.5;
        base += cs;
    }

    int combos = b->combo_count + (combo_boost ? 2 : 0);
    double combo_mult = (combos >= 1) ? (1.0 + 0.25 * (combos - 1)) : 1.0;

    double lmult = 1.0;
    switch (leader) {
        case LS_STEADY_HAND:  lmult = 1.8; break;
        case LS_TWO_PRONGED:  lmult = (colors_matched == 2) ? 2.0 : 1.0; break;
        case LS_COMBO_MASTER: lmult = combos >= 9 ? 3.5 : combos >= 7 ? 2.5
                                                   : combos >= 5 ? 1.5 : 1.0; break;
        case LS_FLAME_HEART:  lmult = 1.0; break;  /* applied per-colour */
    }

    double amult = 1.0;
    if (burst)  amult *= 1.5;
    if (dampen) amult *= 0.5;

    return (int)(base * combo_mult * lmult * amult + 0.5);
}

/* ============================================================ draft */
static void pick_distinct(int n, int k, int out[])
{
    int pool[NUM_LEADERS > NUM_ACTIVES ? NUM_LEADERS : NUM_ACTIVES];
    for (int i = 0; i < n; i++) pool[i] = i;
    for (int i = 0; i < k; i++) {
        int j = GetRandomValue(i, n - 1);
        int tmp = pool[i]; pool[i] = pool[j]; pool[j] = tmp;
        out[i] = pool[i];
    }
}

static void gen_options(void)
{
    int comp = s_pick % 3;
    if (comp == 0) {                       /* chassis */
        for (int i = 0; i < 3; i++) {
            s_opts[i].a = GetRandomValue(0, NUM_ELEMENTS - 1);
            s_opts[i].b = GetRandomValue(5, 20) * 100;   /* 500..2000 */
        }
    } else {                               /* leader / active: 3 distinct ids */
        int ids[3];
        int n = (comp == 1) ? NUM_LEADERS : NUM_ACTIVES;
        pick_distinct(n, 3, ids);
        for (int i = 0; i < 3; i++) { s_opts[i].a = ids[i]; s_opts[i].b = 0; }
    }
}

static void apply_choice(int idx)
{
    int mon  = s_pick / 3;
    int comp = s_pick % 3;
    Monster *m = &s_team.m[mon];
    if (comp == 0) { m->element = (OrbType)s_opts[idx].a; m->atk = s_opts[idx].b; }
    else if (comp == 1) { m->leader = (LeaderSkillId)s_opts[idx].a; }
    else {
        m->active = (ActiveSkillId)s_opts[idx].a;
        m->lock_color = GetRandomValue(0, NUM_ELEMENTS - 1);
    }
    s_pick++;
    if (s_pick >= TEAM_SIZE * 3) s_screen = SCREEN_ASSIGN;
    else gen_options();
}

/* ============================================================ run flow */
static void start_turn(void)
{
    generate_board();
    s_phase      = PHASE_IDLE;
    s_phase_t    = 0.0f;
    s_dragging   = false;
    s_drag_timer = 0.0f;
    s_held_moved = false;
    s_turn_board = (BoardResult){ 0 };
    for (int i = 0; i < TEAM_SIZE; i++) s_armed[i] = false;
    s_turn_phase = TURN_PLAN;
}

static void start_run(void)
{
    s_run_points = 0;
    s_turn       = 0;
    for (int i = 0; i < RUN_TURNS; i++) s_turn_pts[i] = 0;
    for (int i = 0; i < TEAM_SIZE; i++) s_team.m[i].cd = 0;  /* all ready */
    s_screen = SCREEN_RUN;
    start_turn();
}

static void score_turn(void)
{
    int ts     = calc_turn_score(&s_turn_board, &s_team);
    int target = TARGETS[s_turn];
    int pts    = target - abs(target - ts);
    if (pts < 0) pts = 0;

    s_run_points    += pts;
    s_turn_pts[s_turn] = pts;
    s_last_score     = ts;
    s_last_target    = target;
    s_last_points    = pts;

    int dec = 1 + s_turn_board.heart_count;
    for (int i = 0; i < TEAM_SIZE; i++) {
        if (s_armed[i]) s_team.m[i].cd = ACTIVE_DEFS[s_team.m[i].active].cd_max;
        else { s_team.m[i].cd -= dec; if (s_team.m[i].cd < 0) s_team.m[i].cd = 0; }
    }
    s_turn_phase = TURN_SCORED;
}

/* ============================================================ ui layout rects */
static Rectangle draft_card_rect(int i)
{
    float sw = GetScreenWidth(), sh = GetScreenHeight();
    float cw = sw * 0.86f, ch = sh * 0.15f, gap = sh * 0.025f;
    float y0 = sh * 0.34f;
    return (Rectangle){ (sw - cw) * 0.5f, y0 + i * (ch + gap), cw, ch };
}

static Rectangle assign_card_rect(int i)
{
    float sw = GetScreenWidth(), sh = GetScreenHeight();
    int col = i % 2, row = i / 2;
    float cw = sw * 0.42f, ch = sh * 0.11f, gx = sw * 0.04f, gy = sh * 0.02f;
    float x0 = (sw - (2 * cw + gx)) * 0.5f, y0 = sh * 0.20f;
    return (Rectangle){ x0 + col * (cw + gx), y0 + row * (ch + gy), cw, ch };
}

static Rectangle assign_start_rect(void)
{
    float sw = GetScreenWidth(), sh = GetScreenHeight();
    return (Rectangle){ sw * 0.25f, sh * 0.82f, sw * 0.5f, sh * 0.08f };
}

static Rectangle active_btn_rect(int i)
{
    float sw = GetScreenWidth(), sh = GetScreenHeight();
    int col = i % 3, row = i / 3;
    float bw = sw * 0.30f, bh = sh * 0.058f, gx = sw * 0.022f, gy = sh * 0.012f;
    float x0 = (sw - (3 * bw + 2 * gx)) * 0.5f, y0 = sh * 0.845f;
    return (Rectangle){ x0 + col * (bw + gx), y0 + row * (bh + gy), bw, bh };
}

static Rectangle next_btn_rect(void)
{
    float sw = GetScreenWidth(), sh = GetScreenHeight();
    return (Rectangle){ sw * 0.30f, sh * 0.62f, sw * 0.4f, sh * 0.07f };
}

static Rectangle play_again_rect(void)
{
    float sw = GetScreenWidth(), sh = GetScreenHeight();
    return (Rectangle){ sw * 0.28f, sh * 0.80f, sw * 0.44f, sh * 0.08f };
}

/* ============================================================ board draw */
static void draw_board(void)
{
    float cs = cell_sz(), orb_r = cs * 0.42f, gx = grid_ox(), gy = grid_oy();

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            Rectangle cr = { gx + c * cs + 3, gy + r * cs + 3, cs - 6, cs - 6 };
            DrawRectangleRounded(cr, 0.20f, 6, (Color){ 228, 222, 210, 255 });
        }

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            Orb *o = &s_grid[r][c];
            if (!o->alive) continue;
            Color col = Fade(ORB_COL[o->type], o->alpha);
            if (o->matched && s_phase == PHASE_FLASH)
                DrawCircleV(o->vis, orb_r + 5.0f, Fade(WHITE, 0.75f));
            DrawCircleV(o->vis, orb_r, col);
            DrawCircleV((Vector2){ o->vis.x - orb_r * 0.28f, o->vis.y - orb_r * 0.28f },
                        orb_r * 0.28f, Fade(WHITE, 0.28f * o->alpha));
        }

    if (s_dragging) {
        float r_big = orb_r * 1.22f;
        DrawCircleV((Vector2){ s_held_vis.x + 5, s_held_vis.y + 7 }, r_big, (Color){ 0,0,0,55 });
        DrawCircleV(s_held_vis, r_big, ORB_COL[s_held_type]);
        DrawCircleV((Vector2){ s_held_vis.x - r_big * 0.28f, s_held_vis.y - r_big * 0.28f },
                    r_big * 0.28f, Fade(WHITE, 0.35f));
        float frac = s_drag_timer / DRAG_SECS;
        Color tcol = frac > 0.4f ? (Color){ 80,200,80,220 } : (Color){ 220,60,60,220 };
        if (frac > 0.001f)
            DrawRing(s_held_vis, r_big + 6.0f, r_big + 11.0f, -90.0f, -90.0f + frac * 360.0f, 32, tcol);
    }
}

/* ============================================================ screen: draft */
static void draw_draft(void)
{
    float sw = GetScreenWidth(), sh = GetScreenHeight();
    int comp = s_pick % 3, mon = s_pick / 3;
    const char *comp_name[3] = { "Chassis", "Leader Skill", "Active Skill" };

    draw_text_centered("DRAFT", sw * 0.5f, sh * 0.06f, (int)(sw / 12), INK_COL);
    char sub[96];
    snprintf(sub, sizeof(sub), "Pick %d/18  -  Monster %d  -  %s",
             s_pick + 1, mon + 1, comp_name[comp]);
    draw_text_centered(sub, sw * 0.5f, sh * 0.14f, (int)(sw / 26), INK_COL);

    int big = (int)(sw / 22), small = (int)(sw / 34);
    for (int i = 0; i < 3; i++) {
        Rectangle r = draft_card_rect(i);
        bool hot = CheckCollisionPointRec(input_pos(), r);
        draw_panel(r, hot ? (Color){ 230, 224, 250, 255 } : (Color){ 236, 232, 224, 255 });

        char l1[64], l2[96];
        if (comp == 0) {
            DrawRectangleRounded((Rectangle){ r.x + 12, r.y + 12, r.height * 0.5f, r.height * 0.5f },
                                 0.4f, 8, ORB_COL[s_opts[i].a]);
            snprintf(l1, sizeof(l1), "%s", ELEM_NAME[s_opts[i].a]);
            snprintf(l2, sizeof(l2), "ATK %d", s_opts[i].b);
        } else if (comp == 1) {
            snprintf(l1, sizeof(l1), "%s", LEADER_DEFS[s_opts[i].a].name);
            snprintf(l2, sizeof(l2), "%s", LEADER_DEFS[s_opts[i].a].desc);
        } else {
            snprintf(l1, sizeof(l1), "%s", ACTIVE_DEFS[s_opts[i].a].name);
            snprintf(l2, sizeof(l2), "%s  (CD %d)", ACTIVE_DEFS[s_opts[i].a].desc,
                     ACTIVE_DEFS[s_opts[i].a].cd_max);
        }
        float tx = r.x + r.width * 0.36f;
        DrawText(l1, (int)tx, (int)(r.y + r.height * 0.22f), big, INK_COL);
        DrawText(l2, (int)tx, (int)(r.y + r.height * 0.58f), small, (Color){ 110,100,90,255 });
    }
}

/* ============================================================ screen: assign */
static void draw_assign(void)
{
    float sw = GetScreenWidth(), sh = GetScreenHeight();
    draw_text_centered("ASSIGN LEADER", sw * 0.5f, sh * 0.06f, (int)(sw / 14), INK_COL);
    draw_text_centered("Tap a monster to make it Leader",
                       sw * 0.5f, sh * 0.13f, (int)(sw / 30), (Color){ 110,100,90,255 });

    int big = (int)(sw / 30), small = (int)(sw / 40);
    for (int i = 0; i < TEAM_SIZE; i++) {
        Rectangle r = assign_card_rect(i);
        bool lead = (i == s_team.leader_idx);
        draw_panel(r, lead ? (Color){ 250, 235, 200, 255 } : (Color){ 236, 232, 224, 255 });
        if (lead) DrawRectangleRoundedLines(r, 0.16f, 8, (Color){ 220, 160, 40, 255 });

        Monster *m = &s_team.m[i];
        DrawRectangleRounded((Rectangle){ r.x + 10, r.y + 10, r.height * 0.3f, r.height * 0.3f },
                             0.4f, 8, ORB_COL[m->element]);
        char l1[64], l2[80];
        snprintf(l1, sizeof(l1), "%s %d%s", ELEM_NAME[m->element], m->atk, lead ? "  [L]" : "");
        snprintf(l2, sizeof(l2), "%s / %s", LEADER_DEFS[m->leader].name, ACTIVE_DEFS[m->active].name);
        DrawText(l1, (int)(r.x + r.width * 0.28f), (int)(r.y + r.height * 0.18f), big, INK_COL);
        DrawText(l2, (int)(r.x + 12), (int)(r.y + r.height * 0.6f), small, (Color){ 110,100,90,255 });
    }
    draw_button(assign_start_rect(), "Start Run", true);
}

/* ============================================================ screen: run */
static void draw_run(void)
{
    float sw = GetScreenWidth(), sh = GetScreenHeight();
    int hud = (int)(sw / 20);

    char hdr[64];
    snprintf(hdr, sizeof(hdr), "Turn %d/5", s_turn + 1);
    DrawText(hdr, (int)(sw * 0.05f), (int)(sh * 0.03f), hud, INK_COL);
    char rp[48];
    snprintf(rp, sizeof(rp), "Run Pts: %d", s_run_points);
    int rpw = MeasureText(rp, hud);
    DrawText(rp, (int)(sw * 0.95f - rpw), (int)(sh * 0.03f), hud, INK_COL);

    char tgt[48];
    snprintf(tgt, sizeof(tgt), "TARGET  %d", TARGETS[s_turn]);
    draw_text_centered(tgt, sw * 0.5f, sh * 0.09f, (int)(sw / 12), (Color){ 200, 90, 40, 255 });

    draw_board();

    /* combo badge */
    if (s_turn_board.combo_count > 0 &&
        (s_phase == PHASE_FLASH || s_phase == PHASE_CLEAR ||
         s_phase == PHASE_FALL  || s_phase == PHASE_REFILL)) {
        char cb[24];
        snprintf(cb, sizeof(cb), "%d Combo!", s_turn_board.combo_count);
        draw_text_centered(cb, sw * 0.5f, grid_oy() - hud * 1.6f, hud, (Color){ 220, 110, 40, 255 });
    }

    if (s_turn_phase == TURN_PLAN)
        draw_text_centered("Arm actives, then drag ONE orb",
                           sw * 0.5f, grid_oy() - hud * 1.6f, (int)(sw / 28),
                           (Color){ 130, 120, 110, 255 });

    /* active skill buttons */
    int bf = (int)(sw / 40);
    for (int i = 0; i < TEAM_SIZE; i++) {
        Rectangle r = active_btn_rect(i);
        Monster *m = &s_team.m[i];
        bool ready = (m->cd == 0) && (s_turn_phase == TURN_PLAN);
        Color bg = s_armed[i] ? (Color){ 220, 160, 40, 255 }
                              : ready ? BTN_COL : BTN_OFF;
        if (ready && !s_armed[i] && CheckCollisionPointRec(input_pos(), r)) bg = BTN_HOT;
        draw_panel(r, bg);

        char tag = (i == s_team.leader_idx) ? 'L' : ' ';
        char l1[40], l2[40];
        snprintf(l1, sizeof(l1), "%c %s", tag, ACTIVE_DEFS[m->active].name);
        if (s_armed[i])      snprintf(l2, sizeof(l2), "ARMED");
        else if (m->cd > 0)  snprintf(l2, sizeof(l2), "CD %d", m->cd);
        else if (m->active == AS_ELEMENT_LOCK)
                             snprintf(l2, sizeof(l2), "READY %s", ELEM_NAME[m->lock_color]);
        else                 snprintf(l2, sizeof(l2), "READY");
        DrawText(l1, (int)(r.x + 8), (int)(r.y + r.height * 0.12f), bf, RAYWHITE);
        DrawText(l2, (int)(r.x + 8), (int)(r.y + r.height * 0.54f), bf, Fade(RAYWHITE, 0.85f));
    }

    /* result panel */
    if (s_turn_phase == TURN_SCORED) {
        Rectangle p = { sw * 0.12f, sh * 0.30f, sw * 0.76f, sh * 0.27f };
        draw_panel(p, (Color){ 250, 248, 240, 245 });
        DrawRectangleRoundedLines(p, 0.16f, 8, INK_COL);
        int f = (int)(sw / 22);
        char a[48], b[48], d[64];
        int delta = s_last_score - s_last_target;
        snprintf(a, sizeof(a), "Score: %d", s_last_score);
        snprintf(b, sizeof(b), "Target: %d", s_last_target);
        snprintf(d, sizeof(d), "Delta: %+d   ->   +%d pts", delta, s_last_points);
        draw_text_centered(a, sw * 0.5f, p.y + p.height * 0.12f, f, INK_COL);
        draw_text_centered(b, sw * 0.5f, p.y + p.height * 0.35f, f, INK_COL);
        draw_text_centered(d, sw * 0.5f, p.y + p.height * 0.58f, (int)(sw / 26),
                           (Color){ 200, 90, 40, 255 });
        draw_button(next_btn_rect(), s_turn + 1 >= RUN_TURNS ? "See Results" : "Next Turn", true);
    }
}

/* ============================================================ screen: results */
static void draw_results(void)
{
    float sw = GetScreenWidth(), sh = GetScreenHeight();
    draw_text_centered("RUN COMPLETE", sw * 0.5f, sh * 0.08f, (int)(sw / 12), INK_COL);

    char fs[48];
    snprintf(fs, sizeof(fs), "Final Score: %d", s_run_points);
    draw_text_centered(fs, sw * 0.5f, sh * 0.18f, (int)(sw / 14), (Color){ 200, 90, 40, 255 });

    int f = (int)(sw / 24);
    for (int i = 0; i < RUN_TURNS; i++) {
        char line[64];
        snprintf(line, sizeof(line), "Turn %d  (tgt %d):  +%d", i + 1, TARGETS[i], s_turn_pts[i]);
        draw_text_centered(line, sw * 0.5f, sh * (0.32f + i * 0.07f), f, INK_COL);
    }
    draw_button(play_again_rect(), "Play Again", true);
}

/* ============================================================ callbacks */
static void DragDispDraftInit(void)
{
    s_screen     = SCREEN_DRAFT;
    s_pick       = 0;
    s_team       = (Team){ 0 };
    s_team.leader_idx = 0;
    s_prev_tc    = 0;
    s_run_points = 0;
    s_phase      = PHASE_IDLE;
    s_dragging   = false;
    gen_options();
}

static void DragDispDraftUpdate(float dt)
{
    int     tc   = GetTouchPointCount();
    Vector2 ipos = input_pos();

    if (s_screen == SCREEN_DRAFT) {
        for (int i = 0; i < 3; i++)
            if (tapped(draft_card_rect(i))) { apply_choice(i); break; }
    }
    else if (s_screen == SCREEN_ASSIGN) {
        for (int i = 0; i < TEAM_SIZE; i++)
            if (tapped(assign_card_rect(i))) s_team.leader_idx = i;
        if (tapped(assign_start_rect())) start_run();
    }
    else if (s_screen == SCREEN_RUN) {
        if (s_turn_phase == TURN_PLAN) {
            for (int i = 0; i < TEAM_SIZE; i++)
                if (s_team.m[i].cd == 0 && !s_armed[i] && tapped(active_btn_rect(i)))
                    s_armed[i] = true;

            if (s_phase == PHASE_IDLE && input_just_pressed()) {
                int r, c;
                cell_at(ipos, &r, &c);
                if (r >= 0 && s_grid[r][c].alive) {
                    s_held_type        = s_grid[r][c].type;
                    s_held_r           = r;
                    s_held_c           = c;
                    s_held_vis         = s_grid[r][c].vis;
                    s_grid[r][c].alive = false;
                    s_dragging         = true;
                    s_held_moved       = false;
                    s_drag_timer       = DRAG_SECS;
                    s_phase            = PHASE_DRAGGING;
                    s_phase_t          = 0.0f;
                }
            }
        }

        if (s_phase == PHASE_DRAGGING && s_dragging) {
            float k = fminf(LERP_SPD * 2.5f * dt, 1.0f);
            s_held_vis.x += (ipos.x - s_held_vis.x) * k;
            s_held_vis.y += (ipos.y - s_held_vis.y) * k;

            int nr, nc;
            cell_at(ipos, &nr, &nc);
            if (nr >= 0 && (nr != s_held_r || nc != s_held_c)) {
                if (s_grid[nr][nc].alive) {
                    s_grid[s_held_r][s_held_c] = s_grid[nr][nc];
                    s_grid[nr][nc].alive       = false;
                }
                s_held_r     = nr;
                s_held_c     = nc;
                s_held_moved = true;
            }

            s_drag_timer -= dt;
            if (s_drag_timer <= 0.0f || input_just_released()) {
                release_orb();
                if (s_phase != PHASE_IDLE || s_held_moved)
                    s_turn_phase = TURN_RESOLVING;
            }
        }

        if (s_phase == PHASE_FLASH) {
            s_phase_t += dt;
            if (s_phase_t >= FLASH_SECS) { s_phase = PHASE_CLEAR; s_phase_t = 0.0f; }
        }
        if (s_phase == PHASE_CLEAR) {
            s_phase_t += dt;
            float t = fminf(s_phase_t / CLEAR_SECS, 1.0f);
            for (int r = 0; r < ROWS; r++)
                for (int c = 0; c < COLS; c++)
                    if (s_grid[r][c].matched && s_grid[r][c].alive)
                        s_grid[r][c].alpha = 1.0f - t;
            if (s_phase_t >= CLEAR_SECS) {
                for (int r = 0; r < ROWS; r++)
                    for (int c = 0; c < COLS; c++)
                        if (s_grid[r][c].matched) s_grid[r][c].alive = false;
                compact_columns();
                s_phase = PHASE_FALL; s_phase_t = 0.0f;
            }
        }
        if (s_phase == PHASE_FALL) {
            s_phase_t += dt;
            if (s_phase_t >= FALL_SECS) { refill_columns(); s_phase = PHASE_REFILL; s_phase_t = 0.0f; }
        }
        if (s_phase == PHASE_REFILL) {
            s_phase_t += dt;
            if (s_phase_t >= FALL_SECS) {
                int matched = find_matches();
                if (matched > 0) { s_phase = PHASE_FLASH; s_phase_t = 0.0f; }
                else             { s_phase = PHASE_IDLE; }
            }
        }

        if (s_turn_phase == TURN_RESOLVING && s_phase == PHASE_IDLE)
            score_turn();

        if (s_turn_phase == TURN_SCORED && tapped(next_btn_rect())) {
            s_turn++;
            if (s_turn >= RUN_TURNS) s_screen = SCREEN_RESULTS;
            else                     start_turn();
        }

        float k = fminf(LERP_SPD * dt, 1.0f);
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++) {
                if (!s_grid[r][c].alive) continue;
                Vector2 tg = cell_center(r, c);
                s_grid[r][c].vis.x += (tg.x - s_grid[r][c].vis.x) * k;
                s_grid[r][c].vis.y += (tg.y - s_grid[r][c].vis.y) * k;
            }
    }
    else if (s_screen == SCREEN_RESULTS) {
        if (tapped(play_again_rect())) DragDispDraftInit();
    }

    s_prev_tc = tc;
}

static void DragDispDraftDraw(void)
{
    ClearBackground(BG_COL);
    switch (s_screen) {
        case SCREEN_DRAFT:   draw_draft();   break;
        case SCREEN_ASSIGN:  draw_assign();  break;
        case SCREEN_RUN:     draw_run();     break;
        case SCREEN_RESULTS: draw_results(); break;
    }
}

static void DragDispDraftDeinit(void) { /* nothing to free */ }

const Prototype DragDispDraftProto = {
    .name        = "Drag & Displace: Draft",
    .description = "P&D points slice: draft 6 monsters, then hit each turn's target score.",
    .Init        = DragDispDraftInit,
    .Update      = DragDispDraftUpdate,
    .Draw        = DragDispDraftDraw,
    .Deinit      = DragDispDraftDeinit,
};
