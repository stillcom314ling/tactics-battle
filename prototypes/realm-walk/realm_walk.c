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
#define MOVE_COOLDOWN    0.12f  /* seconds between hero steps while dragging */
#define LEGEND_SPEED     8.0f   /* legend panel animation speed             */
#define SCAN_FLASH_SECS  0.60f
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

/* ---- scoring constants ---- */
#define PT_TRAIL_STEP    1           /* points per trail tile walked    */
#define PT_DENSE_FOREST  8           /* Dense Forest contributes/turn   */
#define PT_FARM          6           /* Farm contributes/turn           */
#define PT_CASTLE        20          /* Castle contributes/turn         */
#define TURNS_LIMIT      20          /* turns before game ends          */
#define SCORE_GOAL       300         /* score needed to win             */
#define POPUP_LIFETIME   1.4f        /* seconds a score popup lives     */
#define POPUP_FLOAT      2.2f        /* cells upward over its lifetime  */
#define MAX_SCORE_POPUPS 16

/* ------------------------------------------------------------------- types */

typedef enum {
    TERRAIN_PLAINS = 0,
    TERRAIN_FOREST,
    TERRAIN_MOUNTAIN,
    TERRAIN_CITY,
    TERRAIN_WATER,
    TERRAIN_COUNT
} Terrain;

typedef enum { PHASE_IDLE, PHASE_DRAGGING, PHASE_SCANNING } Phase;

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

typedef struct {
    float tile_col, tile_row;   /* map-space position (floats upward)   */
    int   value;
    float age;                  /* 0 = just spawned                     */
    bool  active;
} ScorePopup;

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

/* UI */
static bool  s_hud_open;
static float s_hud_t;       /* 0 = closed, 1 = open */
static bool  s_legend_open;
static float s_legend_t;    /* 0 = closed, 1 = open */
static float s_scan_flash;  /* counts down from SCAN_FLASH_SECS */

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

/* ---- scoring state ---- */
static int        s_score;
static int        s_turn_count;
static bool       s_game_over;
static ScorePopup s_popups[MAX_SCORE_POPUPS];

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

/* HUD tab: compact strip pinned to the top of the screen */
static Rectangle hud_tab_rect(void)
{
    int sw    = GetScreenWidth();
    int sh    = GetScreenHeight();
    int tab_h = sh * 6 / 100;
    return (Rectangle){ 0, 0, (float)sw, (float)tab_h };
}

/* HUD body: slides down from below the tab when open */
static Rectangle hud_body_rect(void)
{
    int sw     = GetScreenWidth();
    int sh     = GetScreenHeight();
    int tab_h  = sh *  6 / 100;
    int body_h = sh * 22 / 100;
    float y = (float)tab_h + (s_hud_t - 1.0f) * (float)body_h;
    return (Rectangle){ 0, y, (float)sw, (float)body_h };
}

/* Legend tab: fixed strip at the very bottom of the screen */
static Rectangle legend_tab_rect(void)
{
    int sw    = GetScreenWidth();
    int sh    = GetScreenHeight();
    int tab_h = sh * 7 / 100;
    return (Rectangle){ 0, (float)(sh - tab_h), (float)sw, (float)tab_h };
}

/* Legend body: slides up from behind the tab */
static Rectangle legend_body_rect(void)
{
    int sw     = GetScreenWidth();
    int sh     = GetScreenHeight();
    int tab_h  = sh *  7 / 100;
    int body_h = sh * 50 / 100;
    float y = (float)(sh - tab_h) - s_legend_t * (float)body_h;
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

static void spawn_popup(float tile_col, float tile_row, int value)
{
    for (int i = 0; i < MAX_SCORE_POPUPS; i++) {
        if (!s_popups[i].active) {
            s_popups[i] = (ScorePopup){
                .tile_col = tile_col,
                .tile_row = tile_row,
                .value    = value,
                .age      = 0.0f,
                .active   = true,
            };
            return;
        }
    }
    /* all slots busy — overwrite the oldest (highest age) */
    int oldest = 0;
    for (int i = 1; i < MAX_SCORE_POPUPS; i++)
        if (s_popups[i].age > s_popups[oldest].age) oldest = i;
    s_popups[oldest] = (ScorePopup){
        .tile_col = tile_col,
        .tile_row = tile_row,
        .value    = value,
        .age      = 0.0f,
        .active   = true,
    };
}

static void award_turn_score(void)
{
    s_turn_count++;

    /* trail steps: 1 pt each — popup centered at hero */
    int trail_pts = s_trail_len * PT_TRAIL_STEP;
    if (trail_pts > 0)
        spawn_popup((float)s_hero_col + 0.5f,
                    (float)s_hero_row - 0.2f, trail_pts);

    int total = trail_pts;

    /* each structure on the map contributes every turn */
    for (int i = 0; i < s_structure_count; i++) {
        int pts = 0;
        switch (s_structures[i].type) {
            case STRUCT_DENSE_FOREST: pts = PT_DENSE_FOREST; break;
            case STRUCT_FARM:         pts = PT_FARM;         break;
            case STRUCT_CASTLE:       pts = PT_CASTLE;       break;
            default: break;
        }
        if (pts > 0) {
            total += pts;
            /* popup centered on the structure */
            float sc = s_structures[i].col + s_structures[i].w * 0.5f;
            float sr = s_structures[i].row + s_structures[i].h * 0.5f - 0.5f;
            spawn_popup(sc, sr, pts);
        }
    }

    s_score += total;
    if (s_turn_count >= TURNS_LIMIT) s_game_over = true;
}

static void end_turn(void)
{
    s_is_dragging = false;
    s_flash_count = 0;
    scan_matches();
    award_turn_score();
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
    if (s_game_over) return;
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

    /* HUD tab toggle */
    if (CheckCollisionPointRec(pos, hud_tab_rect())) {
        s_hud_open = !s_hud_open;
        return;
    }

    /* legend tab toggle */
    if (CheckCollisionPointRec(pos, legend_tab_rect())) {
        s_legend_open = !s_legend_open;
    }
}

/* --------------------------------------------------------------- rendering */

static void draw_map_tiles(void)
{
    int ts = tile_px();
    int vc = vis_cols();
    int vr = vis_rows();

    /* Pass 1: draw individual terrain tiles (skip cells that belong to a
     * structure — those are rendered as one large tile in pass 2).
     * Expand loop by 1 cell each side to cover fractional viewport lag. */
    for (int row = s_vp_row - 1; row < s_vp_row + vr + 1 && row < MAP_ROWS; row++) {
        if (row < 0) continue;
        for (int col = s_vp_col - 1; col < s_vp_col + vc + 1 && col < MAP_COLS; col++) {
            if (col < 0) continue;
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
        /* cull off-screen (expanded by 1 to match tile loop) */
        if (s->col + s->w <= s_vp_col - 1 || s->col >= s_vp_col + vc + 1) continue;
        if (s->row + s->h <= s_vp_row - 1 || s->row >= s_vp_row + vr + 1) continue;

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
    int   sw   = GetScreenWidth();
    int   sh   = GetScreenHeight();
    int   top  = sh * 6 / 100;   /* always just below the HUD tab */
    int   bh   = sh / 60;
    float frac = s_turn_timer / TURN_SECS;
    Color col  = frac > 0.57f ? (Color){  80, 200,  80, 230 }
               : frac > 0.28f ? (Color){ 220, 200,  60, 230 }
                              : (Color){ 220,  60,  60, 230 };
    DrawRectangle(0, top, (int)((float)sw * frac), bh, col);
    DrawRectangle((int)((float)sw * frac), top,
                  sw - (int)((float)sw * frac), bh,
                  (Color){ 50, 50, 50, 180 });
}

static void draw_resource_strip(void)
{
    int sw  = GetScreenWidth();
    int sh  = GetScreenHeight();
    int pad = sw / 30;

    /* --- body slides down from below the tab --- */
    if (s_hud_t > 0.02f) {
        Rectangle dr = hud_body_rect();
        DrawRectangleRec(dr, (Color){ 20, 22, 30, 240 });
        DrawRectangleLinesEx(dr, 1.5f, (Color){ 60, 75, 100, 200 });

        if (s_hud_t > 0.3f) {
            int bfs = (int)(dr.height * 0.26f);
            if (bfs < 14) bfs = 14;
            int row1 = (int)(dr.y + dr.height * 0.15f);
            int row2 = (int)(dr.y + dr.height * 0.55f);

            /* Score */
            char score_buf[32];
            snprintf(score_buf, sizeof(score_buf), "Score   %d", s_score);
            Color sc = (s_score >= SCORE_GOAL) ? (Color){ 100, 240, 100, 255 }
                                               : (Color){ 255, 215,  70, 255 };
            DrawText(score_buf, pad, row1, bfs, sc);

            /* Goal */
            char goal_buf[32];
            snprintf(goal_buf, sizeof(goal_buf), "Goal    %d", SCORE_GOAL);
            int gw = MeasureText(goal_buf, bfs);
            DrawText(goal_buf, (sw - gw) / 2, row1, bfs,
                     (Color){ 120, 130, 150, 210 });

            /* Turns */
            char turn_buf[32];
            snprintf(turn_buf, sizeof(turn_buf), "Turn    %d / %d",
                     s_turn_count, TURNS_LIMIT);
            int tw = MeasureText(turn_buf, bfs);
            int turns_left = TURNS_LIMIT - s_turn_count;
            Color tc = (turns_left <= 0)
                           ? (Color){ 255,  80,  80, 255 }
                       : (turns_left <= TURNS_LIMIT * 3 / 10)
                           ? (Color){ 255, 160,  60, 255 }
                       :   (Color){ 200, 200, 200, 220 };
            DrawText(turn_buf, sw - tw - pad, row1, bfs, tc);

            /* Progress bar toward goal */
            float prog = (float)s_score / (float)SCORE_GOAL;
            if (prog > 1.0f) prog = 1.0f;
            int bar_x = pad;
            int bar_w = sw - pad * 2;
            int bar_h = sh * 3 / 100;
            DrawRectangle(bar_x, row2, bar_w, bar_h,
                          (Color){ 40, 45, 55, 220 });
            DrawRectangle(bar_x, row2, (int)(bar_w * prog), bar_h,
                          (Color){ 255, 215, 70, 220 });
            DrawRectangleLinesEx(
                (Rectangle){ (float)bar_x, (float)row2,
                             (float)bar_w, (float)bar_h },
                1.0f, (Color){ 80, 90, 110, 200 });

            /* progress label */
            char prog_buf[32];
            snprintf(prog_buf, sizeof(prog_buf), "%d%%",
                     (int)(prog * 100.0f));
            int pfs = bar_h * 80 / 100;
            if (pfs < 10) pfs = 10;
            int pw = MeasureText(prog_buf, pfs);
            DrawText(prog_buf, (sw - pw) / 2,
                     row2 + (bar_h - pfs) / 2,
                     pfs, (Color){ 20, 20, 20, 200 });
        }
    }

    /* --- always-visible tab --- */
    Rectangle tab   = hud_tab_rect();
    int       tab_h = (int)tab.height;
    int       fs    = tab_h * 46 / 100;
    DrawRectangleRec(tab, (Color){ 20, 22, 30, 220 });
    DrawLine(0, tab_h, sw, tab_h, (Color){ 60, 75, 100, 180 });

    /* compact summary: Score left, Turn right, arrow centre */
    char score_buf[24];
    snprintf(score_buf, sizeof(score_buf), "%d", s_score);
    Color sc = (s_score >= SCORE_GOAL) ? (Color){ 100, 240, 100, 255 }
                                       : (Color){ 255, 215,  70, 255 };
    DrawText(score_buf, pad, (tab_h - fs) / 2, fs, sc);

    char turn_buf[24];
    snprintf(turn_buf, sizeof(turn_buf), "%d/%d", s_turn_count, TURNS_LIMIT);
    int tw = MeasureText(turn_buf, fs);
    int turns_left = TURNS_LIMIT - s_turn_count;
    Color tc = (turns_left <= 0)
                   ? (Color){ 255,  80,  80, 255 }
               : (turns_left <= TURNS_LIMIT * 3 / 10)
                   ? (Color){ 255, 160,  60, 255 }
               :   (Color){ 200, 200, 200, 220 };
    DrawText(turn_buf, sw - tw - pad, (tab_h - fs) / 2, fs, tc);

    const char *arrow = s_hud_open ? "^" : "v";
    int aw = MeasureText(arrow, fs);
    DrawText(arrow, (sw - aw) / 2, (tab_h - fs) / 2, fs,
             (Color){ 160, 160, 160, 200 });
}

/* Draw a mini flat terrain grid for legend entries. */
static void draw_legend_grid(int x, int y, int cs,
                              const Terrain *cells, int cols, int rows)
{
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            Terrain t = cells[r * cols + c];
            DrawRectangle(x + c * cs, y + r * cs, cs - 1, cs - 1,
                          TERRAIN_COLOR[t]);
            char ltr[2] = { TERRAIN_LETTER[t], '\0' };
            int  lfs = cs * 38 / 100;
            if (lfs < 6) lfs = 6;
            int  lw  = MeasureText(ltr, lfs);
            DrawText(ltr,
                     x + c * cs + (cs - lw)  / 2,
                     y + r * cs + (cs - lfs) / 2,
                     lfs, (Color){ 0, 0, 0, 100 });
        }
    }
}

static void draw_legend(void)
{
    int sw = GetScreenWidth();

    /* --- always-visible tab --- */
    Rectangle tab    = legend_tab_rect();
    int       tab_fs = (int)(tab.height * 0.42f);
    DrawRectangleRec(tab, (Color){ 30, 36, 48, 240 });
    DrawRectangleLinesEx(tab, 1.5f, (Color){ 70, 85, 110, 255 });

    const char *label = "Tile Guide";
    int tlw = MeasureText(label, tab_fs);
    DrawText(label, (sw - tlw) / 2,
             (int)(tab.y + (tab.height - tab_fs) / 2),
             tab_fs, (Color){ 180, 190, 210, 240 });

    const char *arrow = s_legend_open ? "v" : "^";
    int aw = MeasureText(arrow, tab_fs);
    DrawText(arrow, sw - aw - 12,
             (int)(tab.y + (tab.height - tab_fs) / 2),
             tab_fs, (Color){ 160, 160, 160, 200 });

    if (s_legend_t < 0.05f) return;

    /* --- sliding body --- */
    Rectangle dr  = legend_body_rect();
    int       pad = (int)(dr.width * 0.04f);
    DrawRectangleRec(dr, (Color){ 25, 30, 42, 235 });
    DrawRectangleLinesEx(dr, 1.5f, (Color){ 60, 75, 100, 200 });

    if (s_legend_t < 0.25f) return;

    static const Terrain s_forest_cells[4] = {
        TERRAIN_FOREST,   TERRAIN_FOREST,
        TERRAIN_FOREST,   TERRAIN_FOREST,
    };
    static const Terrain s_farm_cells[4] = {
        TERRAIN_PLAINS,   TERRAIN_PLAINS,
        TERRAIN_PLAINS,   TERRAIN_PLAINS,
    };
    static const Terrain s_castle_cells[9] = {
        TERRAIN_WATER,    TERRAIN_WATER,    TERRAIN_WATER,
        TERRAIN_WATER,    TERRAIN_MOUNTAIN, TERRAIN_WATER,
        TERRAIN_WATER,    TERRAIN_WATER,    TERRAIN_WATER,
    };

    static const char *s_names[4] = {
        "Dense Forest", "Farm", "Castle", "Trail Step"
    };
    static const char *s_descs[4] = {
        "2x2 Forest tiles", "2x2 Plains tiles",
        "Mountain + Water ring", "Each step on trail"
    };
    static const int s_pts[4] = {
        PT_DENSE_FOREST, PT_FARM, PT_CASTLE, PT_TRAIL_STEP
    };

    int n_rows    = 4;
    int row_h     = (int)(dr.height / (float)n_rows);
    int diagram_w = row_h;

    for (int i = 0; i < n_rows; i++) {
        int ry = (int)dr.y + i * row_h;

        if (i > 0)
            DrawLine((int)(dr.x + pad), ry,
                     (int)(dr.x + dr.width - pad), ry,
                     (Color){ 60, 70, 90, 160 });

        /* mini diagram */
        if (i < 3) {
            int g_cols = (i == 2) ? 3 : 2;
            int g_rows = (i == 2) ? 3 : 2;
            int cs = diagram_w / (g_cols + 1);
            int gx = (int)(dr.x + pad) + (diagram_w - g_cols * cs) / 2;
            int gy = ry + (row_h - g_rows * cs) / 2;
            const Terrain *cells = (i == 0) ? s_forest_cells
                                 : (i == 1) ? s_farm_cells
                                            : s_castle_cells;
            draw_legend_grid(gx, gy, cs, cells, g_cols, g_rows);
        } else {
            /* trail row: hero glyph placeholder */
            int cs = diagram_w / 3;
            int gx = (int)(dr.x + pad) + (diagram_w - cs) / 2;
            int gy = ry + (row_h - cs) / 2;
            DrawRectangle(gx, gy, cs - 1, cs - 1, (Color){ 240, 220, 100, 255 });
            int hfs = cs * 55 / 100;
            if (hfs < 6) hfs = 6;
            int hw  = MeasureText("H", hfs);
            DrawText("H", gx + (cs - hw) / 2, gy + (cs - hfs) / 2,
                     hfs, (Color){ 80, 50, 10, 200 });
        }

        /* name + description */
        int text_x = (int)(dr.x + pad + diagram_w + pad);
        int name_fs = row_h * 30 / 100;
        int desc_fs = row_h * 22 / 100;
        if (name_fs < 12) name_fs = 12;
        if (desc_fs < 10) desc_fs = 10;

        DrawText(s_names[i], text_x,
                 ry + (row_h / 2 - name_fs) / 2,
                 name_fs, (Color){ 220, 225, 235, 240 });
        DrawText(s_descs[i], text_x,
                 ry + row_h / 2,
                 desc_fs, (Color){ 140, 150, 170, 200 });

        /* points label — right-aligned */
        char pts_buf[24];
        snprintf(pts_buf, sizeof(pts_buf),
                 (i < 3) ? "+%d pt/turn" : "+%d pt/step", s_pts[i]);
        int   pts_fs = name_fs;
        int   pts_w  = MeasureText(pts_buf, pts_fs);
        Color pts_c  = (s_pts[i] >= PT_CASTLE)       ? (Color){ 255, 215,  60, 255 }
                     : (s_pts[i] >= PT_DENSE_FOREST)  ? (Color){ 100, 220, 130, 255 }
                     :                                   (Color){ 180, 200, 220, 230 };
        DrawText(pts_buf,
                 (int)(dr.x + dr.width - pad - pts_w),
                 ry + (row_h - pts_fs) / 2,
                 pts_fs, pts_c);
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

    /* scoring state */
    s_score      = 0;
    s_turn_count = 0;
    s_game_over  = false;
    for (int _pi = 0; _pi < MAX_SCORE_POPUPS; _pi++)
        s_popups[_pi].active = false;

    s_is_dragging       = false;
    s_phase             = PHASE_IDLE;
    s_turn_timer        = TURN_SECS;
    s_trail_len         = 0;
    s_scan_flash        = 0.0f;
    s_scanned_col_count = 0;
    s_scanned_row_count = 0;

    memset(s_col_visited,   0, sizeof(s_col_visited));
    memset(s_row_visited,   0, sizeof(s_row_visited));

    s_flash_count   = 0;
    s_move_cd       = 0.0f;
    s_hud_open      = false;
    s_hud_t         = 0.0f;
    s_legend_open   = false;
    s_legend_t      = 0.0f;
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

    /* HUD + legend panel animations */
    float hud_target = s_hud_open ? 1.0f : 0.0f;
    s_hud_t += (hud_target - s_hud_t) * fminf(LEGEND_SPEED * dt, 1.0f);

    float leg_target = s_legend_open ? 1.0f : 0.0f;
    s_legend_t += (leg_target - s_legend_t) * fminf(LEGEND_SPEED * dt, 1.0f);

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

        /* score popups float upward and fade out */
        for (int _pi = 0; _pi < MAX_SCORE_POPUPS; _pi++) {
            ScorePopup *p = &s_popups[_pi];
            if (!p->active) continue;
            p->age += dt;
            p->tile_row -= (POPUP_FLOAT / POPUP_LIFETIME) * dt;
            if (p->age >= POPUP_LIFETIME) p->active = false;
        }
    }
}

static void draw_score_popups(void)
{
    int ts = tile_px();
    for (int _pi = 0; _pi < MAX_SCORE_POPUPS; _pi++) {
        ScorePopup *p = &s_popups[_pi];
        if (!p->active) continue;

        /* ease out: fast rise at start, slows near top */
        float t     = p->age / POPUP_LIFETIME;
        float alpha = (1.0f - t) * (1.0f - t);
        unsigned char a = (unsigned char)(alpha * 255.0f);

        float cx = (p->tile_col - s_vp_vis_col) * ts;
        float cy = (p->tile_row - s_vp_vis_row) * ts;

        char buf[16];
        snprintf(buf, sizeof(buf), "+%d", p->value);
        int fs = ts * 42 / 100;
        if (fs < 14) fs = 14;
        int lw = MeasureText(buf, fs);

        /* drop shadow */
        DrawText(buf, (int)cx - lw / 2 + 2, (int)cy + 2, fs,
                 (Color){ 0, 0, 0, (unsigned char)(a / 2) });
        /* main text — warm gold */
        DrawText(buf, (int)cx - lw / 2, (int)cy, fs,
                 (Color){ 255, 215, 60, a });
    }
}

static void draw_end_game(void)
{
    if (!s_game_over) return;
    int sw = GetScreenWidth(), sh = GetScreenHeight();

    DrawRectangle(0, 0, sw, sh, (Color){ 0, 0, 0, 170 });

    bool won = (s_score >= SCORE_GOAL);
    const char *title   = won ? "GOAL REACHED" : "TURNS EXHAUSTED";
    Color       title_c = won ? (Color){ 100, 240, 100, 255 }
                               : (Color){ 255,  80,  80, 255 };
    int tfs = sh / 9;
    int tw  = MeasureText(title, tfs);
    DrawText(title, (sw - tw) / 2, sh * 3 / 10, tfs, title_c);

    char score_buf[48];
    snprintf(score_buf, sizeof(score_buf), "Final Score  %d  /  %d", s_score, SCORE_GOAL);
    int sfs = sh / 18;
    int sw2 = MeasureText(score_buf, sfs);
    DrawText(score_buf, (sw - sw2) / 2, sh * 3 / 10 + tfs + sh / 20,
             sfs, (Color){ 220, 220, 220, 255 });

    const char *hint = "Press ESC to return to menu";
    int hfs = sh / 26;
    int hw  = MeasureText(hint, hfs);
    DrawText(hint, (sw - hw) / 2, sh * 7 / 10, hfs, (Color){ 140, 140, 150, 200 });
}

static void RealmWalkDraw(void)
{
    ClearBackground((Color){ 34, 40, 49, 255 });
    draw_map_tiles();
    draw_trail();
    draw_scan_flash();
    draw_flying_tiles();
    draw_hero();
    draw_score_popups();
    draw_resource_strip();
    draw_timer_bar();
    draw_legend();
    draw_end_game();
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
