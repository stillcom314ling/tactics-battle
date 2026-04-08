#include "realm_walk.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- constants */

#define MAP_COLS        30
#define MAP_ROWS        20
#define SCROLL_DEAD_ZONE 2      /* hero cells from vp center before scroll */
#define TURN_SECS        7.0f
#define MAX_TRAIL       (MAP_COLS + MAP_ROWS)
#define MAX_PENDING      50
#define MOVE_COOLDOWN    0.12f  /* seconds between hero steps while dragging */
#define DRAWER_SPEED     8.0f   /* animation fraction per second */
#define SCAN_FLASH_SECS  0.60f
#define ACTION_COUNT     4
#define MAX_FLASH_CELLS  (MAP_COLS * MAP_ROWS)

/* ------------------------------------------------------------------- types */

typedef enum {
    TERRAIN_PLAINS = 0,
    TERRAIN_FOREST,
    TERRAIN_MOUNTAIN,
    TERRAIN_CITY,
    TERRAIN_WATER,
    TERRAIN_COUNT
} Terrain;

typedef enum {
    RES_GOLD = 0,
    RES_FAITH,
    RES_WAR,
    RES_REPUTATION,
    RES_COUNT
} Resource;

typedef enum { PHASE_IDLE, PHASE_DRAGGING, PHASE_SCANNING } Phase;
typedef enum { DRAWER_CLOSED, DRAWER_OPEN }                  DrawerState;

typedef struct { Resource resource; int amount; } Connection;

typedef struct {
    const char *name;
    Resource    cost_resource;
    int         cost_amount;
} KingdomAction;

/* ------------------------------------------------------------ module state */

static Terrain s_map[MAP_ROWS][MAP_COLS];

static int s_hero_col, s_hero_row;
static int s_vp_col,   s_vp_row;
static int s_vp_col_prev, s_vp_row_prev;  /* previous vp, for scroll compensation */

/* drag / turn */
static bool  s_is_dragging;
static Phase s_phase;
static float s_turn_timer;
static int   s_trail_col[MAX_TRAIL];
static int   s_trail_row[MAX_TRAIL];
static int   s_trail_len;

/* scan tracking */
static bool s_col_visited[MAP_COLS];
static bool s_row_visited[MAP_ROWS];
static int  s_scanned_cols[MAP_COLS];
static int  s_scanned_rows[MAP_ROWS];
static int  s_scanned_col_count;
static int  s_scanned_row_count;

/* resources */
static int        s_resources[RES_COUNT];
static Connection s_pending[MAX_PENDING];
static int        s_pending_count;
static int        s_action_counts[ACTION_COUNT];

/* UI */
static DrawerState s_drawer;
static float       s_drawer_t;   /* 0 = closed, 1 = open */
static float       s_scan_flash; /* counts down from SCAN_FLASH_SECS */

/* matched-cell flash positions — populated during scan, drawn during PHASE_SCANNING */
static int s_flash_col[MAX_FLASH_CELLS];
static int s_flash_row[MAX_FLASH_CELLS];
static int s_flash_count;

/* movement pacing */
static float s_move_cd;

/* touch */
static int     s_prev_tc;
static Vector2 s_prev_touch;

/* -------------------------------------------------------------- data tables */

static const Color TERRAIN_COLOR[TERRAIN_COUNT] = {
    { 180, 220, 100, 255 }, /* PLAINS   – olive green */
    {  34, 120,  50, 255 }, /* FOREST   – dark green  */
    { 130, 130, 140, 255 }, /* MOUNTAIN – stone grey  */
    { 200, 160,  80, 255 }, /* CITY     – amber       */
    {  60, 130, 200, 255 }, /* WATER    – blue        */
};

static const char TERRAIN_LETTER[TERRAIN_COUNT] = { 'P', 'F', 'M', 'C', 'W' };

static const Color RES_COLOR[RES_COUNT] = {
    { 255, 215,   0, 255 }, /* GOLD       – yellow     */
    { 180, 180, 255, 255 }, /* FAITH      – lavender   */
    { 220,  60,  60, 255 }, /* WAR        – red        */
    {  60, 220, 220, 255 }, /* REPUTATION – cyan       */
};

static const char *RES_NAME[RES_COUNT] = { "Gold", "Faith", "War", "Rep" };

static const Resource TERRAIN_RESOURCE[TERRAIN_COUNT] = {
    RES_GOLD,        /* PLAINS   */
    RES_FAITH,       /* FOREST   */
    RES_WAR,         /* MOUNTAIN */
    RES_REPUTATION,  /* CITY     */
    RES_GOLD,        /* WATER    */
};

static const KingdomAction ACTIONS[ACTION_COUNT] = {
    { "Build Market",  RES_GOLD,        5 },
    { "Sign Treaty",   RES_REPUTATION,  4 },
    { "Raise Army",    RES_WAR,         6 },
    { "Build Shrine",  RES_FAITH,       3 },
};

/* ---------------------------------------------------------- layout helpers */

static int tile_px(void)
{
    int t = GetScreenWidth() / 8;
    return (t < 40) ? 40 : t;
}

static int vis_cols(void) { return GetScreenWidth()  / tile_px() + 1; }
static int vis_rows(void) { return GetScreenHeight() / tile_px() + 1; }

static bool screen_to_tile(Vector2 p, int *out_col, int *out_row)
{
    int ts  = tile_px();
    int col = s_vp_col + (int)(p.x / (float)ts);
    int row = s_vp_row + (int)(p.y / (float)ts);
    if (col < 0 || col >= MAP_COLS || row < 0 || row >= MAP_ROWS) {
        *out_col = -1; *out_row = -1;
        return false;
    }
    *out_col = col;
    *out_row = row;
    return true;
}

static Vector2 tile_center_screen(int map_col, int map_row)
{
    int ts = tile_px();
    return (Vector2){
        (float)((map_col - s_vp_col) * ts + ts / 2),
        (float)((map_row - s_vp_row) * ts + ts / 2),
    };
}

/* Drawer tab: fixed strip at the very bottom of the screen */
static Rectangle drawer_tab_rect(void)
{
    int sw    = GetScreenWidth();
    int sh    = GetScreenHeight();
    int tab_h = sh * 7 / 100;
    return (Rectangle){ 0, (float)(sh - tab_h), (float)sw, (float)tab_h };
}

/* Drawer body: slides up from behind the tab */
static Rectangle drawer_body_rect(void)
{
    int sw     = GetScreenWidth();
    int sh     = GetScreenHeight();
    int tab_h  = sh *  7 / 100;
    int body_h = sh * 38 / 100;
    float y = (float)(sh - tab_h) - s_drawer_t * (float)body_h;
    return (Rectangle){ 0, y, (float)sw, (float)body_h };
}

/* ------------------------------------------------------------ game helpers */

static void record_visited(int col, int row)
{
    if (!s_col_visited[col]) {
        s_col_visited[col] = true;
        s_scanned_cols[s_scanned_col_count++] = col;
    }
    if (!s_row_visited[row]) {
        s_row_visited[row] = true;
        s_scanned_rows[s_scanned_row_count++] = row;
    }
}

static void check_scroll_trigger(void)
{
    int vc = vis_cols();
    int vr = vis_rows();

    /* Scroll when hero strays more than SCROLL_DEAD_ZONE cells from
       the viewport centre.  Moving by 1 cell puts the hero back inside
       the dead zone, which prevents the old edge-cascade problem. */
    int center_col = s_vp_col + vc / 2;
    int center_row = s_vp_row + vr / 2;
    int off_col = s_hero_col - center_col;
    int off_row = s_hero_row - center_row;

    if (off_col > SCROLL_DEAD_ZONE)       s_vp_col++;
    else if (off_col < -SCROLL_DEAD_ZONE) s_vp_col--;

    if (off_row > SCROLL_DEAD_ZONE)       s_vp_row++;
    else if (off_row < -SCROLL_DEAD_ZONE) s_vp_row--;

    if (s_vp_col < 0)              s_vp_col = 0;
    if (s_vp_col + vc > MAP_COLS)  s_vp_col = MAP_COLS - vc;
    if (s_vp_row < 0)              s_vp_row = 0;
    if (s_vp_row + vr > MAP_ROWS)  s_vp_row = MAP_ROWS - vr;
}

static void advance_hero(int new_col, int new_row)
{
    if (new_col < 0 || new_col >= MAP_COLS) return;
    if (new_row < 0 || new_row >= MAP_ROWS) return;
    if (new_col == s_hero_col && new_row == s_hero_row) return;

    /* Slide the tile at the destination back to the hero's previous cell */
    s_map[s_hero_row][s_hero_col] = s_map[new_row][new_col];

    if (s_trail_len < MAX_TRAIL) {
        s_trail_col[s_trail_len] = new_col;
        s_trail_row[s_trail_len] = new_row;
        s_trail_len++;
    }

    s_hero_col = new_col;
    s_hero_row = new_row;

    record_visited(new_col, new_row);
    check_scroll_trigger();
}

static void add_connection(Terrain t, int amount)
{
    if (s_pending_count >= MAX_PENDING) return;
    Resource res = TERRAIN_RESOURCE[t];
    /* merge with last entry if same resource */
    if (s_pending_count > 0 &&
        s_pending[s_pending_count - 1].resource == res) {
        s_pending[s_pending_count - 1].amount += amount;
    } else {
        s_pending[s_pending_count++] = (Connection){ res, amount };
    }
}

/* Pick a random terrain that is NOT the given type, for in-place replacement. */
static Terrain random_different_terrain(Terrain exclude)
{
    Terrain t;
    do { t = (Terrain)GetRandomValue(0, TERRAIN_COUNT - 1); } while (t == exclude);
    return t;
}

static void scan_line_horizontal(int row)
{
    int c = 0;
    while (c < MAP_COLS) {
        Terrain t   = s_map[row][c];
        int     run = 1;
        while (c + run < MAP_COLS && s_map[row][c + run] == t) run++;
        if (run >= 3) {
            add_connection(t, run >= 5 ? 2 : 1);
            /* replace matched tiles in-place with new random terrain */
            for (int k = 0; k < run; k++) {
                if (s_flash_count < MAX_FLASH_CELLS) {
                    s_flash_col[s_flash_count] = c + k;
                    s_flash_row[s_flash_count] = row;
                    s_flash_count++;
                }
                s_map[row][c + k] = random_different_terrain(t);
            }
        }
        c += run;
    }
}

static void scan_line_vertical(int col)
{
    int r = 0;
    while (r < MAP_ROWS) {
        Terrain t   = s_map[r][col];
        int     run = 1;
        while (r + run < MAP_ROWS && s_map[r + run][col] == t) run++;
        if (run >= 3) {
            add_connection(t, run >= 5 ? 2 : 1);
            /* replace matched tiles in-place with new random terrain */
            for (int k = 0; k < run; k++) {
                if (s_flash_count < MAX_FLASH_CELLS) {
                    s_flash_col[s_flash_count] = col;
                    s_flash_row[s_flash_count] = r + k;
                    s_flash_count++;
                }
                s_map[r + k][col] = random_different_terrain(t);
            }
        }
        r += run;
    }
}

static void scan_matches(void)
{
    for (int i = 0; i < s_scanned_col_count; i++)
        scan_line_vertical(s_scanned_cols[i]);
    for (int i = 0; i < s_scanned_row_count; i++)
        scan_line_horizontal(s_scanned_rows[i]);
}

static void bank_connections(void)
{
    for (int i = 0; i < s_pending_count; i++)
        s_resources[s_pending[i].resource] += s_pending[i].amount;
    s_pending_count = 0;
}

static void try_action(int idx)
{
    if (idx < 0 || idx >= ACTION_COUNT) return;
    Resource r = ACTIONS[idx].cost_resource;
    int      c = ACTIONS[idx].cost_amount;
    if (s_resources[r] < c) return;
    s_resources[r] -= c;
    s_action_counts[idx]++;
}

static void end_turn(void)
{
    s_is_dragging = false;
    s_flash_count = 0;
    scan_matches();
    s_phase      = PHASE_SCANNING;
    s_scan_flash = (s_flash_count > 0) ? SCAN_FLASH_SECS : 0.0f;
    s_turn_timer = TURN_SECS;
}

/* ------------------------------------------------------------- map generation */

static void generate_map(void)
{
    /* weights: Plains 30, Forest 25, Mountain 15, City 10, Water 20 */
    static const int WEIGHTS[TERRAIN_COUNT] = { 30, 25, 15, 10, 20 };
    static Terrain   LUT[100];
    int idx = 0;
    for (int t = 0; t < TERRAIN_COUNT; t++)
        for (int k = 0; k < WEIGHTS[t]; k++)
            LUT[idx++] = (Terrain)t;
    for (int r = 0; r < MAP_ROWS; r++)
        for (int c = 0; c < MAP_COLS; c++)
            s_map[r][c] = LUT[GetRandomValue(0, 99)];
}

/* ----------------------------------------------------------- input callbacks */

static void on_down(Vector2 pos)
{
    if (s_phase != PHASE_IDLE) return;

    int col, row;
    if (!screen_to_tile(pos, &col, &row)) return;
    if (col != s_hero_col || row != s_hero_row) return;

    s_is_dragging       = true;
    s_turn_timer        = TURN_SECS;
    s_phase             = PHASE_DRAGGING;
    s_trail_len         = 0;
    s_scanned_col_count = 0;
    s_scanned_row_count = 0;
    memset(s_col_visited, 0, sizeof(s_col_visited));
    memset(s_row_visited, 0, sizeof(s_row_visited));
    record_visited(s_hero_col, s_hero_row);
    s_vp_col_prev = s_vp_col;
    s_vp_row_prev = s_vp_row;
    s_move_cd     = 0.0f;
}

static void on_move(Vector2 pos)
{
    if (!s_is_dragging) return;
    if (s_move_cd > 0.0f) return;   /* wait for cooldown between steps */

    /* Compensate for viewport shifts since last move.  When the viewport
       scrolls, the same screen pixel maps to a different tile.  Subtract
       the pixel displacement so the hero only moves when the *finger*
       actually moves to a new tile. */
    int ts = tile_px();
    float adj_x = pos.x - (float)((s_vp_col - s_vp_col_prev) * ts);
    float adj_y = pos.y - (float)((s_vp_row - s_vp_row_prev) * ts);
    s_vp_col_prev = s_vp_col;
    s_vp_row_prev = s_vp_row;

    int col, row;
    if (!screen_to_tile((Vector2){ adj_x, adj_y }, &col, &row)) return;

    /* Only step one orthogonal cell at a time toward the cursor tile. */
    int dc = col - s_hero_col;
    int dr = row - s_hero_row;
    if (dc == 0 && dr == 0) return;

    int step_col = s_hero_col;
    int step_row = s_hero_row;
    if (abs(dc) >= abs(dr))
        step_col += (dc > 0) ? 1 : -1;
    else
        step_row += (dr > 0) ? 1 : -1;

    advance_hero(step_col, step_row);
    s_move_cd = MOVE_COOLDOWN;
}

static void on_up(Vector2 pos)
{
    if (s_is_dragging) {
        end_turn();
        return;
    }

    /* drawer tab toggle */
    if (CheckCollisionPointRec(pos, drawer_tab_rect())) {
        s_drawer = (s_drawer == DRAWER_OPEN) ? DRAWER_CLOSED : DRAWER_OPEN;
        return;
    }

    if (s_drawer != DRAWER_OPEN) return;

    Rectangle dr  = drawer_body_rect();
    int        pad = (int)(dr.width * 0.05f);

    /* bank button */
    if (s_pending_count > 0) {
        int       bh       = (int)(dr.height * 0.18f);
        Rectangle bank_btn = {
            dr.x + pad,
            dr.y + (int)(dr.height * 0.08f),
            dr.width - pad * 2,
            (float)bh
        };
        if (CheckCollisionPointRec(pos, bank_btn)) {
            bank_connections();
            return;
        }
    }

    /* action buttons (2×2 grid) */
    int btn_w   = (int)((dr.width - pad * 3) / 2);
    int btn_h   = (int)(dr.height * 0.20f);
    int start_y = (int)(dr.y + dr.height * 0.34f);
    for (int i = 0; i < ACTION_COUNT; i++) {
        int bx = (int)(dr.x + pad + (i % 2) * (btn_w + pad));
        int by = start_y + (i / 2) * (btn_h + pad / 2);
        Rectangle btn = { (float)bx, (float)by, (float)btn_w, (float)btn_h };
        if (CheckCollisionPointRec(pos, btn)) {
            try_action(i);
            return;
        }
    }
}

/* --------------------------------------------------------------- rendering */

static void draw_map_tiles(void)
{
    int ts = tile_px();
    int vc = vis_cols();
    int vr = vis_rows();
    for (int row = s_vp_row; row < s_vp_row + vr && row < MAP_ROWS; row++) {
        for (int col = s_vp_col; col < s_vp_col + vc && col < MAP_COLS; col++) {
            if (col == s_hero_col && row == s_hero_row) continue;
            int   sx = (col - s_vp_col) * ts;
            int   sy = (row - s_vp_row) * ts;
            Color c  = TERRAIN_COLOR[s_map[row][col]];
            DrawRectangle(sx, sy, ts - 1, ts - 1, c);
            char letter[2] = { TERRAIN_LETTER[s_map[row][col]], '\0' };
            int  fs = ts * 28 / 100;
            int  lw = MeasureText(letter, fs);
            DrawText(letter, sx + (ts - lw) / 2, sy + (ts - fs) / 2,
                     fs, (Color){ 0, 0, 0, 70 });
        }
    }
}

static void draw_trail(void)
{
    if (!s_is_dragging || s_trail_len == 0) return;
    int ts = tile_px();
    for (int i = 0; i < s_trail_len; i++) {
        int col = s_trail_col[i];
        int row = s_trail_row[i];
        /* skip if off-screen */
        if (col < s_vp_col || col >= s_vp_col + vis_cols()) continue;
        if (row < s_vp_row || row >= s_vp_row + vis_rows()) continue;
        int sx = (col - s_vp_col) * ts;
        int sy = (row - s_vp_row) * ts;
        DrawRectangle(sx, sy, ts - 1, ts - 1, (Color){ 255, 255, 255, 45 });
    }
}

static void draw_scan_flash(void)
{
    if (s_phase != PHASE_SCANNING || s_scan_flash <= 0.0f || s_flash_count == 0) return;

    /* pulse: bright at start, fades out */
    float frac  = s_scan_flash / SCAN_FLASH_SECS;
    /* rapid pulse using sine so tiles visibly strobe */
    float pulse = 0.55f + 0.45f * sinf(frac * 3.14159f * 4.0f);
    unsigned char a = (unsigned char)(pulse * 220.0f);

    int ts = tile_px();
    int vc = vis_cols();
    int vr = vis_rows();

    for (int i = 0; i < s_flash_count; i++) {
        int col = s_flash_col[i];
        int row = s_flash_row[i];
        if (col < s_vp_col || col >= s_vp_col + vc) continue;
        if (row < s_vp_row || row >= s_vp_row + vr) continue;
        int sx = (col - s_vp_col) * ts;
        int sy = (row - s_vp_row) * ts;
        /* bright white flash over the replaced tile */
        DrawRectangle(sx, sy, ts - 1, ts - 1, (Color){ 255, 255, 200, a });
        /* sparkle border */
        DrawRectangleLinesEx(
            (Rectangle){ (float)sx, (float)sy, (float)(ts - 1), (float)(ts - 1) },
            2.0f, (Color){ 255, 220, 60, (unsigned char)(a * 0.8f) });
    }
}

static void draw_hero(void)
{
    int ts = tile_px();
    int sx = (s_hero_col - s_vp_col) * ts;
    int sy = (s_hero_row - s_vp_row) * ts;

    /* tile fill */
    DrawRectangle(sx, sy, ts - 1, ts - 1, (Color){ 240, 220, 100, 255 });
    /* highlight border */
    DrawRectangleLinesEx(
        (Rectangle){ (float)sx, (float)sy, (float)(ts - 1), (float)(ts - 1) },
        2.0f, (Color){ 180, 140, 40, 255 });
    /* letter */
    int  fs = ts * 36 / 100;
    int  lw = MeasureText("H", fs);
    DrawText("H", sx + (ts - 1 - lw) / 2, sy + (ts - 1 - fs) / 2,
             fs, (Color){ 80, 50, 10, 200 });
}

static void draw_timer_bar(void)
{
    if (s_phase != PHASE_DRAGGING) return;
    int   sw    = GetScreenWidth();
    int   sh    = GetScreenHeight();
    int   top   = sh * 7 / 100;          /* sits just below resource strip */
    int   bh    = sh / 60;
    float frac  = s_turn_timer / TURN_SECS;
    Color col   = frac > 0.57f ? (Color){  80, 200,  80, 230 }
                : frac > 0.28f ? (Color){ 220, 200,  60, 230 }
                               : (Color){ 220,  60,  60, 230 };
    /* filled portion */
    DrawRectangle(0, top, (int)((float)sw * frac), bh, col);
    /* depleted portion */
    DrawRectangle((int)((float)sw * frac), top, sw - (int)((float)sw * frac), bh,
                  (Color){ 50, 50, 50, 180 });
}

static void draw_resource_strip(void)
{
    int sw      = GetScreenWidth();
    int sh      = GetScreenHeight();
    int strip_h = sh * 7 / 100;
    DrawRectangle(0, 0, sw, strip_h, (Color){ 20, 20, 25, 210 });

    int   col_w = sw / RES_COUNT;
    int   fs    = strip_h * 48 / 100;
    for (int i = 0; i < RES_COUNT; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s %d", RES_NAME[i], s_resources[i]);
        int tw = MeasureText(buf, fs);
        DrawText(buf, i * col_w + (col_w - tw) / 2,
                 (strip_h - fs) / 2, fs, RES_COLOR[i]);
    }

    /* pending badge */
    if (s_pending_count > 0) {
        char   pbuf[32];
        snprintf(pbuf, sizeof(pbuf), "+%d pending", s_pending_count);
        int    psz = strip_h * 34 / 100;
        int    pw  = MeasureText(pbuf, psz);
        int    px  = (sw - pw) / 2;
        int    py  = strip_h + 6;
        DrawRectangle(px - 6, py - 2, pw + 12, psz + 4, (Color){ 10, 10, 10, 160 });
        DrawText(pbuf, px, py, psz, (Color){ 255, 240, 80, 230 });
    }
}

static void draw_drawer(void)
{
    int sw = GetScreenWidth();

    /* --- always-visible tab --- */
    Rectangle tab = drawer_tab_rect();
    DrawRectangleRec(tab, (Color){ 30, 36, 48, 240 });
    DrawRectangleLinesEx(tab, 1.5f, (Color){ 70, 85, 110, 255 });

    int tab_fs = (int)(tab.height * 0.42f);
    /* resource summary in tab */
    int item_w = sw / RES_COUNT;
    for (int i = 0; i < RES_COUNT; i++) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%s:%d", RES_NAME[i], s_resources[i]);
        int fs = tab_fs;
        int tw = MeasureText(buf, fs);
        DrawText(buf, i * item_w + (item_w - tw) / 2,
                 (int)(tab.y + (tab.height - fs) / 2), fs, RES_COLOR[i]);
    }

    /* arrow indicator */
    const char *arrow = (s_drawer == DRAWER_OPEN) ? "v" : "^";
    int afs = tab_fs;
    int aw  = MeasureText(arrow, afs);
    DrawText(arrow, sw - aw - 12,
             (int)(tab.y + (tab.height - afs) / 2), afs,
             (Color){ 160, 160, 160, 200 });

    if (s_drawer_t < 0.05f) return;

    /* --- sliding body --- */
    Rectangle dr  = drawer_body_rect();
    int        pad = (int)(dr.width * 0.05f);
    DrawRectangleRec(dr, (Color){ 25, 30, 42, 235 });
    DrawRectangleLinesEx(dr, 1.5f, (Color){ 60, 75, 100, 200 });

    if (s_drawer_t < 0.25f) return; /* skip text while barely open */

    /* --- bank button --- */
    int       bh       = (int)(dr.height * 0.18f);
    Rectangle bank_btn = {
        dr.x + pad,
        dr.y + (int)(dr.height * 0.08f),
        dr.width - pad * 2,
        (float)bh
    };
    bool  can_bank  = s_pending_count > 0;
    Color bank_bg   = can_bank ? (Color){ 50, 150, 60, 255 }
                               : (Color){ 45, 50,  60, 200 };
    DrawRectangleRounded(bank_btn, 0.2f, 8, bank_bg);

    char bank_lbl[48];
    snprintf(bank_lbl, sizeof(bank_lbl), "Bank Connections  (%d)", s_pending_count);
    int bfs = bh * 38 / 100;
    int blw = MeasureText(bank_lbl, bfs);
    Color bank_text = can_bank ? WHITE : (Color){ 90, 90, 90, 255 };
    DrawText(bank_lbl,
             (int)(bank_btn.x + (bank_btn.width  - blw) / 2),
             (int)(bank_btn.y + (bank_btn.height - bfs) / 2),
             bfs, bank_text);

    /* --- action buttons (2×2) --- */
    int btn_w   = (int)((dr.width - pad * 3) / 2);
    int btn_h   = (int)(dr.height * 0.20f);
    int start_y = (int)(dr.y + dr.height * 0.34f);
    for (int i = 0; i < ACTION_COUNT; i++) {
        int bx = (int)(dr.x + pad + (i % 2) * (btn_w + pad));
        int by = start_y + (i / 2) * (btn_h + pad / 2);
        Rectangle btn = { (float)bx, (float)by, (float)btn_w, (float)btn_h };

        Resource r         = ACTIONS[i].cost_resource;
        bool     affordable = s_resources[r] >= ACTIONS[i].cost_amount;
        Color    btn_bg    = affordable ? (Color){ 45, 65, 110, 255 }
                                        : (Color){ 40, 40,  50, 180 };
        DrawRectangleRounded(btn, 0.18f, 8, btn_bg);

        /* action name */
        int   afs = btn_h * 33 / 100;
        int   alw = MeasureText(ACTIONS[i].name, afs);
        Color atc = affordable ? WHITE : (Color){ 80, 80, 80, 255 };
        DrawText(ACTIONS[i].name, bx + (btn_w - alw) / 2, by + 6, afs, atc);

        /* cost + count */
        char  cost_buf[32];
        snprintf(cost_buf, sizeof(cost_buf), "-%d %s  [x%d]",
                 ACTIONS[i].cost_amount, RES_NAME[r], s_action_counts[i]);
        int cfs = btn_h * 24 / 100;
        int clw = MeasureText(cost_buf, cfs);
        Color ctc = affordable ? RES_COLOR[r] : (Color){ 70, 70, 70, 255 };
        DrawText(cost_buf, bx + (btn_w - clw) / 2,
                 by + btn_h - cfs - 6, cfs, ctc);
    }

    /* --- hint when nothing is pending --- */
    if (s_pending_count == 0 && s_drawer_t > 0.8f) {
        int   hfs = (int)(dr.height * 0.06f);
        const char *hint = "Drag your hero to match tiles and earn connections";
        int   hw  = MeasureText(hint, hfs);
        if (hw > sw - pad * 2) hfs = hfs * (sw - pad * 2) / hw;
        hw = MeasureText(hint, hfs);
        DrawText(hint, (sw - hw) / 2,
                 (int)(bank_btn.y + (bank_btn.height - hfs) / 2),
                 hfs, (Color){ 100, 110, 130, 200 });
    }
}

/* ------------------------------------------------- prototype callbacks */

static void RealmWalkInit(void)
{
    generate_map();

    s_hero_col = MAP_COLS / 2;
    s_hero_row = MAP_ROWS / 2;

    int vc = vis_cols();
    int vr = vis_rows();
    s_vp_col = s_hero_col - vc / 2;
    s_vp_row = s_hero_row - vr / 2;
    if (s_vp_col < 0) s_vp_col = 0;
    if (s_vp_row < 0) s_vp_row = 0;
    if (s_vp_col + vc > MAP_COLS) s_vp_col = MAP_COLS - vc;
    if (s_vp_row + vr > MAP_ROWS) s_vp_row = MAP_ROWS - vr;
    s_vp_col_prev = s_vp_col;
    s_vp_row_prev = s_vp_row;

    s_is_dragging       = false;
    s_phase             = PHASE_IDLE;
    s_turn_timer        = TURN_SECS;
    s_trail_len         = 0;
    s_scan_flash        = 0.0f;
    s_scanned_col_count = 0;
    s_scanned_row_count = 0;

    memset(s_col_visited,   0, sizeof(s_col_visited));
    memset(s_row_visited,   0, sizeof(s_row_visited));
    memset(s_resources,     0, sizeof(s_resources));
    memset(s_action_counts, 0, sizeof(s_action_counts));

    s_pending_count = 0;
    s_flash_count   = 0;
    s_move_cd       = 0.0f;
    s_drawer        = DRAWER_CLOSED;
    s_drawer_t      = 0.0f;
    s_prev_tc       = 0;
    s_prev_touch    = (Vector2){ -1.0f, -1.0f };
}

static void RealmWalkUpdate(float dt)
{
    int     tc = GetTouchPointCount();
    Vector2 tp = (tc > 0) ? GetTouchPosition(0) : (Vector2){ -1.0f, -1.0f };

    bool handled = false;
    if (tc > 0 && s_prev_tc == 0)      { on_down(tp);          handled = true; }
    else if (tc > 0 && s_prev_tc > 0)  { on_move(tp);          handled = true; }
    else if (tc == 0 && s_prev_tc > 0) { on_up(s_prev_touch);  handled = true; }

    if (tc > 0) s_prev_touch = tp;
    s_prev_tc = tc;

    /* mouse fallback (desktop / no touch) */
    if (!handled) {
        Vector2 mp = GetMousePosition();
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))       on_down(mp);
        else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))     on_move(mp);
        else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) on_up(mp);
    }

    /* move cooldown + turn timer */
    if (s_phase == PHASE_DRAGGING) {
        if (s_move_cd > 0.0f) s_move_cd -= dt;
        s_turn_timer -= dt;
        if (s_turn_timer <= 0.0f) {
            s_turn_timer = 0.0f;
            end_turn();
        }
    }

    /* scan flash countdown → idle */
    if (s_phase == PHASE_SCANNING) {
        s_scan_flash -= dt;
        if (s_scan_flash <= 0.0f) {
            s_scan_flash = 0.0f;
            s_phase      = PHASE_IDLE;
        }
    }

    /* drawer animation */
    float target = (s_drawer == DRAWER_OPEN) ? 1.0f : 0.0f;
    s_drawer_t  += (target - s_drawer_t) * fminf(DRAWER_SPEED * dt, 1.0f);
}

static void RealmWalkDraw(void)
{
    ClearBackground((Color){ 34, 40, 49, 255 });
    draw_map_tiles();
    draw_trail();
    draw_scan_flash();
    draw_hero();
    draw_resource_strip();
    draw_timer_bar();
    draw_drawer();
}

static void RealmWalkDeinit(void)
{
    /* all state is static; reset happens in Init */
}

const Prototype RealmWalkProto = {
    .name        = "Realm Walk",
    .description = "Drag hero to displace tiles. Match 3+ in a line to earn resources.",
    .Init        = RealmWalkInit,
    .Update      = RealmWalkUpdate,
    .Draw        = RealmWalkDraw,
    .Deinit      = RealmWalkDeinit,
};
