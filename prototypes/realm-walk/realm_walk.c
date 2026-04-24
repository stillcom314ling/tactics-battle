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
#define TURN_SECS        3.5f
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
#define PT_WHEAT_FIELD   4           /* Wheat Field (1×3 Plains)        */
#define PT_ROAD          5           /* Road (1×3 City)                 */
#define PT_FARM          6           /* Farm (2×2 Plains)               */
#define PT_QUARRY        6           /* Quarry (1×3 Mountain)           */
#define PT_RIVER         7           /* River (1×4 Water)               */
#define PT_DENSE_FOREST  8           /* Dense Forest (2×2 Forest)       */
#define PT_FOREST_CORNER 6           /* Forest Corner (L 3× Forest)     */
#define PT_RIVER_BEND    8           /* River Bend (L 4× Water)         */
#define PT_CROSSROADS    9           /* Crossroads (+ 5× City)          */
#define PT_LUMBER_CAMP   11          /* Lumber Camp (3×3 City+Forest)   */
#define PT_CASTLE        18          /* Castle (3×3 Mountain+Water)     */
#define MAX_STRUCT_CELLS 9           /* max cells in any structure shape */
#define TURNS_LIMIT      15          /* turns before game ends          */
#define SCORE_GOAL       3000        /* score needed to win             */
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
    STRUCT_DENSE_FOREST,   /* 2×2 Forest                       */
    STRUCT_FARM,           /* 2×2 Plains                       */
    STRUCT_CASTLE,         /* 3×3 Mountain center + Water ring */
    STRUCT_LUMBER_CAMP,    /* 3×3 City center + Forest ring    */
    STRUCT_RIVER,          /* 1×4 Water (H or V)               */
    STRUCT_WHEAT_FIELD,    /* 1×3 Plains (H or V)              */
    STRUCT_ROAD,           /* 1×3 City (H or V)                */
    STRUCT_QUARRY,         /* 1×3 Mountain (H or V)            */
    STRUCT_FOREST_CORNER,  /* L-shape 3× Forest (4 rotations)  */
    STRUCT_RIVER_BEND,     /* L-shape 4× Water  (4 rotations)  */
    STRUCT_CROSSROADS,     /* + shape  5× City  (symmetric)    */
    STRUCT_TYPE_COUNT
} StructureType;

typedef struct {
    StructureType type;
    int           col, row;                   /* top-left of bounding box  */
    int           w, h;                       /* bounding box in tiles     */
    int           cell_count;
    int           cell_dc[MAX_STRUCT_CELLS];  /* offsets from (col, row)   */
    int           cell_dr[MAX_STRUCT_CELLS];
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

#define MAX_STRUCTURES  32
#define MAX_ORDER_REQS   3   /* terrain requirements per order */
#define ORDER_COUNT      4   /* orders shown simultaneously    */
#define MAX_GROUPS     300   /* max connected same-terrain groups on map */

typedef struct {
    Terrain terrain[MAX_ORDER_REQS];
    int     min_size[MAX_ORDER_REQS];
    int     req_count;
} Order;

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
static int   s_pending_moves;    /* moves taken since last cash-in */

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
/* cells the hero stepped on this turn — structures only form here */
static bool      s_visited_cell[MAP_ROWS][MAP_COLS];

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

/* ---- orders ---- */
static Order s_orders[ORDER_COUNT];

/* ---- group data (rebuilt on cash-in) ---- */
static int s_group_map[MAP_ROWS][MAP_COLS];         /* cell → group idx, -1 = none */
static int s_group_terrain[MAX_GROUPS];
static int s_group_size[MAX_GROUPS];
static int s_group_start[MAX_GROUPS + 1];           /* prefix offsets into flat arrays */
static int s_group_col_flat[MAP_ROWS * MAP_COLS];
static int s_group_row_flat[MAP_ROWS * MAP_COLS];
static int s_group_count;

/* -------------------------------------------------------------- data tables */

static const Color TERRAIN_COLOR[TERRAIN_COUNT] = {
    { 168, 255, 120, 255 }, /* PLAINS   – electric lime    */
    {  80, 230, 160, 255 }, /* FOREST   – mint emerald     */
    { 195, 175, 255, 255 }, /* MOUNTAIN – bright lavender  */
    { 255, 200,  80, 255 }, /* CITY     – amber gold       */
    {  70, 210, 255, 255 }, /* WATER    – electric cyan    */
};

static const char TERRAIN_LETTER[TERRAIN_COUNT] = { 'P', 'F', 'M', 'C', 'W' };

static const Color STRUCT_COLOR[STRUCT_TYPE_COUNT] = {
    {   0,   0,   0,   0 }, /* NONE           */
    {  50, 210,  95, 255 }, /* DENSE_FOREST   – vivid green    */
    { 245, 230, 100, 255 }, /* FARM           – bright gold     */
    { 220, 230, 255, 255 }, /* CASTLE         – ice white       */
    { 100, 215, 135, 255 }, /* LUMBER_CAMP    – jade            */
    {  50, 175, 255, 255 }, /* RIVER          – cobalt blue     */
    { 255, 240, 130, 255 }, /* WHEAT_FIELD    – lemon           */
    { 255, 175,  80, 255 }, /* ROAD           – bright orange   */
    { 205, 185, 255, 255 }, /* QUARRY         – light violet    */
    {  60, 225, 115, 255 }, /* FOREST_CORNER  – bright teal     */
    {  75, 195, 255, 255 }, /* RIVER_BEND     – bright cyan     */
    { 255, 200,  65, 255 }, /* CROSSROADS     – warm gold       */
};

static const char *STRUCT_LABEL[STRUCT_TYPE_COUNT] = {
    "", "Dense Forest", "Farm", "Castle",
    "Lumber Camp", "River", "Wheat Field", "Road", "Quarry",
    "Forest Corner", "River Bend", "Crossroads"
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

/* Cash In button: lower-right corner, just above the legend tab */
static Rectangle cashin_btn_rect(void)
{
    int sw     = GetScreenWidth();
    int sh     = GetScreenHeight();
    int bh     = sh * 8 / 100;
    int bw     = sw * 22 / 100;
    int leg_h  = sh * 7 / 100;
    int margin = sh * 2 / 100;
    return (Rectangle){
        (float)(sw - bw - margin),
        (float)(sh - leg_h - bh - margin),
        (float)bw, (float)bh
    };
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
    int body_h = sh * 65 / 100;
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

    /* Bounds check each cell in the structure's new position */
    for (int i = 0; i < s->cell_count; i++) {
        int cc = nc + s->cell_dc[i];
        int cr = nr + s->cell_dr[i];
        if (cc < 0 || cc >= MAP_COLS || cr < 0 || cr >= MAP_ROWS) return false;
    }

    /* Don't allow collision with another structure at the new cell positions */
    for (int i = 0; i < s->cell_count; i++) {
        int ci = s_cell_struct[nr + s->cell_dr[i]][nc + s->cell_dc[i]];
        if (ci >= 0 && ci != si) return false;
    }

    /* Cell swap: exchange terrain between old and new positions for each cell */
    Terrain old_struct[MAX_STRUCT_CELLS];
    Terrain old_dest[MAX_STRUCT_CELLS];
    for (int i = 0; i < s->cell_count; i++) {
        old_struct[i] = s_map[s->row + s->cell_dr[i]][s->col + s->cell_dc[i]];
        old_dest[i]   = s_map[nr     + s->cell_dr[i]][nc     + s->cell_dc[i]];
    }
    for (int i = 0; i < s->cell_count; i++) {
        s_map[s->row + s->cell_dr[i]][s->col + s->cell_dc[i]] = old_dest[i];
        s_map[nr     + s->cell_dr[i]][nc     + s->cell_dc[i]] = old_struct[i];
    }

    /* Move the structure's cell-index footprint.
     * Shift is always ±w or ±h, so old and new positions never overlap. */
    for (int i = 0; i < s->cell_count; i++)
        s_cell_struct[s->row + s->cell_dr[i]][s->col + s->cell_dc[i]] = -1;
    for (int i = 0; i < s->cell_count; i++)
        s_cell_struct[nr + s->cell_dr[i]][nc + s->cell_dc[i]] = si;

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
    s_visited_cell[new_row][new_col] = true;

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

static bool add_structure_cells(StructureType type,
                                 const int *dcs, const int *drs, int n,
                                 int ac, int ar)
{
    if (s_structure_count >= MAX_STRUCTURES) return false;
    int max_dc = 0, max_dr = 0;
    for (int i = 0; i < n; i++) {
        if (dcs[i] > max_dc) max_dc = dcs[i];
        if (drs[i] > max_dr) max_dr = drs[i];
    }
    int idx = s_structure_count++;
    Structure *s = &s_structures[idx];
    s->type       = type;
    s->col        = ac;
    s->row        = ar;
    s->w          = max_dc + 1;
    s->h          = max_dr + 1;
    s->cell_count = n;
    for (int i = 0; i < n; i++) {
        s->cell_dc[i] = dcs[i];
        s->cell_dr[i] = drs[i];
        s_cell_struct[ar + drs[i]][ac + dcs[i]] = idx;
    }
    return true;
}

static bool try_2x2(StructureType type, Terrain t, int c, int r)
{
    if (c + 2 > MAP_COLS || r + 2 > MAP_ROWS) return false;
    static const int dcs[4] = { 0, 1, 0, 1 };
    static const int drs[4] = { 0, 0, 1, 1 };
    bool any_visited = false;
    for (int i = 0; i < 4; i++) {
        int cc = c + dcs[i], cr = r + drs[i];
        if (s_cell_struct[cr][cc] >= 0)    return false;
        if (s_map[cr][cc] != t)            return false;
        if (cell_has_hero(cc, cr))         return false;
        if (s_visited_cell[cr][cc])        any_visited = true;
    }
    if (!any_visited) return false;
    return add_structure_cells(type, dcs, drs, 4, c, r);
}

/* 3×3 center+ring detector. Center tile = center_t, all 8 neighbours = ring_t. */
static bool try_ring_3x3(StructureType type,
                          Terrain center_t, Terrain ring_t,
                          int cc, int cr)
{
    if (cc < 1 || cc >= MAP_COLS - 1) return false;
    if (cr < 1 || cr >= MAP_ROWS - 1) return false;
    if (s_map[cr][cc] != center_t) return false;
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++) {
            if (dc == 0 && dr == 0) continue;
            if (s_map[cr + dr][cc + dc] != ring_t) return false;
        }
    int ac = cc - 1, ar = cr - 1;
    /* Build 9-cell offset list for the 3×3 block */
    int dcs[9], drs[9];
    int n = 0;
    bool any_visited = false;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            if (s_cell_struct[ar + r][ac + c] >= 0)   return false;
            if (cell_has_hero(ac + c, ar + r))         return false;
            if (s_visited_cell[ar + r][ac + c])        any_visited = true;
            dcs[n] = c; drs[n] = r; n++;
        }
    if (!any_visited) return false;
    return add_structure_cells(type, dcs, drs, 9, ac, ar);
}

static bool try_castle(int cc, int cr)
{
    return try_ring_3x3(STRUCT_CASTLE, TERRAIN_MOUNTAIN, TERRAIN_WATER, cc, cr);
}

/* Generic linear (row or column) structure detector. */
static bool try_row(StructureType type, Terrain t,
                    int c, int r, int w, int h)
{
    if (c + w > MAP_COLS || r + h > MAP_ROWS) return false;
    int dcs[MAX_STRUCT_CELLS], drs[MAX_STRUCT_CELLS];
    int n = 0;
    bool any_visited = false;
    for (int dr = 0; dr < h; dr++)
        for (int dc = 0; dc < w; dc++) {
            if (s_cell_struct[r + dr][c + dc] >= 0) return false;
            if (s_map[r + dr][c + dc] != t)         return false;
            if (cell_has_hero(c + dc, r + dr))      return false;
            if (s_visited_cell[r + dr][c + dc])     any_visited = true;
            dcs[n] = dc; drs[n] = dr; n++;
        }
    if (!any_visited) return false;
    return add_structure_cells(type, dcs, drs, n, c, r);
}

/* Rotate (dc, dr) 90° clockwise in screen space (y-down): (dc,dr) → (-dr, dc) */
static void rot90(int *dc, int *dr) { int t = *dc; *dc = -(*dr); *dr = t; }

/* Try all 8 orientations (4 rotations × original + horizontal mirror) of a
 * cell-offset shape at anchor (ac, ar). Normalises each so min offset = (0,0).
 * Symmetric shapes produce duplicate normalised layouts on the mirror pass;
 * those attempts simply fail the s_cell_struct check and are skipped. */
static bool try_shape(StructureType type, Terrain t,
                      const int *base_dcs, const int *base_drs, int n,
                      int ac, int ar)
{
    int dcs[MAX_STRUCT_CELLS], drs[MAX_STRUCT_CELLS];

    for (int pass = 0; pass < 2; pass++) {
        /* pass 0 = original, pass 1 = horizontally mirrored (dc → -dc) */
        for (int i = 0; i < n; i++) {
            dcs[i] = (pass == 0) ? base_dcs[i] : -base_dcs[i];
            drs[i] = base_drs[i];
        }

        for (int rot = 0; rot < 4; rot++) {
            /* Normalise: shift so min_dc = 0, min_dr = 0 */
            int min_dc = dcs[0], min_dr = drs[0];
            for (int i = 1; i < n; i++) {
                if (dcs[i] < min_dc) min_dc = dcs[i];
                if (drs[i] < min_dr) min_dr = drs[i];
            }
            int ndcs[MAX_STRUCT_CELLS], ndrs[MAX_STRUCT_CELLS];
            for (int i = 0; i < n; i++) {
                ndcs[i] = dcs[i] - min_dc;
                ndrs[i] = drs[i] - min_dr;
            }

            /* Bounds, terrain, structure, and visited checks */
            bool ok = true;
            bool any_visited = false;
            for (int i = 0; i < n && ok; i++) {
                int cc = ac + ndcs[i], cr = ar + ndrs[i];
                if (cc < 0 || cc >= MAP_COLS || cr < 0 || cr >= MAP_ROWS) ok = false;
                else if (s_map[cr][cc] != t)         ok = false;
                else if (s_cell_struct[cr][cc] >= 0) ok = false;
                else if (cell_has_hero(cc, cr))      ok = false;
                else if (s_visited_cell[cr][cc])     any_visited = true;
            }
            if (ok && any_visited) return add_structure_cells(type, ndcs, ndrs, n, ac, ar);

            /* Rotate for next iteration */
            for (int i = 0; i < n; i++) rot90(&dcs[i], &drs[i]);
        }
    }
    return false;
}

/* Shape definitions (canonical orientation — try_shape tests all 4 rotations) */
/* Forest Corner — L shape, 3× Forest */
static const int s_fc_dcs[] = { 0, 0, 1 };
static const int s_fc_drs[] = { 0, 1, 1 };
/* River Bend — L shape, 4× Water */
static const int s_rb_dcs[] = { 0, 0, 0, 1 };
static const int s_rb_drs[] = { 0, 1, 2, 0 };
/* Crossroads — + shape, 5× City */
static const int s_cr_dcs[] = { 1, 0, 1, 2, 1 };
static const int s_cr_drs[] = { 0, 1, 1, 1, 2 };

static void detect_structures(void)
{
    /* Does NOT clear existing structures. Locked cells (s_cell_struct >= 0)
     * are skipped by every try_* function, preserving formed structures. */

    /* 3×3 ring patterns first — largest and most specific */
    for (int r = 1; r < MAP_ROWS - 1; r++)
        for (int c = 1; c < MAP_COLS - 1; c++) {
            try_castle(c, r);
            try_ring_3x3(STRUCT_LUMBER_CAMP, TERRAIN_CITY, TERRAIN_FOREST, c, r);
        }

    /* Crossroads: 5-cell + shape (City) — before Road 1×3 to prevent subset */
    for (int r = 0; r < MAP_ROWS; r++)
        for (int c = 0; c < MAP_COLS; c++)
            try_shape(STRUCT_CROSSROADS, TERRAIN_CITY,
                      s_cr_dcs, s_cr_drs, 5, c, r);

    /* River Bend: 4-cell L (Water) — before River 1×4 straight */
    for (int r = 0; r < MAP_ROWS; r++)
        for (int c = 0; c < MAP_COLS; c++)
            try_shape(STRUCT_RIVER_BEND, TERRAIN_WATER,
                      s_rb_dcs, s_rb_drs, 4, c, r);

    /* River: 1×4 Water straight — scan before 1×3 to prevent subset formations */
    for (int r = 0; r < MAP_ROWS; r++)
        for (int c = 0; c <= MAP_COLS - 4; c++)
            try_row(STRUCT_RIVER, TERRAIN_WATER, c, r, 4, 1);
    for (int r = 0; r <= MAP_ROWS - 4; r++)
        for (int c = 0; c < MAP_COLS; c++)
            try_row(STRUCT_RIVER, TERRAIN_WATER, c, r, 1, 4);

    /* 2×2 homogeneous blocks — before smaller Forest/Plains patterns */
    for (int r = 0; r < MAP_ROWS - 1; r++)
        for (int c = 0; c < MAP_COLS - 1; c++) {
            try_2x2(STRUCT_DENSE_FOREST, TERRAIN_FOREST, c, r);
            try_2x2(STRUCT_FARM,         TERRAIN_PLAINS, c, r);
        }

    /* Forest Corner: 3-cell L (Forest) — after Dense Forest 2×2 */
    for (int r = 0; r < MAP_ROWS; r++)
        for (int c = 0; c < MAP_COLS; c++)
            try_shape(STRUCT_FOREST_CORNER, TERRAIN_FOREST,
                      s_fc_dcs, s_fc_drs, 3, c, r);

    /* 1×3 linear patterns — horizontal then vertical */
    for (int r = 0; r < MAP_ROWS; r++)
        for (int c = 0; c <= MAP_COLS - 3; c++) {
            try_row(STRUCT_WHEAT_FIELD, TERRAIN_PLAINS,   c, r, 3, 1);
            try_row(STRUCT_QUARRY,      TERRAIN_MOUNTAIN, c, r, 3, 1);
            try_row(STRUCT_ROAD,        TERRAIN_CITY,     c, r, 3, 1);
        }
    for (int r = 0; r <= MAP_ROWS - 3; r++)
        for (int c = 0; c < MAP_COLS; c++) {
            try_row(STRUCT_WHEAT_FIELD, TERRAIN_PLAINS,   c, r, 1, 3);
            try_row(STRUCT_QUARRY,      TERRAIN_MOUNTAIN, c, r, 1, 3);
            try_row(STRUCT_ROAD,        TERRAIN_CITY,     c, r, 1, 3);
        }
}

static void scan_matches(void)
{
    /* Each try_* function now requires s_visited_cell to be true for ALL
     * of its cells, so only patterns the hero fully traced this turn are
     * added.  Previously locked cells (s_cell_struct >= 0) block reuse. */
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
            case STRUCT_DENSE_FOREST:  pts = PT_DENSE_FOREST;  break;
            case STRUCT_FARM:          pts = PT_FARM;          break;
            case STRUCT_CASTLE:        pts = PT_CASTLE;        break;
            case STRUCT_LUMBER_CAMP:   pts = PT_LUMBER_CAMP;   break;
            case STRUCT_RIVER:         pts = PT_RIVER;         break;
            case STRUCT_WHEAT_FIELD:   pts = PT_WHEAT_FIELD;   break;
            case STRUCT_ROAD:          pts = PT_ROAD;          break;
            case STRUCT_QUARRY:        pts = PT_QUARRY;        break;
            case STRUCT_FOREST_CORNER: pts = PT_FOREST_CORNER; break;
            case STRUCT_RIVER_BEND:    pts = PT_RIVER_BEND;    break;
            case STRUCT_CROSSROADS:    pts = PT_CROSSROADS;    break;
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

/* Build connected same-terrain groups via BFS; populates all s_group_* state. */
static void build_groups(void)
{
    static bool visited[MAP_ROWS][MAP_COLS];
    static int  bfs_col[MAP_ROWS * MAP_COLS];
    static int  bfs_row[MAP_ROWS * MAP_COLS];

    static const int dc4[4] = { 0,  0,  1, -1 };
    static const int dr4[4] = { 1, -1,  0,  0 };

    memset(visited,      0,   sizeof(visited));
    memset(s_group_map, -1,   sizeof(s_group_map));
    s_group_count  = 0;
    int flat_idx   = 0;
    s_group_start[0] = 0;

    for (int sr = 0; sr < MAP_ROWS; sr++) {
        for (int sc = 0; sc < MAP_COLS; sc++) {
            if (visited[sr][sc]) continue;
            if (s_group_count >= MAX_GROUPS) break;

            Terrain t  = s_map[sr][sc];
            int head   = 0, tail = 0;
            bfs_col[tail] = sc; bfs_row[tail] = sr; tail++;
            visited[sr][sc] = true;

            while (head < tail) {
                int c = bfs_col[head], r = bfs_row[head]; head++;
                s_group_map[r][c] = s_group_count;
                s_group_col_flat[flat_idx] = c;
                s_group_row_flat[flat_idx] = r;
                flat_idx++;

                for (int d = 0; d < 4; d++) {
                    int nc = c + dc4[d], nr = r + dr4[d];
                    if (nc < 0 || nc >= MAP_COLS || nr < 0 || nr >= MAP_ROWS) continue;
                    if (visited[nr][nc]) continue;
                    if (s_map[nr][nc] != t) continue;
                    visited[nr][nc] = true;
                    bfs_col[tail] = nc; bfs_row[tail] = nr; tail++;
                }
            }

            s_group_terrain[s_group_count] = t;
            s_group_size[s_group_count]    = tail;
            s_group_count++;
            s_group_start[s_group_count]   = flat_idx;
        }
    }
}

static bool groups_adjacent(int ga, int gb)
{
    static const int dc4[4] = { 0,  0,  1, -1 };
    static const int dr4[4] = { 1, -1,  0,  0 };
    int start = s_group_start[ga], end = s_group_start[ga + 1];
    for (int i = start; i < end; i++) {
        int c = s_group_col_flat[i], r = s_group_row_flat[i];
        for (int d = 0; d < 4; d++) {
            int nc = c + dc4[d], nr = r + dr4[d];
            if (nc < 0 || nc >= MAP_COLS || nr < 0 || nr >= MAP_ROWS) continue;
            if (s_group_map[nr][nc] == gb) return true;
        }
    }
    return false;
}

/* Check that the n selected groups form a connected subgraph (each group
 * can reach every other via adjacency).  Uses a simple BFS on group indices. */
static bool order_groups_connected(int *gidx, int n)
{
    if (n <= 1) return true;
    bool reached[MAX_ORDER_REQS] = { false };
    int  queue[MAX_ORDER_REQS];
    reached[0] = true;
    queue[0]   = 0;
    int head = 0, tail = 1;
    while (head < tail) {
        int a = queue[head++];
        for (int b = 0; b < n; b++) {
            if (reached[b]) continue;
            if (groups_adjacent(gidx[a], gidx[b])) {
                reached[b]    = true;
                queue[tail++] = b;
            }
        }
    }
    for (int i = 0; i < n; i++) if (!reached[i]) return false;
    return true;
}

static Order generate_order(void)
{
    Order o;
    o.req_count = GetRandomValue(1, MAX_ORDER_REQS);
    bool used[TERRAIN_COUNT] = { false };
    for (int i = 0; i < o.req_count; i++) {
        /* pick unused terrain type */
        int t;
        do { t = GetRandomValue(0, TERRAIN_COUNT - 1); } while (used[t]);
        used[t] = true;
        o.terrain[i]  = (Terrain)t;
        o.min_size[i] = GetRandomValue(2, 5);
    }
    return o;
}

/* Remove cells belonging to groups in gidx[0..n-1] from the map.
 * Handles structure cleanup, flash registration, and scoring. */
static void remove_groups(int *gidx, int n)
{
    static bool struct_remove[MAX_STRUCTURES];
    memset(struct_remove, 0, sizeof(struct_remove));

    int total_cells = 0;
    float sum_c = 0.0f, sum_r = 0.0f;

    for (int gi = 0; gi < n; gi++) {
        int g = gidx[gi];
        int start = s_group_start[g], end = s_group_start[g + 1];
        total_cells += end - start;
        for (int i = start; i < end; i++) {
            int c = s_group_col_flat[i], r = s_group_row_flat[i];
            sum_c += (float)c; sum_r += (float)r;
            int si = s_cell_struct[r][c];
            if (si >= 0) struct_remove[si] = true;
        }
    }

    /* Clear structure footprints */
    for (int i = 0; i < s_structure_count; i++) {
        if (!struct_remove[i]) continue;
        Structure *s = &s_structures[i];
        for (int ci = 0; ci < s->cell_count; ci++)
            s_cell_struct[s->row + s->cell_dr[ci]][s->col + s->cell_dc[ci]] = -1;
    }
    /* Compact s_structures[] */
    int new_count = 0;
    for (int i = 0; i < s_structure_count; i++) {
        if (struct_remove[i]) continue;
        if (new_count != i) {
            s_structures[new_count] = s_structures[i];
            Structure *s = &s_structures[new_count];
            for (int ci = 0; ci < s->cell_count; ci++)
                s_cell_struct[s->row + s->cell_dr[ci]][s->col + s->cell_dc[ci]] = new_count;
        }
        new_count++;
    }
    s_structure_count = new_count;

    /* Replace terrain, record flash */
    for (int gi = 0; gi < n; gi++) {
        int g = gidx[gi];
        int start = s_group_start[g], end = s_group_start[g + 1];
        for (int i = start; i < end; i++) {
            int c = s_group_col_flat[i], r = s_group_row_flat[i];
            s_map[r][c] = (Terrain)GetRandomValue(0, TERRAIN_COUNT - 1);
            if (s_flash_count < MAX_FLASH_CELLS) {
                s_flash_col[s_flash_count] = c;
                s_flash_row[s_flash_count] = r;
                s_flash_count++;
            }
        }
    }

    /* Score popup centred on removed cells */
    int pts = total_cells * 10;
    spawn_popup(sum_c / (float)total_cells,
                sum_r / (float)total_cells - 0.5f, pts);
    s_score += pts;
}

static void cashin_resolve(void)
{
    build_groups();

    bool any_fulfilled = false;
    s_flash_count = 0;

    for (int oi = 0; oi < ORDER_COUNT; oi++) {
        Order *ord = &s_orders[oi];

        /* Collect candidate groups for each requirement */
        int  cands[MAX_ORDER_REQS][MAX_GROUPS];
        int  cand_count[MAX_ORDER_REQS];
        memset(cand_count, 0, sizeof(cand_count));

        for (int ri = 0; ri < ord->req_count; ri++) {
            for (int g = 0; g < s_group_count; g++) {
                if (s_group_terrain[g] == ord->terrain[ri] &&
                    s_group_size[g]    >= ord->min_size[ri])
                    cands[ri][cand_count[ri]++] = g;
            }
            if (cand_count[ri] == 0) goto next_order;
        }

        /* Try all combinations (one group per requirement); all groups
         * must be distinct and their union must be connected. */
        {
            int gidx[MAX_ORDER_REQS];
            bool found = false;

            /* Simple nested iteration up to MAX_ORDER_REQS = 3 */
            for (int a = 0; a < cand_count[0] && !found; a++) {
                gidx[0] = cands[0][a];
                if (ord->req_count == 1) {
                    found = true; break;
                }
                for (int b = 0; b < cand_count[1] && !found; b++) {
                    if (cands[1][b] == gidx[0]) continue;
                    gidx[1] = cands[1][b];
                    if (ord->req_count == 2) {
                        if (order_groups_connected(gidx, 2)) { found = true; break; }
                        continue;
                    }
                    for (int c = 0; c < cand_count[2] && !found; c++) {
                        if (cands[2][c] == gidx[0] || cands[2][c] == gidx[1]) continue;
                        gidx[2] = cands[2][c];
                        if (order_groups_connected(gidx, 3)) { found = true; break; }
                    }
                }
            }

            if (found) {
                remove_groups(gidx, ord->req_count);
                s_orders[oi] = generate_order();
                any_fulfilled = true;
                /* Rebuild groups so subsequent orders see updated map */
                build_groups();
            }
        }

        next_order:;
    }

    s_pending_moves = 0;

    if (any_fulfilled) {
        s_phase      = PHASE_SCANNING;
        s_scan_flash = SCAN_FLASH_SECS;
    }
}

static void end_turn(void)
{
    s_is_dragging   = false;
    s_flash_count   = 0;
    s_pending_moves++;
    award_turn_score();
    s_phase      = PHASE_IDLE;
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
    memset(s_col_visited,   0, sizeof(s_col_visited));
    memset(s_row_visited,   0, sizeof(s_row_visited));
    memset(s_visited_cell,  0, sizeof(s_visited_cell));
    s_visited_cell[s_hero_row][s_hero_col] = true;
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
        return;
    }

    /* cash-in button */
    if (s_phase == PHASE_IDLE && CheckCollisionPointRec(pos, cashin_btn_rect())) {
        cashin_resolve();
    }
}

static const Terrain STRUCT_TERRAIN[STRUCT_TYPE_COUNT] = {
    TERRAIN_PLAINS,    /* NONE           */
    TERRAIN_FOREST,    /* DENSE_FOREST   */
    TERRAIN_PLAINS,    /* FARM           */
    TERRAIN_MOUNTAIN,  /* CASTLE         */
    TERRAIN_CITY,      /* LUMBER_CAMP    */
    TERRAIN_WATER,     /* RIVER          */
    TERRAIN_PLAINS,    /* WHEAT_FIELD    */
    TERRAIN_CITY,      /* ROAD           */
    TERRAIN_MOUNTAIN,  /* QUARRY         */
    TERRAIN_FOREST,    /* FOREST_CORNER  */
    TERRAIN_WATER,     /* RIVER_BEND     */
    TERRAIN_CITY,      /* CROSSROADS     */
};

/* --------------------------------------------------------------- rendering */

/* Draw a terrain letter centered at (cx, cy), scaled by s (= tw/3). */
static void draw_terrain_icon(int cx, int cy, int s, Terrain t, Color c)
{
    if (t < 0 || t >= TERRAIN_COUNT) return;
    char letter[2] = { TERRAIN_LETTER[t], '\0' };
    int fs = s * 2;
    if (fs < 8) fs = 8;
    int lw = MeasureText(letter, fs);
    DrawText(letter, cx - lw / 2, cy - fs / 2, fs, c);
}

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
            int   inset = ts / 10; if (inset < 2) inset = 2;
            int   sx = (int)((col - s_vp_vis_col) * ts) + inset;
            int   sy = (int)((row - s_vp_vis_row) * ts) + inset;
            int   tw = ts - inset * 2;
            Color c  = TERRAIN_COLOR[s_map[row][col]];
            DrawRectangle(sx, sy, tw, tw, c);
            int icx = sx + tw / 2;
            int icy = sy + tw / 2;
            int s   = tw / 3;
            draw_terrain_icon(icx, icy, s, s_map[row][col], (Color){ 0, 0, 0, 160 });
        }
    }

    /* Pass 2: draw each structure cell-by-cell (supports non-rectangular shapes) */
    for (int i = 0; i < s_structure_count; i++) {
        Structure *s = &s_structures[i];
        /* cull off-screen via bounding box (expanded by 1 to match tile loop) */
        if (s->col + s->w <= s_vp_col - 1 || s->col >= s_vp_col + vc + 1) continue;
        if (s->row + s->h <= s_vp_row - 1 || s->row >= s_vp_row + vr + 1) continue;

        Color col = STRUCT_COLOR[s->type];
        int inset = ts / 10; if (inset < 2) inset = 2;
        int tw    = ts - inset * 2;
        Terrain st = STRUCT_TERRAIN[s->type];
        for (int ci = 0; ci < s->cell_count; ci++) {
            int ox = (int)((s->col + s->cell_dc[ci] - s_vp_vis_col) * ts) + inset;
            int oy = (int)((s->row + s->cell_dr[ci] - s_vp_vis_row) * ts) + inset;
            /* faint outer glow */
            DrawRectangleRounded(
                (Rectangle){ (float)(ox - 3), (float)(oy - 3),
                             (float)(tw + 6), (float)(tw + 6) },
                0.30f, 4, (Color){ col.r, col.g, col.b, 50 });
            /* solid filled cell */
            DrawRectangleRounded(
                (Rectangle){ (float)ox, (float)oy, (float)tw, (float)tw },
                0.20f, 4, col);
            /* white terrain icon inside */
            int icx = ox + tw / 2;
            int icy = oy + tw / 2;
            draw_terrain_icon(icx, icy, tw / 3, st, (Color){ 0, 0, 0, 140 });
        }

        /* Label centered on bounding box */
        int sx = (int)((s->col - s_vp_vis_col) * ts);
        int sy = (int)((s->row - s_vp_vis_row) * ts);
        int bw = s->w * ts - 1;
        int bh = s->h * ts - 1;
        const char *label = STRUCT_LABEL[s->type];
        int fs = ts * 34 / 100;
        int lw = MeasureText(label, fs);
        /* drop shadow then white label */
        DrawText(label, sx + (bw - lw) / 2 + 1, sy + (bh - fs) / 2 + 1,
                 fs, (Color){ 0, 0, 0, 140 });
        DrawText(label, sx + (bw - lw) / 2, sy + (bh - fs) / 2,
                 fs, (Color){ 255, 255, 255, 235 });
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

        /* colour gradient: oldest = cool cyan, newest = warm gold */
        unsigned char tr = (unsigned char)(80  + (int)(175 * gradient_t));
        unsigned char tg = (unsigned char)(200 + (int)( 20 * gradient_t));
        unsigned char tb = (unsigned char)(255 - (int)(195 * gradient_t));
        int inset = ts / 10; if (inset < 2) inset = 2;
        int tw    = ts - inset * 2;
        float bw  = (float)(tw * 8 / 100); if (bw < 2.0f) bw = 2.0f;
        /* trail uses filled tile so the path reads clearly on the black grid */
        DrawRectangleRounded(
            (Rectangle){ (float)sx + inset, (float)sy + inset, (float)tw, (float)tw },
            0.18f, 4, (Color){ tr, tg, tb, (unsigned char)alpha });
        /* brighter border on the most recent TRAIL_BORDER_RECENT tiles */
        if (i >= s_trail_len - TRAIL_BORDER_RECENT) {
            DrawRectangleLinesEx(
                (Rectangle){ (float)sx + inset, (float)sy + inset, (float)tw, (float)tw },
                bw, (Color){ 255, 255, 255, (unsigned char)(alpha * 3 / 4) });
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
        int   inset = ts / 10; if (inset < 2) inset = 2;
        int   tw = ts - inset * 2;
        int   ox = sx + inset, oy = sy + inset;
        Color tc = TERRAIN_COLOR[ft->terrain];
        float bw = (float)(tw * 8 / 100); if (bw < 2.0f) bw = 2.0f;
        /* white border = tile in motion */
        DrawRectangleLinesEx(
            (Rectangle){ (float)ox, (float)oy, (float)tw, (float)tw },
            bw + 1.0f, (Color){ 255, 255, 255, 220 });
        DrawRectangleLinesEx(
            (Rectangle){ (float)ox, (float)oy, (float)tw, (float)tw },
            bw, tc);
        draw_terrain_icon(ox + tw / 2, oy + tw / 2, tw / 3, ft->terrain, tc);
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

    /* outer glow ring */
    float gw = w + 6.0f * scale;
    DrawRectangleRounded(
        (Rectangle){ cx - gw * 0.5f, cy - gw * 0.5f, gw, gw },
        0.30f, 6, (Color){ 220, 100, 255, 60 });
    /* main hero tile — bright white */
    DrawRectangleRounded(
        (Rectangle){ sx, sy, w, h },
        0.22f, 6, (Color){ 255, 255, 255, 255 });
    /* vivid magenta border */
    DrawRectangleLinesEx((Rectangle){ sx, sy, w, h },
                         2.5f, (Color){ 220, 80, 255, 255 });
    int  fs = (int)(ts * 36 / 100 * scale);
    int  lw = MeasureText("@", fs);
    DrawText("@", (int)(cx - lw * 0.5f), (int)(cy - fs * 0.5f),
             fs, (Color){ 40, 0, 60, 220 });
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
        DrawRectangleRec(dr, (Color){ 8, 8, 12, 248 });
        DrawRectangleLinesEx(dr, 1.5f, (Color){ 220, 80, 255, 160 });

        if (s_hud_t > 0.3f) {
            int n_rows = 3;
            int row_h  = (int)(dr.height * 0.26f);
            int bar_h  = (int)(dr.height * 0.16f);
            /* centre the block vertically */
            int block_h = n_rows * row_h + bar_h;
            int oy      = (int)(dr.y + (dr.height - block_h) * 0.45f);

            int lfs = row_h * 44 / 100;
            if (lfs < 12) lfs = 12;

            /* --- row 0: Score --- */
            {
                int ry = oy;
                DrawText("Score", pad, ry + (row_h - lfs) / 2, lfs,
                         (Color){ 140, 150, 170, 200 });
                char buf[24];
                snprintf(buf, sizeof(buf), "%d", s_score);
                Color vc = (s_score >= SCORE_GOAL) ? (Color){ 100, 240, 100, 255 }
                                                   : (Color){ 255, 215,  70, 255 };
                int vw = MeasureText(buf, lfs);
                DrawText(buf, sw - vw - pad, ry + (row_h - lfs) / 2, lfs, vc);
            }
            /* --- row 1: Goal --- */
            {
                int ry = oy + row_h;
                DrawText("Goal", pad, ry + (row_h - lfs) / 2, lfs,
                         (Color){ 140, 150, 170, 200 });
                char buf[24];
                snprintf(buf, sizeof(buf), "%d", SCORE_GOAL);
                int vw = MeasureText(buf, lfs);
                DrawText(buf, sw - vw - pad, ry + (row_h - lfs) / 2, lfs,
                         (Color){ 180, 190, 210, 220 });
            }
            /* --- row 2: Turn --- */
            {
                int ry = oy + row_h * 2;
                DrawText("Turn", pad, ry + (row_h - lfs) / 2, lfs,
                         (Color){ 140, 150, 170, 200 });
                char buf[24];
                snprintf(buf, sizeof(buf), "%d / %d", s_turn_count, TURNS_LIMIT);
                int turns_left = TURNS_LIMIT - s_turn_count;
                Color vc = (turns_left <= 0)
                               ? (Color){ 255,  80,  80, 255 }
                           : (turns_left <= TURNS_LIMIT * 3 / 10)
                               ? (Color){ 255, 160,  60, 255 }
                           :   (Color){ 200, 200, 200, 220 };
                int vw = MeasureText(buf, lfs);
                DrawText(buf, sw - vw - pad, ry + (row_h - lfs) / 2, lfs, vc);
            }
            /* --- progress bar --- */
            {
                int by   = oy + row_h * 3;
                float prog = (float)s_score / (float)SCORE_GOAL;
                if (prog > 1.0f) prog = 1.0f;
                int bw = sw - pad * 2;
                DrawRectangle(pad, by, bw, bar_h,
                              (Color){ 20, 20, 28, 230 });
                DrawRectangle(pad, by, (int)(bw * prog), bar_h,
                              (Color){ 255, 200, 65, 230 });
                DrawRectangleLinesEx(
                    (Rectangle){ (float)pad, (float)by,
                                 (float)bw,  (float)bar_h },
                    1.0f, (Color){ 220, 80, 255, 160 });
                /* percentage inside bar */
                char pct[16];
                snprintf(pct, sizeof(pct), "%d%%", (int)(prog * 100.0f));
                int pfs = bar_h * 60 / 100;
                if (pfs < 10) pfs = 10;
                int pw = MeasureText(pct, pfs);
                DrawText(pct, (sw - pw) / 2,
                         by + (bar_h - pfs) / 2,
                         pfs, (Color){ 20, 20, 20, 200 });
            }
        }
    }

    /* --- always-visible tab --- */
    Rectangle tab   = hud_tab_rect();
    int       tab_h = (int)tab.height;
    int       fs    = tab_h * 46 / 100;
    DrawRectangleRec(tab, (Color){ 0, 0, 0, 240 });
    DrawLine(0, tab_h, sw, tab_h, (Color){ 220, 80, 255, 180 });

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

static void draw_legend(void)
{
    int sw = GetScreenWidth();

    /* --- always-visible tab --- */
    Rectangle tab    = legend_tab_rect();
    int       tab_fs = (int)(tab.height * 0.42f);
    DrawRectangleRec(tab, (Color){ 0, 0, 0, 245 });
    DrawRectangleLinesEx(tab, 1.5f, (Color){ 255, 200, 80, 200 });

    const char *label = "Orders";
    int tlw = MeasureText(label, tab_fs);
    DrawText(label, (sw - tlw) / 2,
             (int)(tab.y + (tab.height - tab_fs) / 2),
             tab_fs, (Color){ 255, 200, 80, 255 });

    const char *arrow = s_legend_open ? "v" : "^";
    int aw = MeasureText(arrow, tab_fs);
    DrawText(arrow, sw - aw - 12,
             (int)(tab.y + (tab.height - tab_fs) / 2),
             tab_fs, (Color){ 160, 160, 160, 200 });

    if (s_legend_t < 0.05f) return;

    /* --- sliding body --- */
    Rectangle dr  = legend_body_rect();
    int       pad = (int)(dr.width * 0.03f);
    DrawRectangleRec(dr, (Color){ 5, 5, 8, 248 });
    DrawRectangleLinesEx(dr, 1.5f, (Color){ 255, 200, 80, 160 });

    if (s_legend_t < 0.25f) return;

    int row_h = (int)(dr.height / (float)ORDER_COUNT);

    for (int oi = 0; oi < ORDER_COUNT; oi++) {
        Order *ord = &s_orders[oi];
        int    ry  = (int)dr.y + oi * row_h;

        if (oi > 0)
            DrawLine((int)(dr.x + pad), ry,
                     (int)(dr.x + dr.width - pad), ry,
                     (Color){ 255, 200, 80, 60 });

        /* "Order N:" prefix */
        int label_fs = row_h * 28 / 100;
        if (label_fs < 8) label_fs = 8;
        char ord_label[16];
        snprintf(ord_label, sizeof(ord_label), "Order %d:", oi + 1);
        DrawText(ord_label, (int)(dr.x + pad),
                 ry + (row_h - label_fs) / 2,
                 label_fs, (Color){ 200, 200, 200, 200 });
        int lw = MeasureText(ord_label, label_fs);
        int x  = (int)(dr.x + pad + lw + pad);

        /* requirement badges: [coloured square with letter] x N */
        int badge_h  = row_h * 65 / 100; if (badge_h < 8) badge_h = 8;
        int badge_fs = badge_h * 60 / 100; if (badge_fs < 6) badge_fs = 6;
        int count_fs = label_fs;
        int plus_fs  = label_fs;

        for (int ri = 0; ri < ord->req_count; ri++) {
            if (ri > 0) {
                DrawText("+", x + 4, ry + (row_h - plus_fs) / 2,
                         plus_fs, (Color){ 180, 180, 180, 200 });
                x += MeasureText("+", plus_fs) + 8;
            }

            Terrain t  = ord->terrain[ri];
            Color   tc = TERRAIN_COLOR[t];
            int     by = ry + (row_h - badge_h) / 2;

            DrawRectangle(x, by, badge_h, badge_h, tc);
            char tl[2] = { TERRAIN_LETTER[t], '\0' };
            int  tfw   = MeasureText(tl, badge_fs);
            DrawText(tl, x + (badge_h - tfw) / 2,
                     by + (badge_h - badge_fs) / 2,
                     badge_fs, (Color){ 0, 0, 0, 200 });
            x += badge_h + 2;

            char cnt[8];
            snprintf(cnt, sizeof(cnt), "x%d", ord->min_size[ri]);
            DrawText(cnt, x, ry + (row_h - count_fs) / 2,
                     count_fs, (Color){ 255, 255, 255, 220 });
            x += MeasureText(cnt, count_fs) + 4;
        }
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
    s_score         = 0;
    s_turn_count    = 0;
    s_pending_moves = 0;
    s_game_over     = false;
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
    memset(s_visited_cell,  0, sizeof(s_visited_cell));

    s_flash_count   = 0;
    s_move_cd       = 0.0f;
    s_hud_open      = false;
    s_hud_t         = 0.0f;
    s_legend_open   = false;
    s_legend_t      = 0.0f;
    s_prev_tc       = 0;
    s_prev_touch    = (Vector2){ -1.0f, -1.0f };

    /* seed initial orders */
    for (int i = 0; i < ORDER_COUNT; i++)
        s_orders[i] = generate_order();
    s_group_count = 0;
    memset(s_group_map, -1, sizeof(s_group_map));
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

static void draw_cashin_btn(void)
{
    Rectangle r    = cashin_btn_rect();
    bool active    = (s_pending_moves > 0) && (s_phase == PHASE_IDLE);
    Color bg       = active ? (Color){ 220, 170, 40, 230 } : (Color){ 80, 80, 80, 180 };
    Color border   = active ? (Color){ 255, 220, 80, 255 } : (Color){ 120, 120, 120, 200 };
    Color text_col = active ? (Color){ 0, 0, 0, 255 }     : (Color){ 160, 160, 160, 200 };

    DrawRectangleRounded(r, 0.25f, 6, bg);
    DrawRectangleLinesEx(r, 2.0f, border);

    char label[32];
    snprintf(label, sizeof(label), "Cash In (%d)", s_pending_moves);
    int sh = GetScreenHeight();
    int fs = sh * 4 / 100;
    if (fs < 10) fs = 10;
    int lw = MeasureText(label, fs);
    DrawText(label,
             (int)(r.x + (r.width  - lw) / 2),
             (int)(r.y + (r.height - fs) / 2),
             fs, text_col);
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
                 (Color){ 0, 0, 0, (unsigned char)(a * 2 / 3) });
        /* outer glow in magenta */
        DrawText(buf, (int)cx - lw / 2 - 1, (int)cy - 1, fs,
                 (Color){ 220, 80, 255, (unsigned char)(a / 3) });
        /* main text — vivid gold */
        DrawText(buf, (int)cx - lw / 2, (int)cy, fs,
                 (Color){ 255, 230, 60, a });
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
    ClearBackground((Color){ 0, 0, 0, 255 });
    draw_map_tiles();
    draw_trail();
    draw_scan_flash();
    draw_flying_tiles();
    draw_hero();
    draw_score_popups();
    draw_resource_strip();
    draw_timer_bar();
    draw_cashin_btn();
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
