#include "drag_disp.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>

/* ---- constants ---- */
#define COLS        6
#define ROWS        5
#define NUM_TYPES   5
#define DRAG_SECS   3.0f
#define LERP_SPD    16.0f
#define FLASH_SECS  0.35f
#define CLEAR_SECS  0.25f
#define FALL_SECS   0.32f

/* ---- types ---- */
typedef enum { ORB_FIRE, ORB_WATER, ORB_WOOD, ORB_LIGHT, ORB_DARK } OrbType;

typedef struct {
    OrbType type;
    Vector2 vis;     /* current visual position — lerps to slot centre  */
    float   alpha;   /* 1.0 normally; fades to 0 during CLEAR           */
    bool    alive;   /* false = empty slot                               */
    bool    matched; /* flagged for clearing this cascade wave           */
} Orb;

typedef enum {
    PHASE_IDLE,
    PHASE_DRAGGING,
    PHASE_FLASH,   /* matched orbs highlighted before clearing          */
    PHASE_CLEAR,   /* matched orbs fade out                             */
    PHASE_FALL,    /* remaining orbs slide down                         */
    PHASE_REFILL,  /* new orbs drop in from above                       */
} Phase;

/* ---- module state ---- */
static Orb   s_grid[ROWS][COLS];
static Phase s_phase;
static float s_phase_t;

static bool    s_dragging;
static OrbType s_held_type;
static int     s_held_r, s_held_c;
static Vector2 s_held_vis;
static float   s_drag_timer;

static int s_score;
static int s_combo;
static int s_prev_tc;

/* ---- palette ---- */
static const Color ORB_COL[NUM_TYPES] = {
    { 255, 110,  80, 255 }, /* fire  – coral red   */
    {  80, 170, 255, 255 }, /* water – sky blue    */
    {  90, 200, 120, 255 }, /* wood  – mint green  */
    { 255, 215,  70, 255 }, /* light – warm gold   */
    { 170,  90, 230, 255 }, /* dark  – soft purple */
};

/* ---- layout ---- */
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

/* ---- input ---- */
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

/* ---- game logic ---- */
static int find_matches(void)
{
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            s_grid[r][c].matched = false;

    int count = 0;

    /* horizontal runs */
    for (int r = 0; r < ROWS; r++) {
        int c = 0;
        while (c < COLS) {
            if (!s_grid[r][c].alive) { c++; continue; }
            int run = 1;
            while (c + run < COLS &&
                   s_grid[r][c + run].alive &&
                   s_grid[r][c + run].type == s_grid[r][c].type)
                run++;
            if (run >= 3)
                for (int k = 0; k < run; k++) s_grid[r][c + k].matched = true;
            c += run;
        }
    }

    /* vertical runs */
    for (int c = 0; c < COLS; c++) {
        int r = 0;
        while (r < ROWS) {
            if (!s_grid[r][c].alive) { r++; continue; }
            int run = 1;
            while (r + run < ROWS &&
                   s_grid[r + run][c].alive &&
                   s_grid[r + run][c].type == s_grid[r][c].type)
                run++;
            if (run >= 3)
                for (int k = 0; k < run; k++) s_grid[r + k][c].matched = true;
            r += run;
        }
    }

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
                s_grid[write][c]       = s_grid[r][c];
                s_grid[r][c].alive     = false;
            }
            write--;
        }
        for (int r = write; r >= 0; r--)
            s_grid[r][c].alive = false;
    }
}

static void refill_columns(void)
{
    float cs = cell_sz();
    for (int c = 0; c < COLS; c++) {
        int empty = 0;
        for (int r = 0; r < ROWS; r++)
            if (!s_grid[r][c].alive) empty++;
        int drop = 0;
        for (int r = 0; r < ROWS; r++) {
            if (s_grid[r][c].alive) continue;
            s_grid[r][c] = (Orb){
                .type    = (OrbType)GetRandomValue(0, NUM_TYPES - 1),
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

static void release_orb(void)
{
    s_grid[s_held_r][s_held_c] = (Orb){
        .type  = s_held_type,
        .alive = true,
        .alpha = 1.0f,
        .vis   = s_held_vis,
    };
    s_dragging   = false;
    s_drag_timer = 0.0f;

    s_combo = 0;
    int matched = find_matches();
    if (matched > 0) {
        s_score  += matched; /* combo multiplier = 1 on first clear */
        s_phase   = PHASE_FLASH;
        s_phase_t = 0.0f;
    } else {
        s_phase = PHASE_IDLE;
    }
}

/* ---- prototype callbacks ---- */

static void DragDispInit(void)
{
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            OrbType t;
            int tries = 0;
            do {
                t = (OrbType)GetRandomValue(0, NUM_TYPES - 1);
                tries++;
            } while (tries < 20 &&
                     ((c >= 2 && s_grid[r][c-1].type == t && s_grid[r][c-2].type == t) ||
                      (r >= 2 && s_grid[r-1][c].type == t && s_grid[r-2][c].type == t)));
            s_grid[r][c] = (Orb){ .type=t, .vis=cell_center(r,c), .alpha=1.0f, .alive=true };
        }
    }
    s_phase      = PHASE_IDLE;
    s_phase_t    = 0.0f;
    s_dragging   = false;
    s_score      = 0;
    s_combo      = 0;
    s_prev_tc    = 0;
    s_drag_timer = 0.0f;
}

static void DragDispUpdate(float dt)
{
    int     tc   = GetTouchPointCount();
    Vector2 ipos = input_pos();

    /* ---- press: pick up an orb ---- */
    if (s_phase == PHASE_IDLE && input_just_pressed()) {
        int r, c;
        cell_at(ipos, &r, &c);
        if (r >= 0 && s_grid[r][c].alive) {
            s_held_type         = s_grid[r][c].type;
            s_held_r            = r;
            s_held_c            = c;
            s_held_vis          = s_grid[r][c].vis;
            s_grid[r][c].alive  = false;
            s_dragging          = true;
            s_drag_timer        = DRAG_SECS;
            s_phase             = PHASE_DRAGGING;
            s_phase_t           = 0.0f;
        }
    }

    /* ---- drag: move held orb, displace others ---- */
    if (s_phase == PHASE_DRAGGING && s_dragging) {
        /* held orb snaps to finger */
        float k = fminf(LERP_SPD * 2.5f * dt, 1.0f);
        s_held_vis.x += (ipos.x - s_held_vis.x) * k;
        s_held_vis.y += (ipos.y - s_held_vis.y) * k;

        /* cell crossing → displace */
        int nr, nc;
        cell_at(ipos, &nr, &nc);
        if (nr >= 0 && (nr != s_held_r || nc != s_held_c)) {
            if (s_grid[nr][nc].alive) {
                s_grid[s_held_r][s_held_c] = s_grid[nr][nc];
                s_grid[nr][nc].alive       = false;
            }
            s_held_r = nr;
            s_held_c = nc;
        }

        s_drag_timer -= dt;
        if (s_drag_timer <= 0.0f || input_just_released())
            release_orb();
    }

    s_prev_tc = tc;

    /* ---- phase timers ---- */

    if (s_phase == PHASE_FLASH) {
        s_phase_t += dt;
        if (s_phase_t >= FLASH_SECS) {
            s_phase   = PHASE_CLEAR;
            s_phase_t = 0.0f;
        }
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
            s_phase   = PHASE_FALL;
            s_phase_t = 0.0f;
        }
    }

    if (s_phase == PHASE_FALL) {
        s_phase_t += dt;
        if (s_phase_t >= FALL_SECS) {
            refill_columns();
            s_phase   = PHASE_REFILL;
            s_phase_t = 0.0f;
        }
    }

    if (s_phase == PHASE_REFILL) {
        s_phase_t += dt;
        if (s_phase_t >= FALL_SECS) {
            int matched = find_matches();
            if (matched > 0) {
                s_combo++;
                s_score  += matched * (s_combo + 1);
                s_phase   = PHASE_FLASH;
                s_phase_t = 0.0f;
            } else {
                s_phase = PHASE_IDLE;
            }
        }
    }

    /* ---- lerp all orb visual positions toward slot centre ---- */
    float k = fminf(LERP_SPD * dt, 1.0f);
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (!s_grid[r][c].alive) continue;
            Vector2 tgt = cell_center(r, c);
            s_grid[r][c].vis.x += (tgt.x - s_grid[r][c].vis.x) * k;
            s_grid[r][c].vis.y += (tgt.y - s_grid[r][c].vis.y) * k;
        }
    }
}

static void DragDispDraw(void)
{
    ClearBackground((Color){ 245, 240, 230, 255 });

    float cs   = cell_sz();
    float orb_r = cs * 0.42f;
    float gx   = grid_ox();
    float gy   = grid_oy();
    int   sw   = GetScreenWidth();
    int   sh   = GetScreenHeight();

    /* ---- grid background cells ---- */
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            Rectangle cr = { gx + c * cs + 3, gy + r * cs + 3, cs - 6, cs - 6 };
            DrawRectangleRounded(cr, 0.20f, 6, (Color){ 228, 222, 210, 255 });
        }
    }

    /* ---- orbs ---- */
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            Orb *o = &s_grid[r][c];
            if (!o->alive) continue;

            Color col = Fade(ORB_COL[o->type], o->alpha);

            /* flash ring on matched orbs */
            if (o->matched && s_phase == PHASE_FLASH)
                DrawCircleV(o->vis, orb_r + 5.0f, Fade(WHITE, 0.75f));

            DrawCircleV(o->vis, orb_r, col);

            /* shine dot */
            DrawCircleV((Vector2){ o->vis.x - orb_r * 0.28f,
                                   o->vis.y - orb_r * 0.28f },
                        orb_r * 0.28f, Fade(WHITE, 0.28f * o->alpha));
        }
    }

    /* ---- held orb ---- */
    if (s_dragging) {
        float r_big = orb_r * 1.22f;

        /* drop shadow */
        DrawCircleV((Vector2){ s_held_vis.x + 5, s_held_vis.y + 7 },
                    r_big, (Color){ 0, 0, 0, 55 });

        DrawCircleV(s_held_vis, r_big, ORB_COL[s_held_type]);
        DrawCircleV((Vector2){ s_held_vis.x - r_big * 0.28f,
                               s_held_vis.y - r_big * 0.28f },
                    r_big * 0.28f, Fade(WHITE, 0.35f));

        /* timer arc (green → red as time runs out) */
        float frac = s_drag_timer / DRAG_SECS;
        Color tc_col = frac > 0.4f ? (Color){ 80, 200,  80, 220 }
                                   : (Color){ 220, 60,  60, 220 };
        if (frac > 0.001f)
            DrawRing(s_held_vis, r_big + 6.0f, r_big + 11.0f,
                     -90.0f, -90.0f + frac * 360.0f, 32, tc_col);
    }

    /* ---- HUD ---- */
    int hud_sz = sw / 18;
    char score_buf[32];
    snprintf(score_buf, sizeof(score_buf), "Score: %d", s_score);
    DrawText(score_buf, sw / 20, sh / 22, hud_sz, (Color){ 80, 70, 60, 255 });

    /* combo badge */
    if (s_combo > 0 &&
        (s_phase == PHASE_FLASH || s_phase == PHASE_CLEAR ||
         s_phase == PHASE_FALL  || s_phase == PHASE_REFILL)) {
        char cb[16];
        snprintf(cb, sizeof(cb), "%d Combo!", s_combo + 1);
        int cbw = MeasureText(cb, hud_sz);
        DrawText(cb, (sw - cbw) / 2,
                 (int)(gy - hud_sz * 2.2f), hud_sz,
                 (Color){ 220, 110, 40, 255 });
    }

    /* ---- hint (first launch only) ---- */
    if (s_phase == PHASE_IDLE && s_score == 0) {
        int hsz = sw / 26;
        const char *hint = "Drag an orb to displace others  –  match 3 or more";
        int hw = MeasureText(hint, hsz);
        DrawText(hint, (sw - hw) / 2,
                 (int)(gy + ROWS * cs + (float)hsz * 0.8f),
                 hsz, (Color){ 160, 150, 135, 255 });
    }
}

static void DragDispDeinit(void) { /* nothing to free */ }

const Prototype DragDispProto = {
    .name        = "Drag & Displace",
    .description = "P&D-style match-3: drag one orb to shuffle the grid, match 3+ to score.",
    .Init        = DragDispInit,
    .Update      = DragDispUpdate,
    .Draw        = DragDispDraw,
    .Deinit      = DragDispDeinit,
};
