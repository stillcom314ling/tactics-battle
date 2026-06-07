#include "realm_walk.h"
#include "raylib.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- constants */

#define MAP_COLS        30
#define MAP_ROWS        20
#define VIEW_COLS        6      /* fixed visible columns                   */
#define VIEW_ROWS        8      /* fixed visible rows                      */
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

typedef enum {
    UNIT_NONE = 0,
    UNIT_BRUTE,
    UNIT_ROGUE,
    UNIT_MAGE,
    UNIT_VILLAIN_STALKER,
    UNIT_VILLAIN_SWARMER,
    UNIT_MINION,
} UnitKind;

#define IS_PLAYER_KIND(k)  ((k) == UNIT_BRUTE || (k) == UNIT_ROGUE || (k) == UNIT_MAGE)
#define IS_VILLAIN_KIND(k) ((k) == UNIT_VILLAIN_STALKER || (k) == UNIT_VILLAIN_SWARMER)
#define IS_ENEMY_KIND(k)   (IS_VILLAIN_KIND(k) || (k) == UNIT_MINION)

#define HP_BRUTE 13
#define HP_ROGUE 11
#define HP_MAGE  10
#define HP_VILLAIN_MIN 15
#define HP_VILLAIN_MAX 25
#define HP_MINION 2
#define DMG_STALKER 6
#define DMG_SWARMER 2
#define MINION_HP_BUFF  5
#define MINION_DMG_BUFF 1
#define OBSTACLE_DENSITY_PCT 15
#define MINIONS_PER_TURN     2

#define MAX_TGTS 8

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
static bool  s_view_locked; /* when true, viewport never auto-pans */

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

/* ---- tactical unit state (multiple units per kind) ---- */
typedef struct {
    UnitKind kind;
    bool     alive;
    int      col, row;
    int      pre_drag_col, pre_drag_row;  /* snapshot at on_down (player only) */
    int      hp, max_hp;
    int      damage_bonus;                /* villains only */
    /* minion-only buff record */
    int      buff_villain_idx;            /* -1 if none */
    int      buff_kind;                   /* 0 = +damage, 1 = +HP */
    int      buff_amount;
    /* rendering */
    float    vis_col, vis_row;
    float    bump;
} Unit;

#define MAX_UNITS 32
static Unit s_units[MAX_UNITS];
static int  s_unit_count;
static int  s_active_player_idx;  /* -1 = none being dragged */
static int  s_inspect_idx;        /* -1 = none being inspected (tap-to-show info) */

/* Villain target lists: one per unit-index, but only villains populate. */
static int s_villain_tgt_col[MAX_UNITS][MAX_TGTS];
static int s_villain_tgt_row[MAX_UNITS][MAX_TGTS];
static int s_villain_tgt_n[MAX_UNITS];

/* Wave counter (1, 2, ...) — incremented when both villains die. */
static int s_wave_index;

/* Obstacles: impassable cells generated at game start. */
static bool s_obstacle[MAP_ROWS][MAP_COLS];

/* Rogue path tracking (cells the rogue traversed in the current drag). */
#define ROGUE_PATH_MAX (MAP_COLS + MAP_ROWS)
static int s_rogue_path_col[ROGUE_PATH_MAX];
static int s_rogue_path_row[ROGUE_PATH_MAX];
static int s_rogue_path_n;

/* Legacy mirror of the active player's logical position. Kept in sync
 * during drag so check_scroll_trigger and a few other helpers continue
 * to work without churn. */
/* (s_hero_col/s_hero_row remain declared above.) */

static bool  s_player_won;
static bool  s_player_lost;
static float s_resolve_flash;     /* counts down to flash attack hits   */
#define RESOLVE_FLASH_SECS 0.45f

/* Latest resolved attack targets (for the post-drag flash). */
typedef struct { int col, row; int damage; bool by_player; } AttackHit;
#define MAX_ATTACKS 64
static AttackHit s_last_attacks[MAX_ATTACKS];
static int       s_last_attack_count;

/* forward decls for round helpers (defined below end_turn) */
static void enemy_plan_turn(void);
static int  unit_at(int col, int row);    /* -1 = none */
static bool cell_is_walkable(int col, int row);
static void damage_unit(int idx, int dmg);
static void spawn_minions(void);
static void spawn_villains_for_wave(void);
static bool wave_clear_if_done(void);

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

static int nav_btn_size(void)
{
    int sh = GetScreenHeight();
    int s  = sh / 14;
    return s < 44 ? 44 : s;
}

static int tile_px(void)
{
    int sw     = GetScreenWidth();
    int sh     = GetScreenHeight();
    int hud_h  = sh * 6 / 100;
    int leg_h  = sh * 7 / 100;
    int nav_h  = nav_btn_size();
    int gap    = nav_h / 6;
    int avail_h = sh - hud_h - leg_h - nav_h - gap * 3;
    if (avail_h < 1) avail_h = 1;
    int t_w = sw / VIEW_COLS;
    int t_h = avail_h / VIEW_ROWS;
    int t   = t_w < t_h ? t_w : t_h;
    return t > 0 ? t : 1;
}

static int grid_x(void)
{
    int sw = GetScreenWidth();
    int gw = VIEW_COLS * tile_px();
    return (sw - gw) / 2;
}

static int grid_y(void)
{
    int sh    = GetScreenHeight();
    int hud_h = sh * 6 / 100;
    int leg_h = sh * 7 / 100;
    int nav_h = nav_btn_size();
    int gap   = nav_h / 6;
    int gh    = VIEW_ROWS * tile_px();
    int avail = sh - hud_h - leg_h - nav_h - gap * 3;
    if (avail < 0) avail = 0;
    return hud_h + gap + (avail - gh) / 2;
}

static int vis_cols(void) { return VIEW_COLS; }
static int vis_rows(void) { return VIEW_ROWS; }

static bool screen_to_tile(Vector2 p, int *out_col, int *out_row)
{
    int ts  = tile_px();
    int gx  = grid_x();
    int gy  = grid_y();
    float lx = p.x - (float)gx;
    float ly = p.y - (float)gy;
    if (lx < 0.0f || ly < 0.0f) { *out_col = -1; *out_row = -1; return false; }
    int col = s_vp_col + (int)(lx / (float)ts);
    int row = s_vp_row + (int)(ly / (float)ts);
    if (col < s_vp_col || col >= s_vp_col + VIEW_COLS ||
        row < s_vp_row || row >= s_vp_row + VIEW_ROWS) {
        *out_col = -1; *out_row = -1;
        return false;
    }
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
        (float)((map_col - s_vp_col) * ts + ts / 2 + grid_x()),
        (float)((map_row - s_vp_row) * ts + ts / 2 + grid_y()),
    };
}

/* Nav button geometry: row of 5 buttons (<, ^, v, >, lock) above the
 * legend tab. Index 4 is the view-lock toggle. */
#define NAV_BTN_COUNT 5
#define NAV_BTN_LOCK  4
static Rectangle nav_btn_rect(int idx)
{
    int sw    = GetScreenWidth();
    int sh    = GetScreenHeight();
    int leg_h = sh * 7 / 100;
    int s     = nav_btn_size();
    int gap   = s / 6;
    int total = s * NAV_BTN_COUNT + gap * (NAV_BTN_COUNT - 1);
    int x     = (sw - total) / 2;
    int y     = sh - leg_h - gap - s;
    return (Rectangle){ (float)(x + idx * (s + gap)), (float)y, (float)s, (float)s };
}

static int touch_nav_btn(Vector2 pos)
{
    for (int i = 0; i < NAV_BTN_COUNT; i++)
        if (CheckCollisionPointRec(pos, nav_btn_rect(i))) return i;
    return -1;
}

static void clamp_viewport(void)
{
    if (s_vp_col < 0) s_vp_col = 0;
    if (s_vp_col + VIEW_COLS > MAP_COLS) s_vp_col = MAP_COLS - VIEW_COLS;
    if (s_vp_row < 0) s_vp_row = 0;
    if (s_vp_row + VIEW_ROWS > MAP_ROWS) s_vp_row = MAP_ROWS - VIEW_ROWS;
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
    if (s_view_locked) return false;   /* view pinned — never auto-pan */

    int vc = vis_cols();
    int vr = vis_rows();
    int old_vp_col = s_vp_col;
    int old_vp_row = s_vp_row;

    /* Keep the hero on-screen: pan only when it lands on (or past) an edge of
       the fixed 6x8 viewport. Manual nav buttons handle all other panning. */
    int rel_col = s_hero_col - s_vp_col;
    int rel_row = s_hero_row - s_vp_row;

    if (rel_col >= vc - 1) s_vp_col++;
    else if (rel_col <= 0) s_vp_col--;

    if (rel_row >= vr - 1) s_vp_row++;
    else if (rel_row <= 0) s_vp_row--;

    clamp_viewport();

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
    if (s_active_player_idx < 0) return false;
    Unit *p = &s_units[s_active_player_idx];
    if (new_col < 0 || new_col >= MAP_COLS) return false;
    if (new_row < 0 || new_row >= MAP_ROWS) return false;
    if (new_col == p->col && new_row == p->row) return false;

    /* Obstacles block movement. */
    if (!cell_is_walkable(new_col, new_row)) return false;

    /* If destination is occupied by another living unit, swap with it: the
     * displaced unit goes to where the player was, terrain at both cells
     * stays put. Lets the player reposition the enemy (and protected) tile
     * to dodge or redirect attack telegraphs. */
    int dest_unit = unit_at(new_col, new_row);

    int si = s_cell_struct[new_row][new_col];
    if (dest_unit >= 0 && dest_unit != s_active_player_idx) {
        Unit *o = &s_units[dest_unit];
        int old_col = p->col;
        int old_row = p->row;
        o->col  = old_col;
        o->row  = old_row;
        o->bump = BUMP_SCALE_PEAK - 1.0f;
    } else if (si >= 0) {
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
        s_map[p->row][p->col] = displaced;

        /* Spawn flying tile: slides from player's destination back to vacated cell */
        for (int _fi = 0; _fi < MAX_FLYING_TILES; _fi++) {
            if (!s_flying[_fi].active) {
                s_flying[_fi] = (FlyingTile){
                    .active  = true,
                    .terrain = displaced,
                    .vis_col = (float)new_col,
                    .vis_row = (float)new_row,
                    .dst_col = (float)p->col,
                    .dst_row = (float)p->row,
                };
                s_cell_flying_dst[p->row][p->col] = true;
                break;
            }
        }
    }

    p->col  = new_col;
    p->row  = new_row;
    p->bump = BUMP_SCALE_PEAK - 1.0f;
    s_inspect_idx = -1;   /* real drag committed — drop any stale inspect card */
    /* Mirror the active player's position into the legacy hero state so
     * check_scroll_trigger() and other helpers keep working. */
    s_hero_col = new_col;
    s_hero_row = new_row;

    /* Track the rogue's path so its attack hits everywhere it stepped. */
    if (p->kind == UNIT_ROGUE && s_rogue_path_n < ROGUE_PATH_MAX) {
        s_rogue_path_col[s_rogue_path_n] = new_col;
        s_rogue_path_row[s_rogue_path_n] = new_row;
        s_rogue_path_n++;
    }

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

/* Returns -1 if no living unit at (col, row), otherwise the unit index. */
static int unit_at(int col, int row)
{
    for (int i = 0; i < s_unit_count; i++) {
        Unit *u = &s_units[i];
        if (!u->alive) continue;
        if (u->col == col && u->row == row) return i;
    }
    return -1;
}

static int count_living(UnitKind kind)
{
    int n = 0;
    for (int i = 0; i < s_unit_count; i++)
        if (s_units[i].alive && s_units[i].kind == kind) n++;
    return n;
}

/* Swap a unit onto (new_col, new_row), pushing the terrain there back
 * into the unit's old cell. (Skipped if destination is the unit itself.)
 * Used by enemy planning; assumes destination has no other unit. */
static void unit_swap_to_terrain(int unit_idx, int new_col, int new_row)
{
    Unit *u = &s_units[unit_idx];
    if (new_col == u->col && new_row == u->row) return;
    Terrain displaced = s_map[new_row][new_col];
    s_map[u->row][u->col] = displaced;

    for (int fi = 0; fi < MAX_FLYING_TILES; fi++) {
        if (!s_flying[fi].active) {
            s_flying[fi] = (FlyingTile){
                .active  = true,
                .terrain = displaced,
                .vis_col = (float)new_col,
                .vis_row = (float)new_row,
                .dst_col = (float)u->col,
                .dst_row = (float)u->row,
            };
            s_cell_flying_dst[u->row][u->col] = true;
            break;
        }
    }

    u->col  = new_col;
    u->row  = new_row;
    u->bump = BUMP_SCALE_PEAK - 1.0f;
}

/* ------------------------------------------------------ unit / spawn helpers */

static int class_max_hp(UnitKind k)
{
    switch (k) {
    case UNIT_BRUTE:           return HP_BRUTE;
    case UNIT_ROGUE:           return HP_ROGUE;
    case UNIT_MAGE:            return HP_MAGE;
    case UNIT_VILLAIN_STALKER:
    case UNIT_VILLAIN_SWARMER: return GetRandomValue(HP_VILLAIN_MIN, HP_VILLAIN_MAX);
    case UNIT_MINION:          return HP_MINION;
    default:                   return 1;
    }
}

static void spawn_unit(UnitKind kind, int col, int row)
{
    if (s_unit_count >= MAX_UNITS) return;
    if (col < 0 || col >= MAP_COLS || row < 0 || row >= MAP_ROWS) return;
    if (unit_at(col, row) >= 0) return;
    Unit *u = &s_units[s_unit_count++];
    u->kind             = kind;
    u->alive            = true;
    u->col              = col;
    u->row              = row;
    u->pre_drag_col     = col;
    u->pre_drag_row     = row;
    u->max_hp           = class_max_hp(kind);
    u->hp               = u->max_hp;
    u->damage_bonus     = 0;
    u->buff_villain_idx = -1;
    u->buff_kind        = 0;
    u->buff_amount      = 0;
    u->vis_col          = (float)col;
    u->vis_row          = (float)row;
    u->bump             = 0.0f;
}

/* ----------------------------------------------------------- map / walkable */

static bool cell_is_walkable(int col, int row)
{
    if (col < 0 || col >= MAP_COLS) return false;
    if (row < 0 || row >= MAP_ROWS) return false;
    return !s_obstacle[row][col];
}

/* Generate the obstacle layout: ~15% scatter plus a handful of small
 * rectangle "wall" blobs for visual variety. Cells in `keep_clear` are
 * forced walkable so initial spawns aren't buried. */
static void generate_obstacles(const int *keep_clear_col, const int *keep_clear_row, int keep_n)
{
    /* uniform scatter */
    for (int r = 0; r < MAP_ROWS; r++)
        for (int c = 0; c < MAP_COLS; c++)
            s_obstacle[r][c] = (GetRandomValue(0, 99) < OBSTACLE_DENSITY_PCT);

    /* a few small wall blobs (2x1, 1x2, 2x2) */
    int blob_count = 5;
    for (int b = 0; b < blob_count; b++) {
        int sw = GetRandomValue(1, 2);
        int sh = GetRandomValue(1, 2);
        int c0 = GetRandomValue(0, MAP_COLS - sw);
        int r0 = GetRandomValue(0, MAP_ROWS - sh);
        for (int rr = 0; rr < sh; rr++)
            for (int cc = 0; cc < sw; cc++)
                s_obstacle[r0 + rr][c0 + cc] = true;
    }

    /* clear the spawn cells (and a 1-tile breathing border around them) */
    for (int i = 0; i < keep_n; i++) {
        int c = keep_clear_col[i];
        int r = keep_clear_row[i];
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++) {
                int nc = c + dc, nr = r + dr;
                if (nc < 0 || nc >= MAP_COLS || nr < 0 || nr >= MAP_ROWS) continue;
                s_obstacle[nr][nc] = false;
            }
    }
}

/* --------------------------------------------------------- attack targeting */

/* All living units other than `self_idx` whose (col, row) match one of the
 * cells in `cells_col`/`cells_row` (n entries). Returns count, fills `out`. */
static int collect_units_on_cells(int self_idx,
                                   const int *cells_col, const int *cells_row, int n,
                                   int *out)
{
    int found = 0;
    for (int i = 0; i < s_unit_count && found < MAX_UNITS; i++) {
        if (i == self_idx) continue;
        Unit *u = &s_units[i];
        if (!u->alive) continue;
        for (int k = 0; k < n; k++) {
            if (u->col == cells_col[k] && u->row == cells_row[k]) {
                out[found++] = i;
                break;
            }
        }
    }
    return found;
}

static int compute_brute_cells(int idx, int *col_out, int *row_out)
{
    Unit *u = &s_units[idx];
    static const int dxs[4] = {  1, -1, 0,  0 };
    static const int dys[4] = {  0,  0, 1, -1 };
    int n = 0;
    for (int d = 0; d < 4; d++) {
        int c = u->col + dxs[d], r = u->row + dys[d];
        if (c < 0 || c >= MAP_COLS || r < 0 || r >= MAP_ROWS) continue;
        col_out[n] = c; row_out[n] = r; n++;
    }
    return n;
}

static int compute_mage_cells(int idx, int *col_out, int *row_out)
{
    Unit *u = &s_units[idx];
    int n = 0;
    for (int c = 0; c < MAP_COLS; c++) {
        if (c == u->col) continue;
        if (abs(c - u->col) == 1) continue;   /* skip the adjacent N/E/S/W */
        col_out[n] = c; row_out[n] = u->row; n++;
    }
    for (int r = 0; r < MAP_ROWS; r++) {
        if (r == u->row) continue;
        if (abs(r - u->row) == 1) continue;
        col_out[n] = u->col; row_out[n] = r; n++;
    }
    return n;
}

/* Rogue: if moved, the recorded drag path; otherwise 4-adjacent. */
static int compute_rogue_cells(int idx, bool moved, int *col_out, int *row_out)
{
    if (moved && s_rogue_path_n > 0) {
        int n = s_rogue_path_n;
        if (n > MAX_UNITS) n = MAX_UNITS;
        for (int i = 0; i < n; i++) {
            col_out[i] = s_rogue_path_col[i];
            row_out[i] = s_rogue_path_row[i];
        }
        return n;
    }
    return compute_brute_cells(idx, col_out, row_out);
}

/* ----------------------------------------------------------- damage / kills */

/* Unwind a minion's buff on its target villain (if both alive / valid). */
static void unwind_minion_buff(int minion_idx)
{
    Unit *m = &s_units[minion_idx];
    if (m->buff_villain_idx < 0) return;
    int vi = m->buff_villain_idx;
    if (vi < 0 || vi >= s_unit_count) return;
    Unit *v = &s_units[vi];
    if (m->buff_kind == 0) {
        /* +damage buff */
        v->damage_bonus -= m->buff_amount;
        if (v->damage_bonus < 0) v->damage_bonus = 0;
    } else {
        /* +HP buff */
        v->max_hp -= m->buff_amount;
        if (v->max_hp < 1) v->max_hp = 1;
        if (v->hp > v->max_hp) v->hp = v->max_hp;
    }
    m->buff_villain_idx = -1;
}

static void damage_unit(int idx, int dmg)
{
    if (idx < 0 || idx >= s_unit_count) return;
    Unit *u = &s_units[idx];
    if (!u->alive) return;
    u->hp -= dmg;
    if (u->hp <= 0) {
        u->hp    = 0;
        u->alive = false;
        if (u->kind == UNIT_MINION) unwind_minion_buff(idx);
    }
}

/* Resolve all player attacks (Brute → Rogue → Mage) and apply damage. */
static void resolve_player_attacks(void)
{
    for (int pass = 0; pass < 3; pass++) {
        UnitKind want = (pass == 0) ? UNIT_BRUTE
                        : (pass == 1) ? UNIT_ROGUE
                                      : UNIT_MAGE;
        for (int i = 0; i < s_unit_count; i++) {
            Unit *u = &s_units[i];
            if (!u->alive) continue;
            if (u->kind != want) continue;

            bool moved = (u->col != u->pre_drag_col || u->row != u->pre_drag_row);

            /* Rogue always attacks. Brute / Mage only if moved this turn. */
            if (want != UNIT_ROGUE && !moved) continue;

            int cells_col[MAP_COLS + MAP_ROWS + ROGUE_PATH_MAX];
            int cells_row[MAP_COLS + MAP_ROWS + ROGUE_PATH_MAX];
            int n = 0;
            if (want == UNIT_BRUTE) n = compute_brute_cells(i, cells_col, cells_row);
            else if (want == UNIT_MAGE)  n = compute_mage_cells(i, cells_col, cells_row);
            else                         n = compute_rogue_cells(i, moved, cells_col, cells_row);

            int targets[MAX_UNITS];
            int tn = collect_units_on_cells(i, cells_col, cells_row, n, targets);
            if (tn == 0) continue;

            if (want == UNIT_BRUTE) {
                /* 4 to single highest-HP target, 2 to all others. */
                int max_hp = -1, max_idx = -1;
                for (int k = 0; k < tn; k++) {
                    if (s_units[targets[k]].hp > max_hp) {
                        max_hp  = s_units[targets[k]].hp;
                        max_idx = targets[k];
                    }
                }
                for (int k = 0; k < tn; k++) {
                    int dmg = (targets[k] == max_idx) ? 4 : 2;
                    damage_unit(targets[k], dmg);
                    if (s_last_attack_count < MAX_ATTACKS) {
                        s_last_attacks[s_last_attack_count++] = (AttackHit){
                            .col = s_units[targets[k]].col,
                            .row = s_units[targets[k]].row,
                            .damage = dmg,
                            .by_player = true,
                        };
                    }
                }
            } else if (want == UNIT_MAGE) {
                int dmg = tn;
                for (int k = 0; k < tn; k++) {
                    int cc = s_units[targets[k]].col;
                    int rr = s_units[targets[k]].row;
                    damage_unit(targets[k], dmg);
                    if (s_last_attack_count < MAX_ATTACKS) {
                        s_last_attacks[s_last_attack_count++] = (AttackHit){
                            .col = cc, .row = rr, .damage = dmg, .by_player = true,
                        };
                    }
                }
            } else { /* ROGUE */
                /* 2 to each target. Then execute any *enemy* with HP <
                 * (count of enemy targets). Count enemies pre-execute on
                 * the snapshot, then apply executes. */
                int enemy_count = 0;
                for (int k = 0; k < tn; k++)
                    if (IS_ENEMY_KIND(s_units[targets[k]].kind)) enemy_count++;
                for (int k = 0; k < tn; k++) {
                    int cc = s_units[targets[k]].col;
                    int rr = s_units[targets[k]].row;
                    damage_unit(targets[k], 2);
                    if (s_last_attack_count < MAX_ATTACKS) {
                        s_last_attacks[s_last_attack_count++] = (AttackHit){
                            .col = cc, .row = rr, .damage = 2, .by_player = true,
                        };
                    }
                }
                /* Execute pass: kill living enemy targets with hp < enemy_count. */
                for (int k = 0; k < tn; k++) {
                    Unit *t = &s_units[targets[k]];
                    if (!t->alive) continue;
                    if (!IS_ENEMY_KIND(t->kind)) continue;
                    if (t->hp < enemy_count) {
                        damage_unit(targets[k], t->hp);
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------- villain attacks */

/* Closest living player unit to (col, row). -1 if none. */
static int closest_player_to(int col, int row)
{
    int best = -1, best_dist = INT_MAX;
    for (int i = 0; i < s_unit_count; i++) {
        Unit *u = &s_units[i];
        if (!u->alive) continue;
        if (!IS_PLAYER_KIND(u->kind)) continue;
        int d = abs(u->col - col) + abs(u->row - row);
        if (d < best_dist) { best_dist = d; best = i; }
    }
    return best;
}

static int random_living_player(void)
{
    int n = count_living(UNIT_BRUTE) + count_living(UNIT_ROGUE) + count_living(UNIT_MAGE);
    if (n == 0) return -1;
    int pick = GetRandomValue(0, n - 1);
    for (int i = 0; i < s_unit_count; i++) {
        Unit *u = &s_units[i];
        if (!u->alive) continue;
        if (!IS_PLAYER_KIND(u->kind)) continue;
        if (pick == 0) return i;
        pick--;
    }
    return -1;
}

static int random_living_villain(void)
{
    int n = count_living(UNIT_VILLAIN_STALKER) + count_living(UNIT_VILLAIN_SWARMER);
    if (n == 0) return -1;
    int pick = GetRandomValue(0, n - 1);
    for (int i = 0; i < s_unit_count; i++) {
        Unit *u = &s_units[i];
        if (!u->alive) continue;
        if (!IS_VILLAIN_KIND(u->kind)) continue;
        if (pick == 0) return i;
        pick--;
    }
    return -1;
}

static void resolve_villain_attacks(void)
{
    for (int i = 0; i < s_unit_count; i++) {
        Unit *v = &s_units[i];
        if (!v->alive) continue;
        if (!IS_VILLAIN_KIND(v->kind)) continue;
        int n = s_villain_tgt_n[i];
        int dmg = (v->kind == UNIT_VILLAIN_STALKER ? DMG_STALKER : DMG_SWARMER) + v->damage_bonus;
        for (int k = 0; k < n; k++) {
            int tc = s_villain_tgt_col[i][k];
            int tr = s_villain_tgt_row[i][k];
            int tgt = unit_at(tc, tr);
            if (tgt >= 0) damage_unit(tgt, dmg);
            if (s_last_attack_count < MAX_ATTACKS) {
                s_last_attacks[s_last_attack_count++] = (AttackHit){
                    .col = tc, .row = tr, .damage = dmg, .by_player = false,
                };
            }
        }
    }
}

/* --------------------------------------------------- minion spawn + buffs */

static void spawn_minions(void)
{
    for (int n = 0; n < MINIONS_PER_TURN; n++) {
        /* pick a random empty walkable cell */
        int c = -1, r = -1;
        for (int tries = 0; tries < 200; tries++) {
            int cc = GetRandomValue(0, MAP_COLS - 1);
            int rr = GetRandomValue(0, MAP_ROWS - 1);
            if (!cell_is_walkable(cc, rr)) continue;
            if (unit_at(cc, rr) >= 0) continue;
            c = cc; r = rr; break;
        }
        if (c < 0) return;

        int before = s_unit_count;
        spawn_unit(UNIT_MINION, c, r);
        if (s_unit_count == before) return;
        int mi = s_unit_count - 1;
        Unit *m = &s_units[mi];

        /* Pick a random villain to buff. */
        int vi = random_living_villain();
        if (vi < 0) {
            m->buff_villain_idx = -1;
            continue;
        }
        Unit *v = &s_units[vi];
        int kind = GetRandomValue(0, 1);   /* 0 = +damage, 1 = +HP */
        m->buff_villain_idx = vi;
        m->buff_kind        = kind;
        if (kind == 0) {
            m->buff_amount = MINION_DMG_BUFF;
            v->damage_bonus += MINION_DMG_BUFF;
        } else {
            m->buff_amount = MINION_HP_BUFF;
            v->max_hp += MINION_HP_BUFF;
            v->hp     += MINION_HP_BUFF;
        }
    }
}

/* ---------------------------------------------------------- villain planning */

static void enemy_plan_one(int v_idx)
{
    Unit *e = &s_units[v_idx];
    if (!e->alive) return;
    if (!IS_VILLAIN_KIND(e->kind)) return;

    static const int dxs[4] = {  1, -1, 0,  0 };
    static const int dys[4] = {  0,  0, 1, -1 };

    /* Movement: try to swap onto a walkable tile adjacent to the relevant
     * player. For Stalker this is the closest player; for Swarmer use a
     * random living player just for positioning. */
    int focus_idx = (e->kind == UNIT_VILLAIN_STALKER)
                       ? closest_player_to(e->col, e->row)
                       : random_living_player();
    if (focus_idx >= 0) {
        Unit *t = &s_units[focus_idx];
        int best_col = -1, best_row = -1, best_dist = INT_MAX;
        for (int d = 0; d < 4; d++) {
            int nc = t->col + dxs[d], nr = t->row + dys[d];
            if (!cell_is_walkable(nc, nr)) continue;
            int occ = unit_at(nc, nr);
            if (occ >= 0 && occ != v_idx) continue;
            int dist = abs(nc - e->col) + abs(nr - e->row);
            if (dist < best_dist) {
                best_dist = dist; best_col = nc; best_row = nr;
            }
        }
        if (best_col >= 0)
            unit_swap_to_terrain(v_idx, best_col, best_row);
    }

    /* Fallback hop: walled in (no focus or no valid adjacency) — random walk. */
    if (focus_idx < 0) {
        for (int tries = 0; tries < 100; tries++) {
            int nc = GetRandomValue(0, MAP_COLS - 1);
            int nr = GetRandomValue(0, MAP_ROWS - 1);
            if (!cell_is_walkable(nc, nr)) continue;
            int occ = unit_at(nc, nr);
            if (occ >= 0 && occ != v_idx) continue;
            unit_swap_to_terrain(v_idx, nc, nr);
            break;
        }
    }

    /* Telegraph: write target list for next round. */
    s_villain_tgt_n[v_idx] = 0;
    if (e->kind == UNIT_VILLAIN_STALKER) {
        int tgt = closest_player_to(e->col, e->row);
        if (tgt >= 0) {
            s_villain_tgt_col[v_idx][0] = s_units[tgt].col;
            s_villain_tgt_row[v_idx][0] = s_units[tgt].row;
            s_villain_tgt_n[v_idx]      = 1;
        }
    } else {
        int n = 0;
        for (int i = 0; i < s_unit_count && n < MAX_TGTS; i++) {
            Unit *u = &s_units[i];
            if (!u->alive || !IS_PLAYER_KIND(u->kind)) continue;
            s_villain_tgt_col[v_idx][n] = u->col;
            s_villain_tgt_row[v_idx][n] = u->row;
            n++;
        }
        s_villain_tgt_n[v_idx] = n;
    }
}

static void enemy_plan_turn(void)
{
    for (int i = 0; i < s_unit_count; i++)
        if (s_units[i].alive && IS_VILLAIN_KIND(s_units[i].kind))
            enemy_plan_one(i);
}

/* ----------------------------------------------------- wave clear / spawn */

static void clear_living_minions_and_buffs(void)
{
    for (int i = 0; i < s_unit_count; i++) {
        Unit *m = &s_units[i];
        if (!m->alive) continue;
        if (m->kind != UNIT_MINION) continue;
        /* Unwind buff (no-op if villain already dead). */
        unwind_minion_buff(i);
        m->alive = false;
        m->hp    = 0;
    }
}

static void recharge_players(void)
{
    for (int i = 0; i < s_unit_count; i++) {
        Unit *u = &s_units[i];
        if (!u->alive) continue;
        if (!IS_PLAYER_KIND(u->kind)) continue;
        u->hp = u->max_hp;
    }
}

/* Pick the closest walkable empty cell to (col, row), or return false. */
static bool nearest_open_cell(int col, int row, int *out_c, int *out_r)
{
    for (int radius = 0; radius < 6; radius++) {
        for (int dr = -radius; dr <= radius; dr++) {
            for (int dc = -radius; dc <= radius; dc++) {
                if (abs(dc) != radius && abs(dr) != radius) continue;  /* shell only */
                int nc = col + dc, nr = row + dr;
                if (!cell_is_walkable(nc, nr)) continue;
                if (unit_at(nc, nr) >= 0) continue;
                *out_c = nc; *out_r = nr;
                return true;
            }
        }
    }
    return false;
}

static void spawn_villains_for_wave(void)
{
    int cx = MAP_COLS / 2;
    int cy = MAP_ROWS / 2;
    int c, r;
    if (nearest_open_cell(cx + 2, cy,     &c, &r)) spawn_unit(UNIT_VILLAIN_STALKER, c, r);
    if (nearest_open_cell(cx + 2, cy + 1, &c, &r)) spawn_unit(UNIT_VILLAIN_SWARMER, c, r);
}

static bool wave_clear_if_done(void)
{
    if (count_living(UNIT_VILLAIN_STALKER) + count_living(UNIT_VILLAIN_SWARMER) > 0)
        return false;
    s_wave_index++;
    recharge_players();
    clear_living_minions_and_buffs();
    spawn_villains_for_wave();
    return true;
}

/* ---------------------------------------------------------- round resolve */

static void resolve_round(void)
{
    s_last_attack_count = 0;

    /* 1. Player attacks. */
    resolve_player_attacks();
    s_resolve_flash = RESOLVE_FLASH_SECS;

    /* 2. Wave-clear short-circuits the round (no villain swing, no minion spawn). */
    bool wave_cleared = wave_clear_if_done();

    if (!wave_cleared) {
        /* 3. Villain attacks. */
        resolve_villain_attacks();

        /* 4. Lose check. */
        if (count_living(UNIT_BRUTE) + count_living(UNIT_ROGUE) + count_living(UNIT_MAGE) == 0) {
            s_player_lost = true;
            s_game_over   = true;
            return;
        }

        /* 5. Spawn minions. */
        spawn_minions();
    } else {
        /* Wave-clear edge case: every player dropped to 0 in their own attack
         * pass (e.g. mage friendly-fired everyone). Still a defeat. */
        if (count_living(UNIT_BRUTE) + count_living(UNIT_ROGUE) + count_living(UNIT_MAGE) == 0) {
            s_player_lost = true;
            s_game_over   = true;
            return;
        }
    }

    /* 6. Plan next round (villain telegraphs). */
    enemy_plan_turn();

    /* Active player may have just died — drop the drag handle. */
    if (s_active_player_idx >= 0 && !s_units[s_active_player_idx].alive)
        s_active_player_idx = -1;
    if (s_inspect_idx >= 0 && !s_units[s_inspect_idx].alive)
        s_inspect_idx = -1;
}

static void end_turn(void)
{
    s_is_dragging       = false;
    s_active_player_idx = -1;
    s_inspect_idx       = -1;
    s_flash_count       = 0;
    resolve_round();
    s_phase      = PHASE_SCANNING;
    s_scan_flash = s_resolve_flash;
    s_turn_timer = TURN_SECS;
}

/* ------------------------------------------------------------- map generation */

static void generate_map(void)
{
    /* No more terrain types — keep s_map zeroed for legacy code that still
     * indexes it (e.g. dead structure helpers, unit_swap_to_terrain). */
    for (int r = 0; r < MAP_ROWS; r++)
        for (int c = 0; c < MAP_COLS; c++)
            s_map[r][c] = TERRAIN_PLAINS;
}

/* ----------------------------------------------------------- input callbacks */

static void on_down(Vector2 pos)
{
    if (s_game_over) return;
    if (s_phase != PHASE_IDLE) return;

    int col, row;
    if (!screen_to_tile(pos, &col, &row)) return;
    int hit = unit_at(col, row);
    if (hit < 0) return;
    if (!IS_PLAYER_KIND(s_units[hit].kind)) return;

    s_active_player_idx = hit;
    s_hero_col = s_units[hit].col;
    s_hero_row = s_units[hit].row;

    /* Snapshot every unit's pre-drag position so swap-displacement still
     * counts as 'moved' for attack triggering at resolve time. */
    for (int i = 0; i < s_unit_count; i++) {
        s_units[i].pre_drag_col = s_units[i].col;
        s_units[i].pre_drag_row = s_units[i].row;
    }

    /* Seed the rogue path with its current cell (so its start counts as a target). */
    s_rogue_path_n = 0;
    if (s_units[hit].kind == UNIT_ROGUE) {
        s_rogue_path_col[s_rogue_path_n] = s_units[hit].col;
        s_rogue_path_row[s_rogue_path_n] = s_units[hit].row;
        s_rogue_path_n++;
    }

    s_is_dragging = true;
    s_turn_timer  = TURN_SECS;
    s_phase       = PHASE_DRAGGING;
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
        /* Detect a tap (touch-down on a player, release without moving):
         * open the unit's info card instead of consuming the turn. */
        int idx = s_active_player_idx;
        bool tap = (idx >= 0)
                && s_units[idx].col == s_units[idx].pre_drag_col
                && s_units[idx].row == s_units[idx].pre_drag_row;
        if (tap) {
            s_inspect_idx = (s_inspect_idx == idx) ? -1 : idx;
            s_is_dragging       = false;
            s_active_player_idx = -1;
            s_phase             = PHASE_IDLE;
            s_turn_timer        = TURN_SECS;
            s_trail_len         = 0;
            return;
        }
        end_turn();
        return;
    }

    /* Tap on empty space dismisses the inspect card. */
    if (s_inspect_idx >= 0) s_inspect_idx = -1;

    /* HUD tab toggle */
    if (CheckCollisionPointRec(pos, hud_tab_rect())) {
        s_hud_open = !s_hud_open;
        return;
    }

    /* Nav buttons: 0-3 pan the viewport, 4 toggles the view lock.
     * Pan arrows are inert while the view is locked. */
    int btn = touch_nav_btn(pos);
    if (btn >= 0) {
        if (btn == NAV_BTN_LOCK) {
            s_view_locked = !s_view_locked;
        } else if (!s_view_locked) {
            if      (btn == 0) s_vp_col--;
            else if (btn == 1) s_vp_row--;
            else if (btn == 2) s_vp_row++;
            else if (btn == 3) s_vp_col++;
            clamp_viewport();
        }
        return;
    }

    /* legend tab toggle */
    if (CheckCollisionPointRec(pos, legend_tab_rect())) {
        s_legend_open = !s_legend_open;
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

/* Draw a terrain-specific icon centered at (cx, cy), scaled by s (= tw/3). */
static void draw_terrain_icon(int cx, int cy, int s, Terrain t, Color c)
{
    switch (t) {
    case TERRAIN_PLAINS:
        /* circle dot */
        DrawCircle(cx, cy, s * 42 / 100, c);
        break;
    case TERRAIN_FOREST:
        /* tree: upward triangle canopy + small trunk */
        DrawTriangle(
            (Vector2){ (float)cx,           (float)(cy - s * 58 / 100) },
            (Vector2){ (float)(cx + s),     (float)(cy + s * 42 / 100) },
            (Vector2){ (float)(cx - s),     (float)(cy + s * 42 / 100) }, c);
        DrawRectangle(cx - s * 14 / 100, cy + s * 42 / 100,
                      s * 28 / 100, s * 22 / 100, c);
        break;
    case TERRAIN_MOUNTAIN:
        /* main peak */
        DrawTriangle(
            (Vector2){ (float)cx,             (float)(cy - s * 68 / 100) },
            (Vector2){ (float)(cx + s),       (float)(cy + s * 32 / 100) },
            (Vector2){ (float)(cx - s),       (float)(cy + s * 32 / 100) }, c);
        /* second smaller peak behind-left */
        DrawTriangle(
            (Vector2){ (float)(cx - s * 42 / 100), (float)(cy - s * 28 / 100) },
            (Vector2){ (float)(cx + s * 10 / 100), (float)(cy + s * 32 / 100) },
            (Vector2){ (float)(cx - s * 115 / 100),(float)(cy + s * 32 / 100) },
            (Color){ c.r, c.g, c.b, 150 });
        break;
    case TERRAIN_CITY:
        /* building: wide base + narrow tower */
        DrawRectangle(cx - s * 46 / 100, cy - s * 15 / 100,
                      s * 92 / 100, s * 50 / 100, c);
        DrawRectangle(cx - s * 16 / 100, cy - s * 58 / 100,
                      s * 32 / 100, s * 43 / 100, c);
        /* window cutout */
        DrawRectangle(cx - s * 10 / 100, cy - s *  8 / 100,
                      s * 20 / 100, s * 28 / 100, (Color){ 0, 0, 0, 200 });
        break;
    case TERRAIN_WATER:
        /* two staggered wave bars */
        {
            int wh = s * 18 / 100; if (wh < 2) wh = 2;
            int ww = s * 56 / 100; if (ww < 3) ww = 3;
            DrawRectangle(cx - ww - ww / 4, cy - wh * 2, ww, wh, c);
            DrawRectangle(cx + ww / 4,      cy - wh * 2, ww, wh, (Color){ c.r, c.g, c.b, 180 });
            DrawRectangle(cx - ww,          cy,           ww, wh, c);
            DrawRectangle(cx,               cy,           ww, wh, (Color){ c.r, c.g, c.b, 180 });
        }
        break;
    default: break;
    }
}

static void draw_map_tiles(void)
{
    int ts = tile_px();
    int vc = vis_cols();
    int vr = vis_rows();
    int gx = grid_x();
    int gy = grid_y();

    BeginScissorMode(gx, gy, vc * ts, vr * ts);

    /* Solid neutral fill for the walkable cells; distinct charcoal block for
     * obstacles. Expand loop by 1 each side for viewport-lerp coverage. */
    const Color WALK     = (Color){  28,  34,  46, 255 };
    const Color WALK_EDG = (Color){  60,  72,  92, 220 };
    const Color BLOCK    = (Color){  60,  18,  18, 255 };
    const Color BLOCK_EDG= (Color){ 200,  60,  60, 200 };
    int inset = ts / 12; if (inset < 1) inset = 1;
    int tw    = ts - inset * 2;
    for (int row = s_vp_row - 1; row < s_vp_row + vr + 1 && row < MAP_ROWS; row++) {
        if (row < 0) continue;
        for (int col = s_vp_col - 1; col < s_vp_col + vc + 1 && col < MAP_COLS; col++) {
            if (col < 0) continue;
            int sx = (int)((col - s_vp_vis_col) * ts) + inset + gx;
            int sy = (int)((row - s_vp_vis_row) * ts) + inset + gy;
            bool block = s_obstacle[row][col];
            DrawRectangle(sx, sy, tw, tw, block ? BLOCK : WALK);
            DrawRectangleLinesEx(
                (Rectangle){ (float)sx, (float)sy, (float)tw, (float)tw },
                1.5f, block ? BLOCK_EDG : WALK_EDG);
            if (block) {
                /* small X mark to read clearly as 'impassable' */
                int m = tw / 4;
                int cx = sx + tw / 2;
                int cy = sy + tw / 2;
                DrawLineEx((Vector2){ (float)(cx - m), (float)(cy - m) },
                           (Vector2){ (float)(cx + m), (float)(cy + m) },
                           2.0f, BLOCK_EDG);
                DrawLineEx((Vector2){ (float)(cx + m), (float)(cy - m) },
                           (Vector2){ (float)(cx - m), (float)(cy + m) },
                           2.0f, BLOCK_EDG);
            }
        }
    }

    EndScissorMode();
}

static void draw_trail(void)
{
    if (!s_is_dragging || s_trail_len == 0) return;
    int ts = tile_px();
    int gx = grid_x();
    int gy = grid_y();
    BeginScissorMode(gx, gy, VIEW_COLS * ts, VIEW_ROWS * ts);
    for (int i = 0; i < s_trail_len; i++) {
        int col = s_trail_col[i];
        int row = s_trail_row[i];
        /* cull using logical vp with ±1 buffer to handle pan lag */
        if (col < s_vp_col - 1 || col >= s_vp_col + vis_cols() + 1) continue;
        if (row < s_vp_row - 1 || row >= s_vp_row + vis_rows() + 1) continue;
        int sx = (int)((col - s_vp_vis_col) * ts) + gx;
        int sy = (int)((row - s_vp_vis_row) * ts) + gy;

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
    EndScissorMode();
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
    int gx = grid_x();
    int gy = grid_y();

    BeginScissorMode(gx, gy, vc * ts, vr * ts);
    for (int i = 0; i < s_flash_count; i++) {
        int col = s_flash_col[i];
        int row = s_flash_row[i];
        if (col < s_vp_col || col >= s_vp_col + vc) continue;
        if (row < s_vp_row || row >= s_vp_row + vr) continue;
        int sx = (int)((col - s_vp_vis_col) * ts) + gx;
        int sy = (int)((row - s_vp_vis_row) * ts) + gy;
        /* bright white flash over the replaced tile */
        DrawRectangle(sx, sy, ts - 1, ts - 1, (Color){ 255, 255, 200, a });
        /* sparkle border */
        DrawRectangleLinesEx(
            (Rectangle){ (float)sx, (float)sy, (float)(ts - 1), (float)(ts - 1) },
            2.0f, (Color){ 255, 220, 60, (unsigned char)(a * 0.8f) });
    }
    EndScissorMode();
}

static void draw_flying_tiles(void)
{
    int ts = tile_px();
    int gx = grid_x();
    int gy = grid_y();
    int gw = VIEW_COLS * ts;
    int gh = VIEW_ROWS * ts;
    BeginScissorMode(gx, gy, gw, gh);
    for (int _fi = 0; _fi < MAX_FLYING_TILES; _fi++) {
        FlyingTile *ft = &s_flying[_fi];
        if (!ft->active) continue;
        int sx = (int)((ft->vis_col - s_vp_vis_col) * ts) + gx;
        int sy = (int)((ft->vis_row - s_vp_vis_row) * ts) + gy;
        if (sx + ts < gx || sx > gx + gw || sy + ts < gy || sy > gy + gh) continue;
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
    EndScissorMode();
}

static void draw_hero(void)
{
    int   ts    = tile_px();
    int   gx    = grid_x();
    int   gy    = grid_y();
    float scale = 1.0f + s_hero_bump;

    BeginScissorMode(gx, gy, VIEW_COLS * ts, VIEW_ROWS * ts);

    /* visual float position → pixel center */
    float cx = (s_hero_vis_col - s_vp_vis_col) * ts + ts * 0.5f + gx;
    float cy = (s_hero_vis_row - s_vp_vis_row) * ts + ts * 0.5f + gy;

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

    EndScissorMode();
}

/* Render a single unit tile at its visual position. */
static void draw_unit_glyph(float vis_col, float vis_row, float bump,
                             const char *glyph,
                             Color fill, Color border, Color glow, Color glyph_col)
{
    int   ts    = tile_px();
    int   gx    = grid_x();
    int   gy    = grid_y();
    float scale = 1.0f + bump;
    float cx = (vis_col - s_vp_vis_col) * ts + ts * 0.5f + gx;
    float cy = (vis_row - s_vp_vis_row) * ts + ts * 0.5f + gy;
    float w  = (ts - 1) * scale;
    float h  = (ts - 1) * scale;
    float sx = cx - w * 0.5f;
    float sy = cy - h * 0.5f;
    float gw = w + 6.0f * scale;
    DrawRectangleRounded(
        (Rectangle){ cx - gw * 0.5f, cy - gw * 0.5f, gw, gw },
        0.30f, 6, glow);
    DrawRectangleRounded((Rectangle){ sx, sy, w, h }, 0.22f, 6, fill);
    DrawRectangleLinesEx((Rectangle){ sx, sy, w, h }, 2.5f, border);
    int fs = (int)(ts * 36 / 100 * scale);
    int lw = MeasureText(glyph, fs);
    DrawText(glyph, (int)(cx - lw * 0.5f), (int)(cy - fs * 0.5f),
             fs, glyph_col);
}

/* Small horizontal HP bar above a tile (vis_col, vis_row). */
static void draw_hp_bar(float vis_col, float vis_row, int hp, int max_hp)
{
    if (max_hp <= 0) return;
    int ts = tile_px();
    int gx = grid_x();
    int gy = grid_y();
    int sx = (int)((vis_col - s_vp_vis_col) * ts) + gx + ts / 8;
    int sy = (int)((vis_row - s_vp_vis_row) * ts) + gy + 2;
    int bw = ts - ts / 4;
    int bh = (int)(ts * 0.10f); if (bh < 4) bh = 4;
    float frac = (float)hp / (float)max_hp;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    Color back = (Color){ 30, 20, 24, 220 };
    Color front = frac > 0.6f ? (Color){  90, 230, 110, 255 }
                : frac > 0.3f ? (Color){ 240, 210,  90, 255 }
                              : (Color){ 235,  90,  90, 255 };
    DrawRectangle(sx, sy, bw, bh, back);
    DrawRectangle(sx, sy, (int)(bw * frac), bh, front);
    DrawRectangleLines(sx, sy, bw, bh, (Color){ 10, 10, 14, 200 });
    /* numeric overlay so exact HP is legible */
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", hp);
    int fs = bh + 2; if (fs < 10) fs = 10;
    int lw = MeasureText(buf, fs);
    DrawText(buf, sx + bw - lw - 2, sy - fs - 1, fs, (Color){ 240, 240, 240, 240 });
}

/* Small "+N" badge for a villain's damage bonus (drawn above the HP bar). */
static void draw_dmg_bonus_badge(float vis_col, float vis_row, int bonus)
{
    if (bonus <= 0) return;
    int ts = tile_px();
    int gx = grid_x();
    int gy = grid_y();
    char buf[16];
    snprintf(buf, sizeof(buf), "+%d", bonus);
    int fs = (int)(ts * 0.22f); if (fs < 10) fs = 10;
    int sx = (int)((vis_col - s_vp_vis_col) * ts) + gx + ts / 8;
    int sy = (int)((vis_row - s_vp_vis_row) * ts) + gy - fs;
    DrawText(buf, sx, sy, fs, (Color){ 255, 130, 90, 240 });
}

static void draw_units(void)
{
    int ts = tile_px();
    int gx = grid_x();
    int gy = grid_y();
    BeginScissorMode(gx, gy, VIEW_COLS * ts, VIEW_ROWS * ts);

    /* Order: minions → villains → players, so players read on top. */
    static const UnitKind order[] = {
        UNIT_MINION,
        UNIT_VILLAIN_STALKER, UNIT_VILLAIN_SWARMER,
        UNIT_BRUTE, UNIT_ROGUE, UNIT_MAGE,
    };
    for (int p = 0; p < (int)(sizeof(order) / sizeof(order[0])); p++) {
        UnitKind want = order[p];
        for (int i = 0; i < s_unit_count; i++) {
            Unit *u = &s_units[i];
            if (!u->alive || u->kind != want) continue;
            const char *glyph = "?";
            Color fill = (Color){ 255, 255, 255, 255 };
            Color border = (Color){  40,  40,  60, 255 };
            Color glow   = (Color){ 255, 255, 255,  60 };
            Color gcol   = (Color){  20,  20,  20, 240 };
            switch (u->kind) {
            case UNIT_BRUTE:
                glyph = "B";
                fill   = (Color){ 200, 220, 235, 255 };
                border = (Color){  60, 100, 150, 255 };
                glow   = (Color){ 150, 200, 255,  60 };
                gcol   = (Color){  10,  30,  60, 240 };
                break;
            case UNIT_ROGUE:
                glyph = "R";
                fill   = (Color){ 215, 160, 245, 255 };
                border = (Color){ 110,  50, 170, 255 };
                glow   = (Color){ 200, 100, 255,  70 };
                gcol   = (Color){  40,   0,  60, 240 };
                break;
            case UNIT_MAGE:
                glyph = "M";
                fill   = (Color){ 255, 200, 130, 255 };
                border = (Color){ 200,  90,  30, 255 };
                glow   = (Color){ 255, 160,  60,  70 };
                gcol   = (Color){  60,  20,   0, 240 };
                break;
            case UNIT_VILLAIN_STALKER:
                glyph = "V";
                fill   = (Color){ 230,  60,  60, 255 };
                border = (Color){ 110,  10,  10, 255 };
                glow   = (Color){ 255,  70,  70, 110 };
                gcol   = (Color){  30,   0,   0, 240 };
                break;
            case UNIT_VILLAIN_SWARMER:
                glyph = "V";
                fill   = (Color){ 220,  90, 140, 255 };
                border = (Color){ 140,  20,  60, 255 };
                glow   = (Color){ 250, 100, 160, 110 };
                gcol   = (Color){  40,   0,  20, 240 };
                break;
            case UNIT_MINION:
                glyph = "m";
                fill   = (Color){ 200, 120, 120, 255 };
                border = (Color){ 100,  40,  40, 255 };
                glow   = (Color){ 240, 120, 120,  70 };
                gcol   = (Color){  40,   0,   0, 240 };
                break;
            default: break;
            }
            draw_unit_glyph(u->vis_col, u->vis_row, u->bump, glyph,
                             fill, border, glow, gcol);
            draw_hp_bar(u->vis_col, u->vis_row, u->hp, u->max_hp);
            if (IS_VILLAIN_KIND(u->kind) && u->damage_bonus > 0)
                draw_dmg_bonus_badge(u->vis_col, u->vis_row, u->damage_bonus);
        }
    }

    EndScissorMode();
}

/* Outline a cell at (col, row) in `col`. Used both for villain threat cells
 * (with a damage label) and for player attack previews. */
static void draw_cell_overlay(int col, int row, Color col_rgba, int label, float pulse)
{
    if (col < 0 || col >= MAP_COLS || row < 0 || row >= MAP_ROWS) return;
    int ts = tile_px();
    int gx = grid_x();
    int gy = grid_y();
    int sx = (int)((col - s_vp_vis_col) * ts) + gx;
    int sy = (int)((row - s_vp_vis_row) * ts) + gy;
    int tw = ts - 1;
    Color fill = (Color){ col_rgba.r, col_rgba.g, col_rgba.b, (unsigned char)(pulse * 90) };
    Color edge = (Color){ col_rgba.r, col_rgba.g, col_rgba.b, 255 };
    DrawRectangleRounded((Rectangle){ (float)sx, (float)sy, (float)tw, (float)tw },
                         0.18f, 4, fill);
    DrawRectangleLinesEx((Rectangle){ (float)sx, (float)sy, (float)tw, (float)tw },
                         3.5f + pulse * 1.5f, edge);
    if (label > 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", label);
        int fs = (int)(ts * 0.38f); if (fs < 12) fs = 12;
        int lw = MeasureText(buf, fs);
        /* shadow + text */
        DrawText(buf, sx + (tw - lw) / 2 + 1, sy + (tw - fs) / 2 + 1, fs,
                 (Color){ 0, 0, 0, 220 });
        DrawText(buf, sx + (tw - lw) / 2,     sy + (tw - fs) / 2,     fs,
                 (Color){ 255, 240, 240, 255 });
    }
}

/* Villain threat overlays: each villain's target list painted in red with
 * the incoming damage number on every target cell. */
static void draw_threats(void)
{
    if (s_phase != PHASE_IDLE && s_phase != PHASE_DRAGGING) return;
    int ts = tile_px();
    int gx = grid_x();
    int gy = grid_y();
    float pulse = 0.55f + 0.45f * sinf((float)GetTime() * 6.0f);
    BeginScissorMode(gx, gy, VIEW_COLS * ts, VIEW_ROWS * ts);
    for (int i = 0; i < s_unit_count; i++) {
        Unit *v = &s_units[i];
        if (!v->alive || !IS_VILLAIN_KIND(v->kind)) continue;
        int n = s_villain_tgt_n[i];
        int dmg = (v->kind == UNIT_VILLAIN_STALKER ? DMG_STALKER : DMG_SWARMER) + v->damage_bonus;
        Color red = (v->kind == UNIT_VILLAIN_STALKER)
                       ? (Color){ 230,  70,  70, 255 }
                       : (Color){ 240,  90, 150, 255 };
        for (int k = 0; k < n; k++)
            draw_cell_overlay(s_villain_tgt_col[i][k], s_villain_tgt_row[i][k], red, dmg, pulse);
    }
    EndScissorMode();
}

/* Render one player unit's attack-preview tiles. `strength` controls the
 * overlay alpha so the active drag stands out over the stationary peers. */
static void draw_one_player_preview(int idx, float strength)
{
    Unit *u = &s_units[idx];
    if (!u->alive) return;
    int cells_col[MAP_COLS + MAP_ROWS + ROGUE_PATH_MAX];
    int cells_row[MAP_COLS + MAP_ROWS + ROGUE_PATH_MAX];
    int n = 0;
    Color tint = (Color){ 90, 200, 255, 255 };
    if (u->kind == UNIT_BRUTE) {
        n = compute_brute_cells(idx, cells_col, cells_row);
        tint = (Color){ 130, 180, 255, 255 };
    } else if (u->kind == UNIT_MAGE) {
        n = compute_mage_cells(idx, cells_col, cells_row);
        tint = (Color){ 255, 180,  90, 255 };
    } else if (u->kind == UNIT_ROGUE) {
        bool moved = (u->col != u->pre_drag_col || u->row != u->pre_drag_row);
        n = compute_rogue_cells(idx, moved, cells_col, cells_row);
        tint = (Color){ 200, 130, 255, 255 };
    }
    for (int i = 0; i < n; i++)
        draw_cell_overlay(cells_col[i], cells_row[i], tint, 0, strength);
}

/* Player attack-preview cells. While dragging, render previews for every
 * living player unit -- the active drag gets a full-strength overlay and
 * the other players get a dimmer pass so the user can see what each
 * teammate would hit from its current cell. When idle, only the
 * inspected unit (if any) shows a preview. */
static void draw_player_previews(void)
{
    if (s_phase != PHASE_IDLE && s_phase != PHASE_DRAGGING) return;
    int ts = tile_px();
    int gx = grid_x();
    int gy = grid_y();
    BeginScissorMode(gx, gy, VIEW_COLS * ts, VIEW_ROWS * ts);

    if (s_active_player_idx >= 0) {
        /* Dim peers first so the active overlay paints on top of them. */
        for (int i = 0; i < s_unit_count; i++) {
            if (i == s_active_player_idx) continue;
            if (!IS_PLAYER_KIND(s_units[i].kind)) continue;
            draw_one_player_preview(i, 0.22f);
        }
        draw_one_player_preview(s_active_player_idx, 0.55f);
    } else if (s_inspect_idx >= 0) {
        draw_one_player_preview(s_inspect_idx, 0.55f);
    }

    EndScissorMode();
}

/* Info card for the inspected player unit: class name, HP, and a short
 * description of how this class attacks. Tiles the unit currently
 * threatens are already highlighted by draw_player_previews(). */
static void draw_inspect_card(void)
{
    if (s_inspect_idx < 0) return;
    Unit *u = &s_units[s_inspect_idx];
    if (!u->alive) return;

    const char *name = "";
    Color tint = WHITE;
    const char *line1 = "";
    const char *line2 = "";
    const char *line3 = "";
    switch (u->kind) {
    case UNIT_BRUTE:
        name  = "Brute";
        tint  = (Color){ 130, 180, 255, 255 };
        line1 = "Attack: 4 adjacent tiles (only after moving).";
        line2 = "Damage: 4 to highest-HP target, 2 to others.";
        line3 = "Tough frontline -- soak hits, lead the charge.";
        break;
    case UNIT_ROGUE:
        name  = "Rogue";
        tint  = (Color){ 200, 130, 255, 255 };
        line1 = "Attack: every tile dragged through (or 4 adjacent if stationary).";
        line2 = "Damage: 2 each. Executes any enemy below the hit-count.";
        line3 = "Reward for long, branching drags through clusters.";
        break;
    case UNIT_MAGE:
        name  = "Mage";
        tint  = (Color){ 255, 180,  90, 255 };
        line1 = "Attack: full row + column, skipping self and 4 adjacent (after moving).";
        line2 = "Damage: X to each target, where X is the number of enemies hit.";
        line3 = "Fragile -- great at culling crowded lanes.";
        break;
    default:
        return;
    }

    int sw  = GetScreenWidth();
    int sh  = GetScreenHeight();
    int pad = sw / 36; if (pad < 8) pad = 8;
    int fs_title = sh / 28; if (fs_title < 18) fs_title = 18;
    int fs_body  = sh / 40; if (fs_body  < 14) fs_body  = 14;

    int line_h = fs_body * 130 / 100;
    int title_h = fs_title * 140 / 100;
    int body_h  = line_h * 4; /* HP line + 3 description lines */
    int card_h  = title_h + body_h + pad * 2;

    int max_w = MeasureText(name, fs_title);
    int w1 = MeasureText(line1, fs_body); if (w1 > max_w) max_w = w1;
    int w2 = MeasureText(line2, fs_body); if (w2 > max_w) max_w = w2;
    int w3 = MeasureText(line3, fs_body); if (w3 > max_w) max_w = w3;
    int card_w  = max_w + pad * 2;
    if (card_w > sw - pad * 2) card_w = sw - pad * 2;

    /* Place card just above the HUD tab, centered horizontally. */
    int hud_top = sh - sh * 6 / 100 - sh * 7 / 100 - nav_btn_size() - card_h - pad;
    if (hud_top < pad) hud_top = pad;
    int x = (sw - card_w) / 2;
    int y = hud_top;

    Rectangle r = { (float)x, (float)y, (float)card_w, (float)card_h };
    DrawRectangleRec(r, (Color){ 8, 8, 12, 235 });
    DrawRectangleLinesEx(r, 2.0f, tint);

    /* Title line: "Brute   HP 13/13" */
    char hp_buf[32];
    snprintf(hp_buf, sizeof(hp_buf), "HP %d/%d", u->hp, u->max_hp);
    int hp_w = MeasureText(hp_buf, fs_title);
    int ty = y + pad;
    DrawText(name, x + pad, ty, fs_title, tint);
    Color hp_c = (u->hp > u->max_hp * 6 / 10) ? (Color){ 150, 240, 150, 240 }
               : (u->hp > u->max_hp * 3 / 10) ? (Color){ 240, 220, 110, 240 }
               :                                (Color){ 240, 110, 110, 240 };
    DrawText(hp_buf, x + card_w - pad - hp_w, ty, fs_title, hp_c);

    int by = ty + title_h;
    Color body_c = (Color){ 210, 215, 225, 235 };
    Color hint_c = (Color){ 160, 165, 180, 220 };
    DrawText(line1, x + pad, by + line_h * 0, fs_body, body_c);
    DrawText(line2, x + pad, by + line_h * 1, fs_body, body_c);
    DrawText(line3, x + pad, by + line_h * 2, fs_body, hint_c);
    DrawText("Tap unit again or anywhere else to close.",
             x + pad, by + line_h * 3, fs_body, hint_c);
}

/* Brief flash on the just-resolved attack target cells. Re-uses
 * s_scan_flash as its countdown so the existing animation update
 * machinery handles it. */
static void draw_resolve_flash(void)
{
    if (s_phase != PHASE_SCANNING || s_scan_flash <= 0.0f) return;
    int ts = tile_px();
    int gx = grid_x();
    int gy = grid_y();
    float frac  = s_scan_flash / RESOLVE_FLASH_SECS;
    if (frac > 1.0f) frac = 1.0f;
    float pulse = 0.5f + 0.5f * sinf(frac * 3.14159f * 4.0f);
    unsigned char a = (unsigned char)(pulse * 230.0f);
    BeginScissorMode(gx, gy, VIEW_COLS * ts, VIEW_ROWS * ts);
    for (int i = 0; i < s_last_attack_count; i++) {
        AttackHit *h = &s_last_attacks[i];
        Color c = h->by_player
                    ? (Color){ 120, 230, 255, a }
                    : (Color){ 255,  80,  80, a };
        int sx = (int)((h->col - s_vp_vis_col) * ts) + gx;
        int sy = (int)((h->row - s_vp_vis_row) * ts) + gy;
        int tw = ts - 1;
        DrawRectangle(sx, sy, tw, tw, c);
        DrawRectangleLinesEx((Rectangle){ (float)sx, (float)sy,
                                          (float)tw, (float)tw },
                             3.0f, (Color){ 255, 255, 255, a });
        if (h->damage > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "-%d", h->damage);
            int fs = (int)(ts * 0.40f); if (fs < 14) fs = 14;
            int lw = MeasureText(buf, fs);
            DrawText(buf, sx + (tw - lw) / 2 + 1, sy + (tw - fs) / 2 + 1, fs,
                     (Color){ 0, 0, 0, a });
            DrawText(buf, sx + (tw - lw) / 2,     sy + (tw - fs) / 2,     fs,
                     (Color){ 255, 240, 240, a });
        }
    }
    EndScissorMode();
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

/* Draw a mini flat terrain grid for legend entries. */
/* Draw one legend tile: hollow border + terrain icon. If t == filler, ghost only. */
static void draw_legend_tile(int tx, int ty, int cs, Terrain t, Terrain filler)
{
    int inset = cs / 9; if (inset < 1) inset = 1;
    int ti    = cs - 1 - inset * 2;
    int tcx   = tx + inset;
    int tcy   = ty + inset;

    if (t == filler) {
        /* ghost: dim outline only to show grid slot */
        DrawRectangleLinesEx(
            (Rectangle){ (float)tcx, (float)tcy, (float)ti, (float)ti },
            1.0f, (Color){ 50, 55, 70, 100 });
        return;
    }

    Color c  = TERRAIN_COLOR[t];
    float bw = (float)(ti * 9 / 100); if (bw < 1.5f) bw = 1.5f;
    DrawRectangleLinesEx(
        (Rectangle){ (float)tcx, (float)tcy, (float)ti, (float)ti },
        bw, c);
    int s = ti / 3; if (s >= 3)
        draw_terrain_icon(tcx + ti / 2, tcy + ti / 2, s, t, c);
}

static void draw_legend_grid(int x, int y, int cs,
                              const Terrain *cells, int cols, int rows,
                              Terrain filler)
{
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            draw_legend_tile(x + c * cs, y + r * cs, cs,
                             cells[r * cols + c], filler);
}

/* Draw a small padlock centred in rect r. closed = shackle down (locked). */
static void draw_lock_icon(Rectangle r, bool closed, Color c)
{
    int   bw = (int)(r.width * 0.46f);          /* body width  */
    int   bh = (int)(r.height * 0.34f);         /* body height */
    int   bx = (int)(r.x + (r.width  - bw) / 2);
    int   by = (int)(r.y + r.height * 0.50f);
    /* lock body */
    DrawRectangleRounded((Rectangle){ (float)bx, (float)by, (float)bw, (float)bh },
                         0.25f, 4, c);
    /* keyhole */
    DrawCircle(bx + bw / 2, by + bh / 2, (float)bh * 0.16f, (Color){ 10, 10, 14, 220 });
    /* shackle: outlined arch above the body */
    int   sw2 = (int)(bw * 0.62f);
    int   sx  = bx + (bw - sw2) / 2;
    int   sh2 = (int)(r.height * (closed ? 0.26f : 0.32f));
    int   sy  = by - sh2 + (closed ? 2 : -1);
    float th  = (float)(bh * 0.20f); if (th < 2.0f) th = 2.0f;
    DrawRectangleLinesEx((Rectangle){ (float)sx, (float)sy, (float)sw2, (float)(sh2 + bh) },
                         th, c);
}

static void draw_nav_buttons(void)
{
    static const char *labels[4] = { "<", "^", "v", ">" };
    bool can_enable[4];
    can_enable[0] = (s_vp_col > 0);
    can_enable[1] = (s_vp_row > 0);
    can_enable[2] = (s_vp_row + VIEW_ROWS < MAP_ROWS);
    can_enable[3] = (s_vp_col + VIEW_COLS < MAP_COLS);

    Vector2 mp        = GetMousePosition();
    bool    mouse_dn  = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    int     touch_n   = GetTouchPointCount();
    Vector2 tp        = (touch_n > 0) ? GetTouchPosition(0) : (Vector2){ -1.0f, -1.0f };

    for (int i = 0; i < 4; i++) {
        Rectangle r = nav_btn_rect(i);
        /* When the view is locked the pan arrows are inert. */
        bool enabled = can_enable[i] && !s_view_locked;

        bool hover = CheckCollisionPointRec(mp, r);
        bool press = enabled && ((mouse_dn && hover) ||
                     (touch_n > 0 && CheckCollisionPointRec(tp, r)));

        Color fill = enabled
                       ? (press ? (Color){ 60, 30, 70, 250 }
                                : (Color){ 20, 20, 28, 240 })
                       : (Color){ 12, 12, 16, 200 };
        Color edge = enabled
                       ? (Color){ 220, 80, 255, 220 }
                       : (Color){ 80, 60, 100, 120 };
        Color text = enabled
                       ? (Color){ 240, 240, 250, 250 }
                       : (Color){ 110, 110, 130, 180 };

        DrawRectangleRounded(r, 0.28f, 6, fill);
        DrawRectangleLinesEx(r, 2.0f, edge);

        int fs = (int)(r.height * 0.55f);
        int lw = MeasureText(labels[i], fs);
        DrawText(labels[i],
                 (int)(r.x + (r.width  - lw) / 2),
                 (int)(r.y + (r.height - fs) / 2),
                 fs, text);
    }

    /* Lock toggle (index 4). Highlighted when engaged. */
    {
        Rectangle r = nav_btn_rect(NAV_BTN_LOCK);
        Color fill = s_view_locked ? (Color){ 30, 70, 50, 250 }
                                   : (Color){ 20, 20, 28, 240 };
        Color edge = s_view_locked ? (Color){ 90, 240, 150, 240 }
                                   : (Color){ 120, 130, 150, 200 };
        Color icon = s_view_locked ? (Color){ 120, 245, 170, 255 }
                                   : (Color){ 200, 205, 215, 230 };
        DrawRectangleRounded(r, 0.28f, 6, fill);
        DrawRectangleLinesEx(r, 2.0f, edge);
        draw_lock_icon(r, s_view_locked, icon);
    }
}

static void draw_legend(void)
{
    int sw = GetScreenWidth();

    /* --- always-visible tab --- */
    Rectangle tab    = legend_tab_rect();
    int       tab_fs = (int)(tab.height * 0.42f);
    DrawRectangleRec(tab, (Color){ 0, 0, 0, 245 });
    DrawRectangleLinesEx(tab, 1.5f, (Color){ 70, 210, 255, 200 });

    const char *label = "Tile Guide";
    int tlw = MeasureText(label, tab_fs);
    DrawText(label, (sw - tlw) / 2,
             (int)(tab.y + (tab.height - tab_fs) / 2),
             tab_fs, (Color){ 70, 210, 255, 255 });

    const char *arrow = s_legend_open ? "v" : "^";
    int aw = MeasureText(arrow, tab_fs);
    DrawText(arrow, sw - aw - 12,
             (int)(tab.y + (tab.height - tab_fs) / 2),
             tab_fs, (Color){ 160, 160, 160, 200 });

    if (s_legend_t < 0.05f) return;

    /* --- sliding body --- */
    Rectangle dr  = legend_body_rect();
    int       pad = (int)(dr.width * 0.04f);
    DrawRectangleRec(dr, (Color){ 5, 5, 8, 248 });
    DrawRectangleLinesEx(dr, 1.5f, (Color){ 70, 210, 255, 160 });

    if (s_legend_t < 0.25f) return;

    /* ---- diagram cell data ---- */
    static const Terrain s_forest_cells[4] = {
        TERRAIN_FOREST, TERRAIN_FOREST,
        TERRAIN_FOREST, TERRAIN_FOREST,
    };
    static const Terrain s_farm_cells[4] = {
        TERRAIN_PLAINS, TERRAIN_PLAINS,
        TERRAIN_PLAINS, TERRAIN_PLAINS,
    };
    static const Terrain s_castle_cells[9] = {
        TERRAIN_WATER,    TERRAIN_WATER,    TERRAIN_WATER,
        TERRAIN_WATER,    TERRAIN_MOUNTAIN, TERRAIN_WATER,
        TERRAIN_WATER,    TERRAIN_WATER,    TERRAIN_WATER,
    };
    static const Terrain s_lumber_cells[9] = {
        TERRAIN_FOREST, TERRAIN_FOREST, TERRAIN_FOREST,
        TERRAIN_FOREST, TERRAIN_CITY,   TERRAIN_FOREST,
        TERRAIN_FOREST, TERRAIN_FOREST, TERRAIN_FOREST,
    };
    static const Terrain s_river_cells[4] = {
        TERRAIN_WATER, TERRAIN_WATER, TERRAIN_WATER, TERRAIN_WATER,
    };
    static const Terrain s_wheat_cells[3] = {
        TERRAIN_PLAINS, TERRAIN_PLAINS, TERRAIN_PLAINS,
    };
    static const Terrain s_road_cells[3] = {
        TERRAIN_CITY, TERRAIN_CITY, TERRAIN_CITY,
    };
    static const Terrain s_quarry_cells[3] = {
        TERRAIN_MOUNTAIN, TERRAIN_MOUNTAIN, TERRAIN_MOUNTAIN,
    };
    /* Forest Corner — 2×2 grid showing L (top-right is blank, use Plains as filler) */
    static const Terrain s_fc_cells[4] = {
        TERRAIN_FOREST, TERRAIN_PLAINS,
        TERRAIN_FOREST, TERRAIN_FOREST,
    };
    /* River Bend — 2×3 grid showing L (right column bottom 2 are blank) */
    static const Terrain s_rb_cells[6] = {
        TERRAIN_WATER,  TERRAIN_PLAINS,
        TERRAIN_WATER,  TERRAIN_PLAINS,
        TERRAIN_WATER,  TERRAIN_WATER,
    };
    /* Crossroads — 3×3 grid showing + */
    static const Terrain s_xr_cells[9] = {
        TERRAIN_PLAINS, TERRAIN_CITY,   TERRAIN_PLAINS,
        TERRAIN_CITY,   TERRAIN_CITY,   TERRAIN_CITY,
        TERRAIN_PLAINS, TERRAIN_CITY,   TERRAIN_PLAINS,
    };

    /* ---- per-row metadata ---- */
    /* g_cols, g_rows, cells ptr, name, desc, pts, per_turn */
    static const char *s_names[12] = {
        "Dense Forest", "Farm", "Castle", "Lumber Camp",
        "River", "Wheat Field", "Road", "Quarry",
        "Forest Corner", "River Bend", "Crossroads", "Trail Step"
    };
    static const char *s_descs[12] = {
        "2x2 Forest", "2x2 Plains", "Mountain+Water ring", "City+Forest ring",
        "4 Water in a line", "3 Plains in a line",
        "3 City in a line", "3 Mountain in a line",
        "L-shape 3x Forest", "L-shape 4x Water", "+ shape 5x City",
        "Each step on trail"
    };
    static const int s_pts[12] = {
        PT_DENSE_FOREST, PT_FARM, PT_CASTLE, PT_LUMBER_CAMP,
        PT_RIVER, PT_WHEAT_FIELD, PT_ROAD, PT_QUARRY,
        PT_FOREST_CORNER, PT_RIVER_BEND, PT_CROSSROADS, PT_TRAIL_STEP
    };
    /* g_cols and g_rows per entry */
    static const int s_gc[12] = { 2, 2, 3, 3, 4, 3, 3, 3, 2, 2, 3, 1 };
    static const int s_gr[12] = { 2, 2, 3, 3, 1, 1, 1, 1, 2, 3, 3, 1 };
    /* filler terrain used as empty-slot marker in diagrams (TERRAIN_COUNT = none) */
    static const Terrain s_filler[12] = {
        TERRAIN_COUNT, TERRAIN_COUNT, TERRAIN_COUNT, TERRAIN_COUNT,
        TERRAIN_COUNT, TERRAIN_COUNT, TERRAIN_COUNT, TERRAIN_COUNT,
        TERRAIN_PLAINS, TERRAIN_PLAINS, TERRAIN_PLAINS,
        TERRAIN_COUNT
    };
    const Terrain *s_cells[12] = {
        s_forest_cells, s_farm_cells, s_castle_cells, s_lumber_cells,
        s_river_cells, s_wheat_cells, s_road_cells, s_quarry_cells,
        s_fc_cells, s_rb_cells, s_xr_cells, NULL
    };

    int n_rows    = 12;
    int row_h     = (int)(dr.height / (float)n_rows);
    int diagram_w = row_h;

    for (int i = 0; i < n_rows; i++) {
        int ry = (int)dr.y + i * row_h;

        if (i > 0)
            DrawLine((int)(dr.x + pad), ry,
                     (int)(dr.x + dr.width - pad), ry,
                     (Color){ 70, 210, 255, 60 });

        /* mini diagram */
        if (s_cells[i] != NULL) {
            int gc = s_gc[i], gr = s_gr[i];
            int max_dim = gc > gr ? gc : gr;
            int cs = diagram_w / (max_dim + 1);
            if (cs < 4) cs = 4;
            int gx = (int)(dr.x + pad) + (diagram_w - gc * cs) / 2;
            int gy = ry + (row_h - gr * cs) / 2;
            draw_legend_grid(gx, gy, cs, s_cells[i], gc, gr, s_filler[i]);
        } else {
            /* trail: mini hero tile */
            int cs = diagram_w * 2 / 3;
            int gx = (int)(dr.x + pad) + (diagram_w - cs) / 2;
            int gy = ry + (row_h - cs) / 2;
            /* outer glow */
            DrawRectangleRounded(
                (Rectangle){ (float)(gx - 2), (float)(gy - 2),
                             (float)(cs + 4), (float)(cs + 4) },
                0.30f, 4, (Color){ 220, 100, 255, 50 });
            /* white hero tile */
            DrawRectangleRounded(
                (Rectangle){ (float)gx, (float)gy, (float)cs, (float)cs },
                0.22f, 4, (Color){ 255, 255, 255, 255 });
            DrawRectangleLinesEx(
                (Rectangle){ (float)gx, (float)gy, (float)cs, (float)cs },
                2.0f, (Color){ 220, 80, 255, 255 });
            int hfs = cs * 50 / 100;
            if (hfs < 6) hfs = 6;
            int hw = MeasureText("@", hfs);
            DrawText("@", gx + (cs - hw) / 2, gy + (cs - hfs) / 2,
                     hfs, (Color){ 40, 0, 60, 200 });
        }

        /* name + description */
        int text_x  = (int)(dr.x + pad + diagram_w + pad);
        int name_fs = row_h * 30 / 100;
        int desc_fs = row_h * 22 / 100;
        if (name_fs < 10) name_fs = 10;
        if (desc_fs <  8) desc_fs =  8;

        DrawText(s_names[i], text_x,
                 ry + (row_h / 2 - name_fs) / 2,
                 name_fs, (Color){ 255, 255, 255, 245 });
        DrawText(s_descs[i], text_x,
                 ry + row_h / 2,
                 desc_fs, (Color){ 150, 200, 220, 200 });

        /* pts label right-aligned */
        char pts_buf[24];
        snprintf(pts_buf, sizeof(pts_buf),
                 (i < 11) ? "+%d/turn" : "+%d/step", s_pts[i]);
        int   pts_fs = name_fs;
        int   pts_w  = MeasureText(pts_buf, pts_fs);
        Color pts_c  = (s_pts[i] >= PT_CASTLE)      ? (Color){ 255, 215,  60, 255 }
                     : (s_pts[i] >= PT_LUMBER_CAMP)  ? (Color){ 160, 240, 140, 255 }
                     : (s_pts[i] >= PT_DENSE_FOREST) ? (Color){ 100, 220, 130, 255 }
                     :                                  (Color){ 180, 200, 220, 230 };
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

    s_player_won  = false;
    s_player_lost = false;
    s_unit_count  = 0;
    s_active_player_idx = -1;
    s_inspect_idx       = -1;
    s_wave_index  = 1;

    /* Compact spawn: 3 player units on the left, 2 villains on the right. */
    int cx = MAP_COLS / 2;
    int cy = MAP_ROWS / 2;

    int keep_c[5] = { cx - 2, cx - 2, cx - 2, cx + 2, cx + 2 };
    int keep_r[5] = { cy - 1, cy,     cy + 1, cy,     cy + 1 };
    generate_obstacles(keep_c, keep_r, 5);

    spawn_unit(UNIT_BRUTE,            cx - 2, cy - 1);
    spawn_unit(UNIT_ROGUE,            cx - 2, cy    );
    spawn_unit(UNIT_MAGE,             cx - 2, cy + 1);
    spawn_unit(UNIT_VILLAIN_STALKER,  cx + 2, cy    );
    spawn_unit(UNIT_VILLAIN_SWARMER,  cx + 2, cy + 1);

    /* Legacy hero state seeded from the first player. */
    s_hero_col = s_units[0].col;
    s_hero_row = s_units[0].row;

    for (int i = 0; i < MAX_UNITS; i++) s_villain_tgt_n[i] = 0;
    s_rogue_path_n = 0;

    s_last_attack_count = 0;
    s_resolve_flash = 0.0f;

    int vc = vis_cols();
    int vr = vis_rows();
    /* Centre the viewport on the cluster centre. */
    s_vp_col = cx - vc / 2;
    s_vp_row = cy - vr / 2;
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
    memset(s_visited_cell,  0, sizeof(s_visited_cell));

    s_flash_count   = 0;
    s_move_cd       = 0.0f;
    s_hud_open      = false;
    s_hud_t         = 0.0f;
    s_legend_open   = false;
    s_legend_t      = 0.0f;
    s_view_locked   = false;
    s_prev_tc       = 0;
    s_prev_touch    = (Vector2){ -1.0f, -1.0f };

    /* Round 0: villains plan their move + telegraph their attack list.
     * Player can then drag; on release we resolve. */
    enemy_plan_turn();
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

        /* Per-unit visual lerp + bump decay. */
        for (int ui = 0; ui < s_unit_count; ui++) {
            Unit *u = &s_units[ui];
            u->vis_col += ((float)u->col - u->vis_col) * k_hero;
            u->vis_row += ((float)u->row - u->vis_row) * k_hero;
            if (u->bump > 0.0f) {
                u->bump -= BUMP_DECAY * dt;
                if (u->bump < 0.0f) u->bump = 0.0f;
            }
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
    int gx = grid_x();
    int gy = grid_y();
    BeginScissorMode(gx, gy, VIEW_COLS * ts, VIEW_ROWS * ts);
    for (int _pi = 0; _pi < MAX_SCORE_POPUPS; _pi++) {
        ScorePopup *p = &s_popups[_pi];
        if (!p->active) continue;

        /* ease out: fast rise at start, slows near top */
        float t     = p->age / POPUP_LIFETIME;
        float alpha = (1.0f - t) * (1.0f - t);
        unsigned char a = (unsigned char)(alpha * 255.0f);

        float cx = (p->tile_col - s_vp_vis_col) * ts + gx;
        float cy = (p->tile_row - s_vp_vis_row) * ts + gy;

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
    EndScissorMode();
}

static void draw_end_game(void)
{
    if (!s_game_over) return;
    int sw = GetScreenWidth(), sh = GetScreenHeight();

    DrawRectangle(0, 0, sw, sh, (Color){ 0, 0, 0, 170 });

    const char *title   = "DEFEAT";
    Color       title_c = (Color){ 255,  80,  80, 255 };
    char        subtitle[64];
    snprintf(subtitle, sizeof(subtitle), "Cleared %d wave%s",
             s_wave_index - 1, (s_wave_index - 1) == 1 ? "" : "s");

    int tfs = sh / 9;
    int tw  = MeasureText(title, tfs);
    DrawText(title, (sw - tw) / 2, sh * 3 / 10, tfs, title_c);

    int sfs = sh / 18;
    int sw2 = MeasureText(subtitle, sfs);
    DrawText(subtitle, (sw - sw2) / 2, sh * 3 / 10 + tfs + sh / 20,
             sfs, (Color){ 220, 220, 220, 255 });

    const char *hint = "Press ESC to return to menu";
    int hfs = sh / 26;
    int hw  = MeasureText(hint, hfs);
    DrawText(hint, (sw - hw) / 2, sh * 7 / 10, hfs, (Color){ 140, 140, 150, 200 });
}

/* Find the first living unit of a given kind; returns -1 if none. */
static int first_living_of_kind(UnitKind k)
{
    for (int i = 0; i < s_unit_count; i++)
        if (s_units[i].alive && s_units[i].kind == k) return i;
    return -1;
}

static void draw_hud_status(void)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int fs = sh / 36;
    if (fs < 12) fs = 12;
    int pad = sw / 40;

    DrawRectangle(0, 0, sw, fs * 2, (Color){ 0, 0, 0, 220 });
    DrawLine(0, fs * 2, sw, fs * 2, (Color){ 80, 200, 255, 160 });

    int b_idx = first_living_of_kind(UNIT_BRUTE);
    int r_idx = first_living_of_kind(UNIT_ROGUE);
    int m_idx = first_living_of_kind(UNIT_MAGE);
    int villains = count_living(UNIT_VILLAIN_STALKER) + count_living(UNIT_VILLAIN_SWARMER);

    char left[96];
    int bhp = b_idx >= 0 ? s_units[b_idx].hp : 0;
    int rhp = r_idx >= 0 ? s_units[r_idx].hp : 0;
    int mhp = m_idx >= 0 ? s_units[m_idx].hp : 0;
    snprintf(left, sizeof(left), "B:%d  R:%d  M:%d", bhp, rhp, mhp);
    DrawText(left, pad, fs / 2, fs, (Color){ 220, 230, 240, 240 });

    char right[64];
    snprintf(right, sizeof(right), "V:%d   W:%d", villains, s_wave_index);
    int rw = MeasureText(right, fs);
    DrawText(right, sw - pad - rw, fs / 2, fs,
             villains ? (Color){ 240, 110, 110, 240 }
                      : (Color){ 100, 240, 130, 240 });
}

static void RealmWalkDraw(void)
{
    ClearBackground((Color){ 0, 0, 0, 255 });
    draw_map_tiles();
    draw_flying_tiles();
    draw_units();
    /* Threats and previews render on top so the targeted cell reads clearly
     * even when a unit is currently standing on it. */
    draw_player_previews();
    draw_threats();
    draw_resolve_flash();
    draw_nav_buttons();
    draw_hud_status();
    draw_timer_bar();
    draw_inspect_card();
    draw_end_game();
}

static void RealmWalkDeinit(void)
{
    /* all state is static; reset happens in Init */
}

const Prototype RealmWalkProto = {
    .name        = "Realm Walk",
    .description = "Brute, Rogue, Mage vs endless villain waves. Drag one player; everyone fights.",
    .Init        = RealmWalkInit,
    .Update      = RealmWalkUpdate,
    .Draw        = RealmWalkDraw,
    .Deinit      = RealmWalkDeinit,
};
