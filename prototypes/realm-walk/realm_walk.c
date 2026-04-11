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

/* ---- animation constants ---- */
#define HERO_LERP_SPD        14.0f   /* cells/sec exponential approach  */
#define VP_LERP_SPD          10.0f   /* viewport pan speed              */
#define FLY_LERP_SPD         12.0f   /* flying tile slide speed         */
#define FLY_ARRIVE_DIST      0.08f   /* snap flying tile when this close*/
#define MAX_FLYING_TILES     8       /* simultaneous displaced tiles    */
#define BUMP_SCALE_PEAK      1.15f   /* hero landing scale overshoot    */
#define BUMP_DECAY           18.0f   /* how fast bump scale returns to 1*/
#define TRAIL_ALPHA_OLD      20      /* alpha of oldest trail tile      */
#define TRAIL_ALPHA_NEW      120     /* alpha of newest trail tile      */
#define TRAIL_FADEIN_SECS    0.12f   /* fade-in duration for new tile   */
#define TRAIL_BORDER_RECENT  3       /* last N trail tiles get a border */

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

typedef enum {
    STRUCT_NONE = 0,
    STRUCT_DENSE_FOREST,  /* 2x2 forest */
    STRUCT_FARM,          /* 2x2 plains */
    STRUCT_CASTLE,        /* 3x3: mountain center + 8 water ring */
    STRUCT_TYPE_COUNT
} StructureType;

typedef struct {
    StructureType type;
    int           col, row;   /* top-left anchor */
    int           w, h;       /* size in tiles */
} Structure;

typedef struct {
    float   vis_col, vis_row;   /* current visual position (float cells) */
    float   dst_col, dst_row;   /* logical destination cell              */
    Terrain terrain;
    bool    active;
} FlyingTile;

#define MAX_STRUCTURES 32

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

/* combined-tile structures */
static Structure s_structures[MAX_STRUCTURES];
static int       s_structure_count;
/* per-cell index into s_structures; -1 when not part of a structure */
static int       s_cell_struct[MAP_ROWS][MAP_COLS];

/* ---- animation state ---- */
static float s_hero_vis_col, s_hero_vis_row;  /* lerps toward s_hero_col/row */
static float s_vp_vis_col,   s_vp_vis_row;    /* lerps toward s_vp_col/row   */
static float s_hero_bump;                      /* 0..0.15, decays after land  */
static FlyingTile s_flying[MAX_FLYING_TILES];
static bool       s_cell_flying_dst[MAP_ROWS][MAP_COLS]; /* suppressed cells  */
static float      s_trail_age[MAX_TRAIL];      /* seconds since tile placed   */

/* -------------------------------------------------------------- data tables */

static const Color TERRAIN_COLOR[TERRAIN_COUNT] = {
    { 180, 220, 100, 255 }, /* PLAINS   – olive green */
    {  34, 120,  50, 255 }, /* FOREST   – dark green  */
    { 130, 130, 140, 255 }, /* MOUNTAIN – stone grey  */
    { 200, 160,  80, 255 }, /* CITY     – amber       */
    {  60, 130, 200, 255 }, /* WATER    – blue        */
};

static const char TERRAIN_LETTER[TERRAIN_COUNT] = { 'P', 'F', 'M', 'C', 'W' };

static const Color STRUCT_COLOR[STRUCT_TYPE_COUNT] = {
    {   0,   0,   0,   0 }, /* STRUCT_NONE          – unused              */
    {  20,  80,  30, 255 }, /* STRUCT_DENSE_FOREST  – very dark green     */
    { 230, 200,  90, 255 }, /* STRUCT_FARM          – wheat / straw       */
    { 200, 200, 220, 255 }, /* STRUCT_CASTLE        – silver stone        */
};

static const char *STRUCT_LABEL[STRUCT_TYPE_COUNT] = {
    "", "Dense Forest", "Farm", "Castle"
};

static const Color RES_COLOR[RES_COUNT] = {
    { 255, 215,   0, 255 }, /* GOLD       – yellow     */
    { 180, 180, 255, 255 }, /* FAITH      – lavender   */
    { 220,  60,  60, 255 }, /* WAR        – red        */
    {  60, 220, 220, 255 }, /* REPUTATION – cyan       */
};

static const char *RES_NAME[RES_COUNT] = { "Gold", "Faith", "War", "Rep" };

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

static bool check_scroll_trigger(void)
{
    int vc = vis_cols();
    int vr = vis_rows();
    int old_vp_col = s_vp_col;
    int old_vp_row = s_vp_row;

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

    return (s_vp_col != old_vp_col || s_vp_row != old_vp_row);
}

/* Move a structure as a unit. Hero is about to enter cell (new_col, new_row),
 * which is part of structure si. The structure shifts opposite to the hero's
 * movement direction by its own width/height. The terrain currently at the
 * structure's destination rectangle mirrors back into the structure's old
 * rectangle. Returns true on success, false if blocked. */
static bool shift_structure(int new_col, int new_row, int si)
{
    Structure *s = &s_structures[si];

    int dx = new_col - s_hero_col;    /* -1, 0, or 1 */
    int dy = new_row - s_hero_row;

    int shift_c = -dx * s->w;
    int shift_r = -dy * s->h;

    int nc = s->col + shift_c;
    int nr = s->row + shift_r;

    /* Bounds check the structure's new rectangle */
    if (nc < 0 || nc + s->w > MAP_COLS) return false;
    if (nr < 0 || nr + s->h > MAP_ROWS) return false;

    /* Don't allow collision with another structure in the new rectangle */
    for (int r = 0; r < s->h; r++)
        for (int c = 0; c < s->w; c++) {
            int ci = s_cell_struct[nr + r][nc + c];
            if (ci >= 0 && ci != si) return false;
        }

    /* Block swap: save old rectangles and exchange */
    Terrain old_struct[9]; /* up to 3x3 */
    Terrain old_dest[9];
    for (int r = 0; r < s->h; r++)
        for (int c = 0; c < s->w; c++) {
            old_struct[r * s->w + c] = s_map[s->row + r][s->col + c];
            old_dest  [r * s->w + c] = s_map[nr + r][nc + c];
        }
    for (int r = 0; r < s->h; r++)
        for (int c = 0; c < s->w; c++) {
            s_map[s->row + r][s->col + c] = old_dest  [r * s->w + c];
            s_map[nr + r][nc + c]          = old_struct[r * s->w + c];
        }

    /* Move the structure's cell-index footprint. Shift is always ±w or ±h,
     * so the old and new rectangles never overlap — safe to clear then set. */
    for (int r = 0; r < s->h; r++)
        for (int c = 0; c < s->w; c++)
            s_cell_struct[s->row + r][s->col + c] = -1;
    for (int r = 0; r < s->h; r++)
        for (int c = 0; c < s->w; c++)
            s_cell_struct[nr + r][nc + c] = si;

    s->col = nc;
    s->row = nr;
    return true;
}

static bool advance_hero(int new_col, int new_row)
{
    if (new_col < 0 || new_col >= MAP_COLS) return false;
    if (new_row < 0 || new_row >= MAP_ROWS) return false;
    if (new_col == s_hero_col && new_row == s_hero_row) return false;

    int si = s_cell_struct[new_row][new_col];
    if (si >= 0) {
        /* Destination is part of a combined tile — push the whole thing. */
        if (!shift_structure(new_col, new_row, si)) {
            /* Structure can't move (wall, other structure) — block the hero. */
            return false;
        }
        /* After the block-swap, the cell at (new_col, new_row) now contains
         * terrain from the destination rectangle. The hero simply steps
         * onto it — no further single-tile swap is needed because the
         * block swap already put the displaced terrain into the old space. */
    } else {
        /* Normal single-tile displacement */
        Terrain displaced = s_map[new_row][new_col];
        s_map[s_hero_row][s_hero_col] = displaced;

        /* Spawn flying tile: slides from hero's destination back to vacated cell */
        for (int _fi = 0; _fi < MAX_FLYING_TILES; _fi++) {
            if (!s_flying[_fi].active) {
                s_flying[_fi] = (FlyingTile){
                    .active  = true,
                    .terrain = displaced,
                    .vis_col = (float)new_col,
                    .vis_row = (float)new_row,
                    .dst_col = (float)s_hero_col,
                    .dst_row = (float)s_hero_row,
                };
                s_cell_flying_dst[s_hero_row][s_hero_col] = true;
                break;
            }
        }
    }

    if (s_trail_len < MAX_TRAIL) {
        s_trail_col[s_trail_len] = new_col;
        s_trail_row[s_trail_len] = new_row;
        s_trail_age[s_trail_len] = 0.0f;
        s_trail_len++;
    }

    s_hero_col = new_col;
    s_hero_row = new_row;
    s_hero_bump = BUMP_SCALE_PEAK - 1.0f;

    record_visited(new_col, new_row);

    /* New merges resolve at end of turn, not on every step. Existing
     * structures are still tracked because shift_structure() updates
     * s_cell_struct in place. */

    return check_scroll_trigger();
}

/* ------------------------------------------------------ structure helpers */

static void clear_structures(void)
{
    s_structure_count = 0;
    for (int r = 0; r < MAP_ROWS; r++)
        for (int c = 0; c < MAP_COLS; c++)
            s_cell_struct[r][c] = -1;
}

static bool cell_has_hero(int col, int row)
{
    return (col == s_hero_col && row == s_hero_row);
}

static bool add_structure(StructureType type, int col, int row, int w, int h)
{
    if (s_structure_count >= MAX_STRUCTURES) return false;
    int idx = s_structure_count++;
    s_structures[idx] = (Structure){ type, col, row, w, h };
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            s_cell_struct[row + r][col + c] = idx;
    return true;
}

static bool try_2x2(StructureType type, Terrain t, int c, int r)
{
    if (s_cell_struct[r  ][c  ] >= 0) return false;
    if (s_cell_struct[r  ][c+1] >= 0) return false;
    if (s_cell_struct[r+1][c  ] >= 0) return false;
    if (s_cell_struct[r+1][c+1] >= 0) return false;

    if (s_map[r  ][c  ] != t) return false;
    if (s_map[r  ][c+1] != t) return false;
    if (s_map[r+1][c  ] != t) return false;
    if (s_map[r+1][c+1] != t) return false;

    /* Don't form a structure under the hero — it would trap them. */
    if (cell_has_hero(c,   r  )) return false;
    if (cell_has_hero(c+1, r  )) return false;
    if (cell_has_hero(c,   r+1)) return false;
    if (cell_has_hero(c+1, r+1)) return false;

    return add_structure(type, c, r, 2, 2);
}

static bool try_castle(int cc, int cr)
{
    /* Mountain at centre (cc, cr), 8 water neighbours. */
    if (cc < 1 || cc >= MAP_COLS - 1) return false;
    if (cr < 1 || cr >= MAP_ROWS - 1) return false;

    if (s_map[cr][cc] != TERRAIN_MOUNTAIN) return false;

    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            if (dc == 0 && dr == 0) continue;
            if (s_map[cr + dr][cc + dc] != TERRAIN_WATER) return false;
        }
    }

    int ac = cc - 1;
    int ar = cr - 1;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            if (s_cell_struct[ar + r][ac + c] >= 0)   return false;
            if (cell_has_hero(ac + c, ar + r))        return false;
        }

    return add_structure(STRUCT_CASTLE, ac, ar, 3, 3);
}

static void detect_structures(void)
{
    clear_structures();

    /* Castles first — they're the largest and most specific. */
    for (int r = 1; r < MAP_ROWS - 1; r++)
        for (int c = 1; c < MAP_COLS - 1; c++)
            try_castle(c, r);

    /* 2x2 dense forest */
    for (int r = 0; r < MAP_ROWS - 1; r++)
        for (int c = 0; c < MAP_COLS - 1; c++)
            try_2x2(STRUCT_DENSE_FOREST, TERRAIN_FOREST, c, r);

    /* 2x2 farm */
    for (int r = 0; r < MAP_ROWS - 1; r++)
        for (int c = 0; c < MAP_COLS - 1; c++)
            try_2x2(STRUCT_FARM, TERRAIN_PLAINS, c, r);
}

static void scan_matches(void)
{
    /* Match-3 consuming patterns are disabled — we only form structures. */
    detect_structures();
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

    bool scrolled = advance_hero(step_col, step_row);
    if (scrolled) s_move_cd = MOVE_COOLDOWN;
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

    /* Pass 1: draw individual terrain tiles (skip cells that belong to a
     * structure — those are rendered as one large tile in pass 2). */
    for (int row = s_vp_row; row < s_vp_row + vr && row < MAP_ROWS; row++) {
        for (int col = s_vp_col; col < s_vp_col + vc && col < MAP_COLS; col++) {
            if (col == s_hero_col && row == s_hero_row) continue;
            if (s_cell_struct[row][col] >= 0) continue;
            if (s_cell_flying_dst[row][col]) continue;   /* animated tile en-route */
            int   sx = (int)((col - s_vp_vis_col) * ts);
            int   sy = (int)((row - s_vp_vis_row) * ts);
            Color c  = TERRAIN_COLOR[s_map[row][col]];
            DrawRectangle(sx, sy, ts - 1, ts - 1, c);
            char letter[2] = { TERRAIN_LETTER[s_map[row][col]], '\0' };
            int  fs = ts * 28 / 100;
            int  lw = MeasureText(letter, fs);
            DrawText(letter, sx + (ts - lw) / 2, sy + (ts - fs) / 2,
                     fs, (Color){ 0, 0, 0, 70 });
        }
    }

    /* Pass 2: draw each structure as a single merged rectangle */
    for (int i = 0; i < s_structure_count; i++) {
        Structure *s = &s_structures[i];
        /* cull off-screen */
        if (s->col + s->w <= s_vp_col || s->col >= s_vp_col + vc) continue;
        if (s->row + s->h <= s_vp_row || s->row >= s_vp_row + vr) continue;

        int sx = (int)((s->col - s_vp_vis_col) * ts);
        int sy = (int)((s->row - s_vp_vis_row) * ts);
        int w  = s->w * ts - 1;
        int h  = s->h * ts - 1;

        Color col = STRUCT_COLOR[s->type];
        DrawRectangle(sx, sy, w, h, col);
        /* darker border to emphasise the merged outline */
        DrawRectangle(sx,         sy,         w, 2,     (Color){ 0, 0, 0, 120 });
        DrawRectangle(sx,         sy + h - 2, w, 2,     (Color){ 0, 0, 0, 120 });
        DrawRectangle(sx,         sy,         2, h,     (Color){ 0, 0, 0, 120 });
        DrawRectangle(sx + w - 2, sy,         2, h,     (Color){ 0, 0, 0, 120 });

        const char *label = STRUCT_LABEL[s->type];
        int fs = ts * 34 / 100;
        int lw = MeasureText(label, fs);
        DrawText(label, sx + (w - lw) / 2, sy + (h - fs) / 2,
                 fs, (Color){ 255, 255, 255, 220 });
    }
}

static void draw_trail(void)
{
    if (!s_is_dragging || s_trail_len == 0) return;
    int ts = tile_px();
    for (int i = 0; i < s_trail_len; i++) {
        int col = s_trail_col[i];
        int row = s_trail_row[i];
        /* cull using logical vp with ±1 buffer to handle pan lag */
        if (col < s_vp_col - 1 || col >= s_vp_col + vis_cols() + 1) continue;
        if (row < s_vp_row - 1 || row >= s_vp_row + vis_rows() + 1) continue;
        int sx = (int)((col - s_vp_vis_col) * ts);
        int sy = (int)((row - s_vp_vis_row) * ts);

        /* gradient: i=0 oldest (dim), i=trail_len-1 newest (bright) */
        float gradient_t = (s_trail_len > 1)
            ? (float)i / (float)(s_trail_len - 1) : 1.0f;
        int base_alpha = TRAIL_ALPHA_OLD +
            (int)((TRAIL_ALPHA_NEW - TRAIL_ALPHA_OLD) * gradient_t);
        /* fade-in for freshly placed tiles */
        float fade_frac = fminf(s_trail_age[i] / TRAIL_FADEIN_SECS, 1.0f);
        int alpha = (int)(base_alpha * fade_frac);

        DrawRectangle(sx, sy, ts - 1, ts - 1,
                      (Color){ 255, 255, 255, (unsigned char)alpha });
        /* subtle border on the most recent TRAIL_BORDER_RECENT tiles */
        if (i >= s_trail_len - TRAIL_BORDER_RECENT) {
            DrawRectangleLinesEx(
                (Rectangle){ (float)sx, (float)sy, (float)(ts - 1), (float)(ts - 1) },
                1.5f, (Color){ 255, 255, 255, (unsigned char)(alpha / 2) });
        }
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
        int sx = (int)((col - s_vp_vis_col) * ts);
        int sy = (int)((row - s_vp_vis_row) * ts);
        /* bright white flash over the replaced tile */
        DrawRectangle(sx, sy, ts - 1, ts - 1, (Color){ 255, 255, 200, a });
        /* sparkle border */
        DrawRectangleLinesEx(
            (Rectangle){ (float)sx, (float)sy, (float)(ts - 1), (float)(ts - 1) },
            2.0f, (Color){ 255, 220, 60, (unsigned char)(a * 0.8f) });
    }
}

static void draw_flying_tiles(void)
{
    int ts = tile_px();
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    for (int _fi = 0; _fi < MAX_FLYING_TILES; _fi++) {
        FlyingTile *ft = &s_flying[_fi];
        if (!ft->active) continue;
        int sx = (int)((ft->vis_col - s_vp_vis_col) * ts);
        int sy = (int)((ft->vis_row - s_vp_vis_row) * ts);
        if (sx + ts < 0 || sx > sw || sy + ts < 0 || sy > sh) continue;
        DrawRectangle(sx, sy, ts - 1, ts - 1, TERRAIN_COLOR[ft->terrain]);
        /* white border signals this tile is in motion */
        DrawRectangleLinesEx(
            (Rectangle){ (float)sx, (float)sy, (float)(ts - 1), (float)(ts - 1) },
            2.0f, (Color){ 255, 255, 255, 160 });
        char letter[2] = { TERRAIN_LETTER[ft->terrain], '\0' };
        int  fs = ts * 28 / 100;
        int  lw = MeasureText(letter, fs);
        DrawText(letter, sx + (ts - lw) / 2, sy + (ts - fs) / 2,
                 fs, (Color){ 0, 0, 0, 70 });
    }
}

static void draw_hero(void)
{
    int   ts    = tile_px();
    float scale = 1.0f + s_hero_bump;

    /* visual float position → pixel center */
    float cx = (s_hero_vis_col - s_vp_vis_col) * ts + ts * 0.5f;
    float cy = (s_hero_vis_row - s_vp_vis_row) * ts + ts * 0.5f;

    /* scaled tile dimensions, centered on cx/cy */
    float w  = (ts - 1) * scale;
    float h  = (ts - 1) * scale;
    float sx = cx - w * 0.5f;
    float sy = cy - h * 0.5f;

    DrawRectangleV((Vector2){ sx, sy }, (Vector2){ w, h },
                   (Color){ 240, 220, 100, 255 });
    DrawRectangleLinesEx((Rectangle){ sx, sy, w, h },
                         2.0f, (Color){ 180, 140, 40, 255 });
    int  fs = (int)(ts * 36 / 100 * scale);
    int  lw = MeasureText("H", fs);
    DrawText("H", (int)(cx - lw * 0.5f), (int)(cy - fs * 0.5f),
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
    clear_structures();

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

    /* animation state — snap visual positions to logical at startup */
    s_hero_vis_col = (float)s_hero_col;
    s_hero_vis_row = (float)s_hero_row;
    s_vp_vis_col   = (float)s_vp_col;
    s_vp_vis_row   = (float)s_vp_row;
    s_hero_bump    = 0.0f;
    for (int _fi = 0; _fi < MAX_FLYING_TILES; _fi++)
        s_flying[_fi].active = false;
    memset(s_cell_flying_dst, 0, sizeof(s_cell_flying_dst));
    memset(s_trail_age, 0, sizeof(s_trail_age));

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

    /* Hero position is set — now detect any initial structures */
    detect_structures();
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

    /* ---- animation lerps ---- */
    {
        float k_hero = fminf(HERO_LERP_SPD * dt, 1.0f);
        s_hero_vis_col += ((float)s_hero_col - s_hero_vis_col) * k_hero;
        s_hero_vis_row += ((float)s_hero_row - s_hero_vis_row) * k_hero;

        float k_vp = fminf(VP_LERP_SPD * dt, 1.0f);
        s_vp_vis_col += ((float)s_vp_col - s_vp_vis_col) * k_vp;
        s_vp_vis_row += ((float)s_vp_row - s_vp_vis_row) * k_vp;

        if (s_hero_bump > 0.0f) {
            s_hero_bump -= BUMP_DECAY * dt;
            if (s_hero_bump < 0.0f) s_hero_bump = 0.0f;
        }

        for (int _fi = 0; _fi < MAX_FLYING_TILES; _fi++) {
            FlyingTile *ft = &s_flying[_fi];
            if (!ft->active) continue;
            float k_fly = fminf(FLY_LERP_SPD * dt, 1.0f);
            ft->vis_col += (ft->dst_col - ft->vis_col) * k_fly;
            ft->vis_row += (ft->dst_row - ft->vis_row) * k_fly;
            float dc = ft->vis_col - ft->dst_col;
            float dr = ft->vis_row - ft->dst_row;
            if (dc * dc + dr * dr < FLY_ARRIVE_DIST * FLY_ARRIVE_DIST) {
                ft->active = false;
                int dst_c = (int)(ft->dst_col + 0.5f);
                int dst_r = (int)(ft->dst_row + 0.5f);
                if (dst_c >= 0 && dst_c < MAP_COLS && dst_r >= 0 && dst_r < MAP_ROWS)
                    s_cell_flying_dst[dst_r][dst_c] = false;
            }
        }

        for (int _ti = 0; _ti < s_trail_len; _ti++) {
            s_trail_age[_ti] += dt;
            if (s_trail_age[_ti] > TRAIL_FADEIN_SECS)
                s_trail_age[_ti] = TRAIL_FADEIN_SECS;
        }
    }
}

static void RealmWalkDraw(void)
{
    ClearBackground((Color){ 34, 40, 49, 255 });
    draw_map_tiles();
    draw_trail();
    draw_scan_flash();
    draw_flying_tiles();
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
