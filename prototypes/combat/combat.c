#include "combat.h"

#include "raylib.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ----------------------------------------------------------------- constants */

#define GRID_COLS         6
#define GRID_ROWS         8

#define MAX_UNITS         16
#define MAX_TG_TILES      16

#define TURN_TIMER_SEC    6.0f
#define RESOLVE_SEC       0.7f
#define BETWEEN_SEC       0.4f

#define UNIT_LERP_SPD     16.0f
#define LIFT_PX_FRAC      8       /* picked ally rises by tile/LIFT_PX_FRAC */

/* class HP / damage */
#define HP_DEFENDER       6
#define HP_ARCHER         3
#define HP_MEDIC          4
#define HP_GRUNT          3
#define DMG_GRUNT         2

/* --------------------------------------------------------------------- types */

typedef enum { SIDE_ALLY = 0, SIDE_ENEMY = 1 } Side;
typedef enum { CLS_DEFENDER, CLS_ARCHER, CLS_MEDIC, CLS_GRUNT } UnitClass;

typedef struct {
    bool      alive;
    Side      side;
    UnitClass cls;
    int       col, row;
    float     vx, vy;
    int       hp, max_hp;
    bool      shielded;     /* defender passive flag, set each plan tick */
    float     flash_t;      /* >0 means recently hit (resolve visual) */
} Unit;

typedef enum {
    PHASE_PLAN,
    PHASE_RESOLVE,
    PHASE_BETWEEN,
    PHASE_WIN,
    PHASE_LOSE
} Phase;

/* Pending damage buffer (negative = healing). Indexed by unit index. */
typedef struct {
    int dmg;
    int heal;
    bool will_die;
    bool warning;
} Pending;

/* ----------------------------------------------------------- module statics */

static Unit    s_units[MAX_UNITS];
static int     s_unit_count;

static Phase   s_phase;
static float   s_phase_t;          /* counts down */
static int     s_turn;

static int     s_picked_idx;       /* -1 if none */
static int     s_drag_dc;
static int     s_drag_dr;
static int     s_pickup_col;
static int     s_pickup_row;

static Pending s_pending[MAX_UNITS];

/* enemy tile-locked telegraphs, one per enemy: -1 if none */
static int     s_enemy_tg_col[MAX_UNITS];
static int     s_enemy_tg_row[MAX_UNITS];
static int     s_enemy_tg_dmg[MAX_UNITS];

/* trail tiles the picked ally has visited this drag */
static bool    s_trail[GRID_ROWS][GRID_COLS];

/* input edge state */
static Vector2 s_prev_touch;
static int     s_prev_tc;
static bool    s_mouse_was_down;

/* resolve-phase damage popups */
typedef struct {
    bool  active;
    float t;
    float x, y;
    int   amount;     /* negative = heal */
} Popup;
#define MAX_POPUPS 24
static Popup s_popups[MAX_POPUPS];

/* ---------------------------------------------------------- layout helpers */

static int hud_top_h(void)   { return GetScreenHeight() * 7 / 100; }
static int hud_bot_h(void)   { return GetScreenHeight() * 8 / 100; }

static int tile_px(void)
{
    int sw      = GetScreenWidth();
    int sh      = GetScreenHeight();
    int avail_h = sh - hud_top_h() - hud_bot_h();
    if (avail_h < 1) avail_h = 1;
    int t_w = sw      / GRID_COLS;
    int t_h = avail_h / GRID_ROWS;
    int t   = t_w < t_h ? t_w : t_h;
    return t > 0 ? t : 1;
}

static int grid_x(void) { return (GetScreenWidth() - GRID_COLS * tile_px()) / 2; }

static int grid_y(void)
{
    int sh    = GetScreenHeight();
    int avail = sh - hud_top_h() - hud_bot_h();
    int gh    = GRID_ROWS * tile_px();
    return hud_top_h() + (avail - gh) / 2;
}

static bool screen_to_cell(Vector2 p, int *out_col, int *out_row)
{
    int ts = tile_px();
    int gx = grid_x();
    int gy = grid_y();
    float lx = p.x - (float)gx;
    float ly = p.y - (float)gy;
    if (lx < 0.0f || ly < 0.0f) return false;
    int col = (int)(lx / (float)ts);
    int row = (int)(ly / (float)ts);
    if (col < 0 || col >= GRID_COLS || row < 0 || row >= GRID_ROWS) return false;
    *out_col = col;
    *out_row = row;
    return true;
}

static Vector2 cell_center_px(float cf, float rf)
{
    int ts = tile_px();
    return (Vector2){
        (float)grid_x() + (cf + 0.5f) * (float)ts,
        (float)grid_y() + (rf + 0.5f) * (float)ts,
    };
}

/* ---------------------------------------------------------- unit helpers */

static int unit_at(int col, int row)
{
    for (int i = 0; i < s_unit_count; i++) {
        if (s_units[i].alive && s_units[i].col == col && s_units[i].row == row) return i;
    }
    return -1;
}

static int nearest_live_ally_to(int from_col, int from_row, int *out_col, int *out_row)
{
    int best = -1;
    int best_d2 = 0x7fffffff;
    for (int i = 0; i < s_unit_count; i++) {
        if (!s_units[i].alive || s_units[i].side != SIDE_ALLY) continue;
        int dc = s_units[i].col - from_col;
        int dr = s_units[i].row - from_row;
        int d2 = dc*dc + dr*dr;
        if (d2 < best_d2) { best_d2 = d2; best = i; }
    }
    if (best >= 0) { *out_col = s_units[best].col; *out_row = s_units[best].row; }
    return best;
}

static int add_unit(Side side, UnitClass cls, int col, int row, int hp)
{
    int i = s_unit_count++;
    s_units[i].alive    = true;
    s_units[i].side     = side;
    s_units[i].cls      = cls;
    s_units[i].col      = col;
    s_units[i].row      = row;
    s_units[i].vx       = (float)col;
    s_units[i].vy       = (float)row;
    s_units[i].hp       = hp;
    s_units[i].max_hp   = hp;
    s_units[i].shielded = false;
    s_units[i].flash_t  = 0.0f;
    return i;
}

/* ---------------------------------------------------------- telegraphs */

/* Archer passive: find nearest enemy in same row or column, LOS-blocked
 * by any unit between. Returns enemy index or -1. */
static int archer_passive_target(int archer_idx)
{
    int ac = s_units[archer_idx].col;
    int ar = s_units[archer_idx].row;
    int best = -1, best_d = 0x7fffffff;

    /* same row, scan left then right */
    for (int dir = -1; dir <= 1; dir += 2) {
        for (int c = ac + dir; c >= 0 && c < GRID_COLS; c += dir) {
            int u = unit_at(c, ar);
            if (u < 0) continue;
            if (s_units[u].side == SIDE_ENEMY) {
                int d = (c > ac) ? c - ac : ac - c;
                if (d < best_d) { best_d = d; best = u; }
            }
            break; /* unit blocks LOS either way */
        }
    }
    /* same col, scan up then down */
    for (int dir = -1; dir <= 1; dir += 2) {
        for (int r = ar + dir; r >= 0 && r < GRID_ROWS; r += dir) {
            int u = unit_at(ac, r);
            if (u < 0) continue;
            if (s_units[u].side == SIDE_ENEMY) {
                int d = (r > ar) ? r - ar : ar - r;
                if (d < best_d) { best_d = d; best = u; }
            }
            break;
        }
    }
    return best;
}

/* Defender passive: shield self + 4-orth allies. Caller responsible for
 * checking that defender isn't the picked ally. Sets shielded flag on units. */
static void apply_defender_shield(int def_idx)
{
    int dc = s_units[def_idx].col;
    int dr = s_units[def_idx].row;
    int N = 5;
    int dx[5] = { 0,  0,  0, -1,  1 };
    int dy[5] = { 0, -1,  1,  0,  0 };
    for (int k = 0; k < N; k++) {
        int c = dc + dx[k];
        int r = dr + dy[k];
        if (c < 0 || c >= GRID_COLS || r < 0 || r >= GRID_ROWS) continue;
        int u = unit_at(c, r);
        if (u >= 0 && s_units[u].side == SIDE_ALLY) s_units[u].shielded = true;
    }
}

/* Active intent footprints: fill out_tiles[] with (col,row) pairs.
 * Returns tile count. dc/dr are last drag-direction; default (0,-1) is up. */
static int defender_active_tiles(int idx, int dc, int dr, int out[][2])
{
    int cc = s_units[idx].col;
    int rr = s_units[idx].row;
    if (dc == 0 && dr == 0) { dc = 0; dr = -1; }
    int n = 0;
    /* T-shape: 1 step forward + 2 flanks at +1 */
    int fc = cc + dc;
    int fr = rr + dr;
    if (fc >= 0 && fc < GRID_COLS && fr >= 0 && fr < GRID_ROWS) {
        out[n][0] = fc; out[n][1] = fr; n++;
    }
    /* flanks: perpendicular to (dc,dr) at distance 1 from defender */
    int pdc = -dr, pdr = dc;
    int lc = cc + pdc, lr = rr + pdr;
    int rc = cc - pdc, rrw = rr - pdr;
    if (lc >= 0 && lc < GRID_COLS && lr >= 0 && lr < GRID_ROWS) {
        out[n][0] = lc; out[n][1] = lr; n++;
    }
    if (rc >= 0 && rc < GRID_COLS && rrw >= 0 && rrw < GRID_ROWS) {
        out[n][0] = rc; out[n][1] = rrw; n++;
    }
    return n;
}

static int archer_active_tiles(int idx, int out[][2])
{
    int cc = s_units[idx].col;
    int n = 0;
    for (int r = 0; r < GRID_ROWS; r++) {
        if (r == s_units[idx].row) continue;
        out[n][0] = cc; out[n][1] = r; n++;
    }
    return n;
}

static int medic_active_tiles(int idx, int out[][2])
{
    int cc = s_units[idx].col;
    int rr = s_units[idx].row;
    int n = 0;
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            int c = cc + dc, r = rr + dr;
            if (c < 0 || c >= GRID_COLS || r < 0 || r >= GRID_ROWS) continue;
            out[n][0] = c; out[n][1] = r; n++;
        }
    }
    return n;
}

static int defender_passive_tiles(int idx, int out[][2])
{
    int cc = s_units[idx].col;
    int rr = s_units[idx].row;
    int dx[5] = { 0,  0,  0, -1,  1 };
    int dy[5] = { 0, -1,  1,  0,  0 };
    int n = 0;
    for (int k = 0; k < 5; k++) {
        int c = cc + dx[k], r = rr + dy[k];
        if (c < 0 || c >= GRID_COLS || r < 0 || r >= GRID_ROWS) continue;
        out[n][0] = c; out[n][1] = r; n++;
    }
    return n;
}

static int medic_passive_tiles(int idx, int out[][2])
{
    return defender_passive_tiles(idx, out);
}

/* For enemy: refresh tile-locked telegraphs based on current board state.
 * Called once at start of each plan phase. */
static void refresh_enemy_telegraphs(void)
{
    for (int i = 0; i < s_unit_count; i++) {
        s_enemy_tg_col[i] = -1;
        s_enemy_tg_row[i] = -1;
        s_enemy_tg_dmg[i] = 0;
        if (!s_units[i].alive || s_units[i].side != SIDE_ENEMY) continue;
        if (s_units[i].cls == CLS_GRUNT) {
            int tc, tr;
            int tgt = nearest_live_ally_to(s_units[i].col, s_units[i].row, &tc, &tr);
            if (tgt >= 0) {
                s_enemy_tg_col[i] = tc;
                s_enemy_tg_row[i] = tr;
                s_enemy_tg_dmg[i] = DMG_GRUNT;
            }
        }
    }
}

/* ------------------------------------------------------ pending damage sim */

static void recompute_pending(void)
{
    for (int i = 0; i < MAX_UNITS; i++) {
        s_pending[i].dmg      = 0;
        s_pending[i].heal     = 0;
        s_pending[i].will_die = false;
        s_pending[i].warning  = false;
    }

    /* 1) defender-shield set (non-picked defenders) */
    for (int i = 0; i < s_unit_count; i++) s_units[i].shielded = false;
    for (int i = 0; i < s_unit_count; i++) {
        if (!s_units[i].alive || s_units[i].side != SIDE_ALLY) continue;
        if (s_units[i].cls != CLS_DEFENDER) continue;
        if (i == s_picked_idx) continue;
        apply_defender_shield(i);
    }

    /* 2) enemy attacks (tile-locked) */
    for (int i = 0; i < s_unit_count; i++) {
        if (s_enemy_tg_col[i] < 0) continue;
        int t = unit_at(s_enemy_tg_col[i], s_enemy_tg_row[i]);
        if (t < 0) continue;
        if (s_units[t].side != SIDE_ALLY) continue;
        int d = s_enemy_tg_dmg[i];
        if (s_units[t].shielded) { d = d / 2; if (d < 1) d = 1; }
        s_pending[t].dmg += d;
    }

    /* 3) non-picked ally passives */
    int tiles[MAX_TG_TILES][2];
    for (int i = 0; i < s_unit_count; i++) {
        if (!s_units[i].alive || s_units[i].side != SIDE_ALLY) continue;
        if (i == s_picked_idx) continue;
        if (s_units[i].cls == CLS_ARCHER) {
            int tgt = archer_passive_target(i);
            if (tgt >= 0) s_pending[tgt].dmg += 1;
        } else if (s_units[i].cls == CLS_MEDIC) {
            int n = medic_passive_tiles(i, tiles);
            for (int k = 0; k < n; k++) {
                int u = unit_at(tiles[k][0], tiles[k][1]);
                if (u >= 0 && s_units[u].side == SIDE_ALLY) s_pending[u].heal += 1;
            }
        }
        /* defender passive handled in step 1 */
    }

    /* 4) picked ally active */
    if (s_picked_idx >= 0) {
        int p = s_picked_idx;
        int n = 0;
        if (s_units[p].cls == CLS_DEFENDER) {
            n = defender_active_tiles(p, s_drag_dc, s_drag_dr, tiles);
            for (int k = 0; k < n; k++) {
                int u = unit_at(tiles[k][0], tiles[k][1]);
                if (u >= 0 && s_units[u].side == SIDE_ENEMY) s_pending[u].dmg += 2;
            }
        } else if (s_units[p].cls == CLS_ARCHER) {
            n = archer_active_tiles(p, tiles);
            for (int k = 0; k < n; k++) {
                int u = unit_at(tiles[k][0], tiles[k][1]);
                if (u >= 0 && s_units[u].side == SIDE_ENEMY) s_pending[u].dmg += 2;
            }
        } else if (s_units[p].cls == CLS_MEDIC) {
            n = medic_active_tiles(p, tiles);
            for (int k = 0; k < n; k++) {
                int u = unit_at(tiles[k][0], tiles[k][1]);
                if (u >= 0 && s_units[u].side == SIDE_ALLY) s_pending[u].heal += 2;
            }
        }
    }

    /* 5) flag death / warning */
    for (int i = 0; i < s_unit_count; i++) {
        if (!s_units[i].alive) continue;
        int net = s_units[i].hp - s_pending[i].dmg + s_pending[i].heal;
        if (net > s_units[i].max_hp) net = s_units[i].max_hp;
        if (net <= 0) s_pending[i].will_die = true;
        else if (s_pending[i].dmg > s_units[i].hp / 2 && s_pending[i].dmg > 0)
            s_pending[i].warning = true;
    }
}

/* --------------------------------------------------------- resolve a turn */

static void spawn_popup(float x, float y, int amount)
{
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (!s_popups[i].active) {
            s_popups[i].active = true;
            s_popups[i].t = 0.6f;
            s_popups[i].x = x;
            s_popups[i].y = y;
            s_popups[i].amount = amount;
            return;
        }
    }
}

static void resolve_turn(void)
{
    /* run pending recompute one more time to capture final state */
    recompute_pending();

    /* commit damage / healing */
    for (int i = 0; i < s_unit_count; i++) {
        if (!s_units[i].alive) continue;
        int delta = s_pending[i].heal - s_pending[i].dmg;
        if (delta == 0) continue;
        s_units[i].hp += delta;
        if (s_units[i].hp > s_units[i].max_hp) s_units[i].hp = s_units[i].max_hp;
        if (s_units[i].hp < 0) s_units[i].hp = 0;
        s_units[i].flash_t = 0.5f;
        Vector2 p = cell_center_px((float)s_units[i].col, (float)s_units[i].row);
        spawn_popup(p.x, p.y, delta);
        if (s_units[i].hp <= 0) s_units[i].alive = false;
    }

    /* win/lose check */
    bool any_ally = false, any_enemy = false;
    for (int i = 0; i < s_unit_count; i++) {
        if (!s_units[i].alive) continue;
        if (s_units[i].side == SIDE_ALLY)  any_ally  = true;
        if (s_units[i].side == SIDE_ENEMY) any_enemy = true;
    }
    if (!any_enemy)      { s_phase = PHASE_WIN;   s_phase_t = 0.0f; }
    else if (!any_ally)  { s_phase = PHASE_LOSE;  s_phase_t = 0.0f; }
    else                 { s_phase = PHASE_RESOLVE; s_phase_t = RESOLVE_SEC; }
}

static void start_plan_phase(void)
{
    s_picked_idx = -1;
    s_drag_dc    = 0;
    s_drag_dr    = -1;
    memset(s_trail, 0, sizeof(s_trail));
    s_phase = PHASE_PLAN;
    s_phase_t = TURN_TIMER_SEC;
    s_turn++;
    refresh_enemy_telegraphs();
    recompute_pending();
}

/* ---------------------------------------------------------------- input */

static void on_down(Vector2 pos)
{
    if (s_phase != PHASE_PLAN) return;
    int c, r;
    if (!screen_to_cell(pos, &c, &r)) return;
    int u = unit_at(c, r);
    if (u < 0 || s_units[u].side != SIDE_ALLY) return;
    s_picked_idx = u;
    s_pickup_col = c;
    s_pickup_row = r;
    s_drag_dc    = 0;
    s_drag_dr    = -1;
    memset(s_trail, 0, sizeof(s_trail));
    s_trail[r][c] = true;
    recompute_pending();
}

static void on_move(Vector2 pos)
{
    if (s_phase != PHASE_PLAN) return;
    if (s_picked_idx < 0) return;
    int c, r;
    if (!screen_to_cell(pos, &c, &r)) return;
    int pc = s_units[s_picked_idx].col;
    int pr = s_units[s_picked_idx].row;
    if (c == pc && r == pr) return;

    /* only orthogonal-adjacent steps */
    int dc = c - pc, dr = r - pr;
    int adc = dc < 0 ? -dc : dc;
    int adr = dr < 0 ? -dr : dr;
    if (adc + adr != 1) return;

    int u = unit_at(c, r);
    if (u >= 0 && s_units[u].side == SIDE_ENEMY) return; /* blocked */

    /* swap */
    if (u >= 0) {
        s_units[u].col = pc;
        s_units[u].row = pr;
    }
    s_units[s_picked_idx].col = c;
    s_units[s_picked_idx].row = r;
    s_drag_dc = dc;
    s_drag_dr = dr;
    s_trail[r][c] = true;

    recompute_pending();
}

static void on_up(Vector2 pos)
{
    (void)pos;
    if (s_phase != PHASE_PLAN) return;
    if (s_picked_idx < 0) return;     /* stray tap on empty tile — ignore */
    resolve_turn();
}

/* ------------------------------------------------------ board setup */

static void setup_board(void)
{
    s_unit_count = 0;
    memset(s_units, 0, sizeof(s_units));
    /* allies */
    add_unit(SIDE_ALLY, CLS_DEFENDER, 2, 6, HP_DEFENDER);
    add_unit(SIDE_ALLY, CLS_MEDIC,    3, 6, HP_MEDIC);
    add_unit(SIDE_ALLY, CLS_ARCHER,   3, 7, HP_ARCHER);
    /* enemies */
    add_unit(SIDE_ENEMY, CLS_GRUNT, 1, 1, HP_GRUNT);
    add_unit(SIDE_ENEMY, CLS_GRUNT, 4, 1, HP_GRUNT);
    add_unit(SIDE_ENEMY, CLS_GRUNT, 2, 2, HP_GRUNT);
}

/* ===================================================== Prototype interface */

static void CombatInit(void)
{
    setup_board();
    for (int i = 0; i < MAX_UNITS; i++) {
        s_enemy_tg_col[i] = -1;
        s_enemy_tg_row[i] = -1;
        s_enemy_tg_dmg[i] = 0;
    }
    for (int i = 0; i < MAX_POPUPS; i++) s_popups[i].active = false;
    s_turn = 0;
    s_prev_tc = 0;
    s_prev_touch = (Vector2){ -1.0f, -1.0f };
    s_mouse_was_down = false;
    start_plan_phase();
}

static void CombatUpdate(float dt)
{
    /* input dispatch — mirror realm_walk */
    int tc = GetTouchPointCount();
    Vector2 tp = (tc > 0) ? GetTouchPosition(0) : (Vector2){ -1.0f, -1.0f };

    bool handled = false;
    if (tc > 0 && s_prev_tc == 0)      { on_down(tp);         handled = true; }
    else if (tc > 0 && s_prev_tc > 0)  { on_move(tp);         handled = true; }
    else if (tc == 0 && s_prev_tc > 0) { on_up(s_prev_touch); handled = true; }
    if (tc > 0) s_prev_touch = tp;
    s_prev_tc = tc;

    if (!handled) {
        Vector2 mp = GetMousePosition();
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))       { on_down(mp); s_mouse_was_down = true; }
        else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))     { on_move(mp); s_mouse_was_down = true; }
        else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { on_up(mp);   s_mouse_was_down = false; }
    }

    /* phase timers */
    if (s_phase == PHASE_PLAN) {
        s_phase_t -= dt;
        if (s_phase_t <= 0.0f) {
            s_phase_t = 0.0f;
            resolve_turn();
        }
    } else if (s_phase == PHASE_RESOLVE) {
        s_phase_t -= dt;
        if (s_phase_t <= 0.0f) { s_phase = PHASE_BETWEEN; s_phase_t = BETWEEN_SEC; }
    } else if (s_phase == PHASE_BETWEEN) {
        s_phase_t -= dt;
        if (s_phase_t <= 0.0f) start_plan_phase();
    }

    /* unit position lerp */
    float k = fminf(UNIT_LERP_SPD * dt, 1.0f);
    for (int i = 0; i < s_unit_count; i++) {
        s_units[i].vx += ((float)s_units[i].col - s_units[i].vx) * k;
        s_units[i].vy += ((float)s_units[i].row - s_units[i].vy) * k;
        if (s_units[i].flash_t > 0.0f) {
            s_units[i].flash_t -= dt;
            if (s_units[i].flash_t < 0.0f) s_units[i].flash_t = 0.0f;
        }
    }

    /* popup timers */
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (s_popups[i].active) {
            s_popups[i].t -= dt;
            s_popups[i].y -= 36.0f * dt;
            if (s_popups[i].t <= 0.0f) s_popups[i].active = false;
        }
    }
}

/* ---------------------------------------------------------------- drawing */

static Color class_color(UnitClass c, Side s)
{
    if (s == SIDE_ENEMY) return (Color){ 200, 60, 60, 255 };
    switch (c) {
        case CLS_DEFENDER: return (Color){  70, 130, 200, 255 };
        case CLS_ARCHER:   return (Color){  90, 180, 110, 255 };
        case CLS_MEDIC:    return (Color){ 230, 230, 230, 255 };
        default:           return GRAY;
    }
}

static const char *class_glyph(UnitClass c)
{
    switch (c) {
        case CLS_DEFENDER: return "D";
        case CLS_ARCHER:   return "A";
        case CLS_MEDIC:    return "+";
        case CLS_GRUNT:    return "g";
    }
    return "?";
}

static void draw_skull_icon(int cx, int cy, int size)
{
    DrawCircle(cx, cy, (float)size * 0.55f, (Color){ 20, 20, 20, 230 });
    DrawCircleLines(cx, cy, (float)size * 0.55f, WHITE);
    int fs = size;
    if (fs < 12) fs = 12;
    int w = MeasureText("X", fs);
    DrawText("X", cx - w / 2, cy - fs / 2, fs, WHITE);
}

static void draw_warning_icon(int cx, int cy, int size)
{
    Vector2 t = { (float)cx,             (float)cy - size * 0.65f };
    Vector2 l = { (float)cx - size*0.65f,(float)cy + size * 0.45f };
    Vector2 r = { (float)cx + size*0.65f,(float)cy + size * 0.45f };
    DrawTriangle(t, l, r, (Color){ 240, 200, 50, 230 });
    DrawTriangleLines(t, l, r, (Color){ 60, 40, 0, 255 });
    int fs = size;
    if (fs < 12) fs = 12;
    int w = MeasureText("!", fs);
    DrawText("!", cx - w / 2, cy - fs / 2, fs, (Color){ 40, 30, 0, 255 });
}

static void draw_tile_overlay(int col, int row, Color c)
{
    int ts = tile_px();
    int x = grid_x() + col * ts;
    int y = grid_y() + row * ts;
    DrawRectangle(x + 2, y + 2, ts - 4, ts - 4, c);
}

static void draw_unit(int idx)
{
    Unit *u = &s_units[idx];
    if (!u->alive) return;
    int ts = tile_px();
    Vector2 p = cell_center_px(u->vx, u->vy);

    float lift = 0.0f;
    if (idx == s_picked_idx) lift = (float)ts / (float)LIFT_PX_FRAC;
    p.y -= lift;

    Color col = class_color(u->cls, u->side);
    if (u->flash_t > 0.0f) {
        float a = u->flash_t / 0.5f;
        col.r = (unsigned char)(col.r + (255 - col.r) * a);
        col.g = (unsigned char)(col.g + (255 - col.g) * a);
        col.b = (unsigned char)(col.b + (255 - col.b) * a);
    }

    float radius = ts * 0.38f;
    if (u->cls == CLS_DEFENDER) {
        DrawRectangle((int)(p.x - radius), (int)(p.y - radius),
                      (int)(radius * 2), (int)(radius * 2), col);
        DrawRectangleLines((int)(p.x - radius), (int)(p.y - radius),
                           (int)(radius * 2), (int)(radius * 2), BLACK);
    } else if (u->cls == CLS_ARCHER) {
        Vector2 a = { p.x,          p.y - radius };
        Vector2 b = { p.x - radius, p.y + radius };
        Vector2 c = { p.x + radius, p.y + radius };
        DrawTriangle(a, b, c, col);
        DrawTriangleLines(a, b, c, BLACK);
    } else if (u->cls == CLS_GRUNT) {
        Vector2 a = { p.x,          p.y + radius };
        Vector2 b = { p.x - radius, p.y - radius };
        Vector2 c = { p.x + radius, p.y - radius };
        DrawTriangle(a, b, c, col);
        DrawTriangleLines(a, b, c, BLACK);
    } else {
        DrawCircleV(p, radius, col);
        DrawCircleLines((int)p.x, (int)p.y, radius, BLACK);
    }

    /* glyph */
    int fs = (int)(ts * 0.30f);
    if (fs < 12) fs = 12;
    Color glyph_col = (u->cls == CLS_MEDIC) ? RED : WHITE;
    int gw = MeasureText(class_glyph(u->cls), fs);
    DrawText(class_glyph(u->cls), (int)(p.x - gw / 2), (int)(p.y - fs / 2),
             fs, glyph_col);

    /* HP pips beneath the unit */
    int pip_r = ts / 18;
    if (pip_r < 2) pip_r = 2;
    int pip_gap = pip_r * 2 + 2;
    int total_w = pip_gap * u->max_hp - 2;
    int start_x = (int)(p.x - total_w / 2);
    int pip_y   = (int)(p.y + radius + pip_r + 4);
    for (int i = 0; i < u->max_hp; i++) {
        Color pc = (i < u->hp) ? (Color){ 90, 220, 90, 255 } : (Color){ 60, 60, 60, 200 };
        if (u->side == SIDE_ENEMY && i < u->hp) pc = (Color){ 230, 100, 100, 255 };
        DrawCircle(start_x + pip_gap * i + pip_r, pip_y, (float)pip_r, pc);
    }

    /* defender shield indicator (passive recipient) */
    if (u->side == SIDE_ALLY && u->shielded && idx != s_picked_idx) {
        DrawCircleLines((int)p.x, (int)p.y, radius + 6.0f, (Color){ 120, 180, 240, 200 });
        DrawCircleLines((int)p.x, (int)p.y, radius + 8.0f, (Color){ 120, 180, 240, 120 });
    }

    /* skull / warning icon */
    if (s_phase == PHASE_PLAN) {
        int icon_size = (int)(ts * 0.18f);
        if (icon_size < 10) icon_size = 10;
        int ix = (int)p.x + (int)radius - icon_size / 2;
        int iy = (int)p.y - (int)radius - icon_size / 2;
        if (s_pending[idx].will_die)      draw_skull_icon(ix, iy, icon_size);
        else if (s_pending[idx].warning)  draw_warning_icon(ix, iy, icon_size);
    }
}

static void draw_telegraphs(void)
{
    int tiles[MAX_TG_TILES][2];

    /* enemy attacks (tile-locked) — red */
    for (int i = 0; i < s_unit_count; i++) {
        if (s_enemy_tg_col[i] < 0) continue;
        draw_tile_overlay(s_enemy_tg_col[i], s_enemy_tg_row[i],
                          (Color){ 200, 60, 60, 90 });
    }

    /* ally passives — blue tint (skipped for picked) */
    for (int i = 0; i < s_unit_count; i++) {
        if (!s_units[i].alive || s_units[i].side != SIDE_ALLY) continue;
        if (i == s_picked_idx) continue;
        int n = 0;
        Color c = (Color){ 80, 150, 220, 70 };
        if (s_units[i].cls == CLS_DEFENDER) {
            n = defender_passive_tiles(i, tiles);
            c = (Color){ 120, 170, 230, 60 };
        } else if (s_units[i].cls == CLS_MEDIC) {
            n = medic_passive_tiles(i, tiles);
            c = (Color){ 220, 220, 230, 70 };
        } else if (s_units[i].cls == CLS_ARCHER) {
            int tgt = archer_passive_target(i);
            if (tgt >= 0) {
                tiles[0][0] = s_units[tgt].col;
                tiles[0][1] = s_units[tgt].row;
                n = 1;
                c = (Color){ 110, 200, 130, 90 };
            }
        }
        for (int k = 0; k < n; k++) draw_tile_overlay(tiles[k][0], tiles[k][1], c);
    }

    /* picked ally active — yellow */
    if (s_picked_idx >= 0) {
        int p = s_picked_idx;
        int n = 0;
        if (s_units[p].cls == CLS_DEFENDER)
            n = defender_active_tiles(p, s_drag_dc, s_drag_dr, tiles);
        else if (s_units[p].cls == CLS_ARCHER)
            n = archer_active_tiles(p, tiles);
        else if (s_units[p].cls == CLS_MEDIC)
            n = medic_active_tiles(p, tiles);
        Color ac = (s_units[p].cls == CLS_MEDIC)
                 ? (Color){ 240, 240, 200, 110 }
                 : (Color){ 240, 200, 60, 110 };
        for (int k = 0; k < n; k++) draw_tile_overlay(tiles[k][0], tiles[k][1], ac);
    }
}

static void draw_grid_and_trail(void)
{
    int ts = tile_px();
    int gx = grid_x();
    int gy = grid_y();
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            Color base = ((c + r) & 1) ? (Color){ 40, 44, 54, 255 }
                                        : (Color){ 50, 56, 68, 255 };
            DrawRectangle(gx + c * ts, gy + r * ts, ts, ts, base);
            if (s_trail[r][c] && s_picked_idx >= 0)
                DrawRectangle(gx + c * ts + 2, gy + r * ts + 2,
                              ts - 4, ts - 4, (Color){ 80, 160, 220, 50 });
        }
    }
    /* outer border */
    DrawRectangleLines(gx, gy, GRID_COLS * ts, GRID_ROWS * ts,
                       (Color){ 90, 100, 120, 200 });
}

static void draw_hud_top(void)
{
    int sw = GetScreenWidth();
    int h  = hud_top_h();
    DrawRectangle(0, 0, sw, h, (Color){ 20, 22, 28, 230 });
    int fs = h * 50 / 100;
    if (fs < 14) fs = 14;
    const char *title = "COMBAT — Grunt Patrol";
    int tw = MeasureText(title, fs);
    DrawText(title, (sw - tw) / 2, (h - fs) / 2, fs, (Color){ 220, 220, 230, 255 });

    char buf[32];
    snprintf(buf, sizeof(buf), "Turn %d", s_turn);
    int bw = MeasureText(buf, fs);
    DrawText(buf, sw - bw - 12, (h - fs) / 2, fs, (Color){ 180, 180, 200, 255 });
}

static void draw_hud_bot(void)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int h  = hud_bot_h();
    int y0 = sh - h;
    DrawRectangle(0, y0, sw, h, (Color){ 20, 22, 28, 230 });

    int bar_pad  = sw / 18;
    int bar_h    = h * 40 / 100;
    int bar_y    = y0 + (h - bar_h) / 2;
    int bar_w    = sw - bar_pad * 2;
    DrawRectangle(bar_pad, bar_y, bar_w, bar_h, (Color){ 50, 52, 60, 255 });

    float frac = 0.0f;
    if (s_phase == PHASE_PLAN) frac = s_phase_t / TURN_TIMER_SEC;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    int fill_w = (int)(bar_w * frac);

    Color bar_col = (s_phase_t < 2.0f && s_phase == PHASE_PLAN)
                  ? (Color){ 230, 80, 80, 255 }
                  : (Color){ 120, 200, 120, 255 };
    DrawRectangle(bar_pad, bar_y, fill_w, bar_h, bar_col);
    DrawRectangleLines(bar_pad, bar_y, bar_w, bar_h, (Color){ 90, 100, 120, 255 });

    int fs = bar_h * 70 / 100;
    if (fs < 12) fs = 12;
    char buf[24];
    if (s_phase == PHASE_PLAN)        snprintf(buf, sizeof(buf), "%.1fs", s_phase_t);
    else if (s_phase == PHASE_RESOLVE) snprintf(buf, sizeof(buf), "RESOLVE");
    else if (s_phase == PHASE_BETWEEN) snprintf(buf, sizeof(buf), "...");
    else if (s_phase == PHASE_WIN)     snprintf(buf, sizeof(buf), "VICTORY");
    else                               snprintf(buf, sizeof(buf), "DEFEAT");
    int tw = MeasureText(buf, fs);
    DrawText(buf, (sw - tw) / 2, bar_y + (bar_h - fs) / 2, fs, WHITE);
}

static void draw_popups(void)
{
    int fs = tile_px() * 28 / 100;
    if (fs < 14) fs = 14;
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (!s_popups[i].active) continue;
        float a = s_popups[i].t / 0.6f;
        if (a < 0.0f) a = 0.0f;
        if (a > 1.0f) a = 1.0f;
        char buf[16];
        Color c;
        if (s_popups[i].amount < 0) {
            snprintf(buf, sizeof(buf), "+%d", -s_popups[i].amount);
            c = (Color){ 120, 220, 120, (unsigned char)(255 * a) };
        } else {
            snprintf(buf, sizeof(buf), "-%d", s_popups[i].amount);
            c = (Color){ 240, 100, 100, (unsigned char)(255 * a) };
        }
        int w = MeasureText(buf, fs);
        DrawText(buf, (int)(s_popups[i].x - w / 2), (int)s_popups[i].y, fs, c);
    }
}

static void draw_overlay_banner(const char *txt, Color tint)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, (Color){ 0, 0, 0, 140 });
    int fs = sh / 14;
    int tw = MeasureText(txt, fs);
    int x = (sw - tw) / 2;
    int y = (sh - fs) / 2;
    DrawRectangle(x - 30, y - 20, tw + 60, fs + 40, (Color){ 20, 24, 32, 220 });
    DrawRectangleLines(x - 30, y - 20, tw + 60, fs + 40, tint);
    DrawText(txt, x, y, fs, tint);

    int sfs = fs / 3;
    const char *sub = "tap X to return to menu";
    int sw2 = MeasureText(sub, sfs);
    DrawText(sub, (sw - sw2) / 2, y + fs + 8, sfs, (Color){ 200, 200, 210, 220 });
}

static void CombatDraw(void)
{
    ClearBackground((Color){ 18, 20, 26, 255 });

    draw_grid_and_trail();
    draw_telegraphs();

    /* draw units in stable order (kept simple, no Y-sort) */
    for (int i = 0; i < s_unit_count; i++) draw_unit(i);

    draw_popups();
    draw_hud_top();
    draw_hud_bot();

    if (s_phase == PHASE_WIN)  draw_overlay_banner("VICTORY", (Color){ 120, 220, 120, 255 });
    if (s_phase == PHASE_LOSE) draw_overlay_banner("DEFEAT",  (Color){ 230,  90,  90, 255 });
}

static void CombatDeinit(void)
{
    /* nothing to free */
}

const Prototype CombatProto = {
    .name        = "Combat",
    .description = "6x8 grid tactics — drag one ally per turn.",
    .Init        = CombatInit,
    .Update      = CombatUpdate,
    .Draw        = CombatDraw,
    .Deinit      = CombatDeinit,
};
