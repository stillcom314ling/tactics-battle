#include "triad_drag.h"
#include "raylib.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/* ----------------------------------------------------------------- constants */

#define BOARD_W        5
#define BOARD_H        5
#define BOARD_SIZE     25
#define FLIP_THRESHOLD 3
#define DRAG_SECS      3.0f
#define LERP_SPD       14.0f
#define FLIP_ANIM      0.20f
#define SPAWN_ANIM     0.22f

/* Direction order: N=0, E=1, S=2, W=3.  Opposite = (d+2)%4. */
static const int DR[4] = {-1,  0, 1, 0};
static const int DC[4] = { 0,  1, 0, -1};

/* ------------------------------------------------------------------- typedefs */

typedef struct {
    int  values[4]; /* N=0, E=1, S=2, W=3 (1–9) */
    int  flip_count;
    bool active;
} Card;

typedef enum {
    PHASE_IDLE,
    PHASE_DRAGGING,
    PHASE_RESOLVING, /* waiting for flip animations to finish */
    PHASE_SPAWN,     /* waiting for spawn animations to finish */
} Phase;

/* ---------------------------------------------------------------- module state */

static Card    s_board[BOARD_SIZE];
static Vector2 s_vis[BOARD_SIZE];   /* visual position — lerps to cell centre */
static Phase   s_phase;
static int     s_prev_tc;
static int     s_score;

/* drag */
static bool    s_dragging;
static Card    s_drag_card;         /* held card copy */
static int     s_drag_idx;          /* current logical cell of held card */
static Vector2 s_drag_vis;          /* visual position follows finger */
static float   s_drag_timer;

/* per-card animation timers */
static float   s_flip_t[BOARD_SIZE];  /* counts FLIP_ANIM → 0 */
static float   s_spawn_t[BOARD_SIZE]; /* counts SPAWN_ANIM → 0 */

/* -------------------------------------------------------------------- layout */

static float cell_sz(void)  { return (float)GetScreenWidth() / 6.0f; }
static float grid_ox(void)  { return ((float)GetScreenWidth() - BOARD_W * cell_sz()) * 0.5f; }
static float grid_oy(void)  { return (float)GetScreenHeight() * 0.16f; }

static Vector2 cell_centre(int idx)
{
    float cs = cell_sz();
    int r = idx / BOARD_W, c = idx % BOARD_W;
    return (Vector2){ grid_ox() + c * cs + cs * 0.5f,
                      grid_oy() + r * cs + cs * 0.5f };
}

static int cell_at_pos(Vector2 p)
{
    float cs = cell_sz();
    int c = (int)((p.x - grid_ox()) / cs);
    int r = (int)((p.y - grid_oy()) / cs);
    if (r < 0 || r >= BOARD_H || c < 0 || c >= BOARD_W) return -1;
    return r * BOARD_W + c;
}

/* --------------------------------------------------------------------- input */

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

/* -------------------------------------------------------------------- helpers */

static Card make_card(void)
{
    Card c;
    c.flip_count = 0;
    c.active     = true;
    for (int i = 0; i < 4; i++)
        c.values[i] = GetRandomValue(1, 9);
    return c;
}

static Color card_color(int flip_count)
{
    switch (flip_count) {
        case 0:  return (Color){  80, 140, 210, 255 }; /* cool blue      */
        case 1:  return (Color){ 230, 170,  50, 255 }; /* warm orange    */
        case 2:  return (Color){ 220,  90,  40, 255 }; /* hot red-orange */
        default: return (Color){ 190,  45,  30, 255 }; /* deep red       */
    }
}

/* ----------------------------------------------------------- flip resolution */

static void run_resolve(int drop_idx)
{
    int  queue[BOARD_SIZE];
    int  depth_arr[BOARD_SIZE];
    bool visited[BOARD_SIZE];
    memset(visited,   0, sizeof(visited));
    memset(depth_arr, 0, sizeof(depth_arr));

    int head = 0, tail = 0;
    queue[tail++]     = drop_idx;
    visited[drop_idx] = true;

    while (head < tail) {
        int cur = queue[head++];
        int d   = depth_arr[cur];
        int r   = cur / BOARD_W;
        int c   = cur % BOARD_W;

        for (int dir = 0; dir < 4; dir++) {
            int nr = r + DR[dir];
            int nc = c + DC[dir];
            if (nr < 0 || nr >= BOARD_H || nc < 0 || nc >= BOARD_W) continue;
            int nb  = nr * BOARD_W + nc;
            int opp = (dir + 2) % 4;
            if (!s_board[nb].active || visited[nb]) continue;
            if (s_board[cur].values[dir] >= s_board[nb].values[opp]) {
                s_board[nb].flip_count++;
                s_score      += 5 * (d + 1);
                s_flip_t[nb]  = FLIP_ANIM;
                visited[nb]   = true;
                depth_arr[nb] = d + 1;
                queue[tail++] = nb;
            }
        }
    }

    /* Extra pass: cards that reached the vanish threshold fire one final
       activation against any neighbour not yet touched by the chain. */
    for (int i = 0; i < BOARD_SIZE; i++) {
        if (!s_board[i].active) continue;
        if (s_board[i].flip_count < FLIP_THRESHOLD) continue;
        int r = i / BOARD_W, c = i % BOARD_W;
        for (int dir = 0; dir < 4; dir++) {
            int nr = r + DR[dir];
            int nc = c + DC[dir];
            if (nr < 0 || nr >= BOARD_H || nc < 0 || nc >= BOARD_W) continue;
            int nb  = nr * BOARD_W + nc;
            int opp = (dir + 2) % 4;
            if (!s_board[nb].active || visited[nb]) continue;
            if (s_board[i].values[dir] >= s_board[nb].values[opp]) {
                s_board[nb].flip_count++;
                s_score     += 5;
                s_flip_t[nb] = FLIP_ANIM;
                visited[nb]  = true;
            }
        }
    }
}

static void apply_vanishes(void)
{
    bool any = false;
    for (int i = 0; i < BOARD_SIZE; i++) {
        if (!s_board[i].active) continue;
        if (s_board[i].flip_count < FLIP_THRESHOLD) continue;
        s_board[i]   = make_card();
        s_vis[i]     = cell_centre(i);
        s_spawn_t[i] = SPAWN_ANIM;
        s_score     += 25;
        any          = true;
    }
    s_phase = any ? PHASE_SPAWN : PHASE_IDLE;
}

/* ---------------------------------------------------------- prototype: Init */

static void TriadDragInit(void)
{
    for (int i = 0; i < BOARD_SIZE; i++) {
        s_board[i]   = make_card();
        s_vis[i]     = cell_centre(i);
        s_flip_t[i]  = 0.0f;
        s_spawn_t[i] = 0.0f;
    }
    s_phase      = PHASE_IDLE;
    s_dragging   = false;
    s_drag_timer = 0.0f;
    s_score      = 0;
    s_prev_tc    = 0;
}

/* ---------------------------------------------------------- prototype: Update */

static void TriadDragUpdate(float dt)
{
    int     tc   = GetTouchPointCount();
    Vector2 ipos = input_pos();

    /* pick up */
    if (s_phase == PHASE_IDLE && input_just_pressed()) {
        int idx = cell_at_pos(ipos);
        if (idx >= 0 && s_board[idx].active) {
            s_drag_card         = s_board[idx];
            s_drag_idx          = idx;
            s_drag_vis          = s_vis[idx];
            s_board[idx].active = false;
            s_dragging          = true;
            s_drag_timer        = DRAG_SECS;
            s_phase             = PHASE_DRAGGING;
        }
    }

    /* drag: card follows finger; cross into a new cell → swap */
    if (s_phase == PHASE_DRAGGING) {
        float k = fminf(LERP_SPD * 2.5f * dt, 1.0f);
        s_drag_vis.x += (ipos.x - s_drag_vis.x) * k;
        s_drag_vis.y += (ipos.y - s_drag_vis.y) * k;

        int next = cell_at_pos(ipos);
        if (next >= 0 && next != s_drag_idx) {
            if (s_board[next].active) {
                /* displaced card slides back to fill the gap */
                s_board[s_drag_idx] = s_board[next];
                s_vis[s_drag_idx]   = s_vis[next];
            }
            s_board[next].active = false;
            s_drag_idx           = next;
        }

        s_drag_timer -= dt;
        if (s_drag_timer <= 0.0f || input_just_released()) {
            /* drop */
            s_board[s_drag_idx]        = s_drag_card;
            s_board[s_drag_idx].active = true;
            s_vis[s_drag_idx]          = s_drag_vis; /* snaps from finger to grid */
            s_dragging                 = false;
            run_resolve(s_drag_idx);
            s_phase = PHASE_RESOLVING;
        }
    }

    /* resolving: wait for all flip animations then check vanishes */
    if (s_phase == PHASE_RESOLVING) {
        bool done = true;
        for (int i = 0; i < BOARD_SIZE; i++)
            if (s_flip_t[i] > 0.0f) { done = false; break; }
        if (done) apply_vanishes();
    }

    /* spawn: wait for all spawn animations */
    if (s_phase == PHASE_SPAWN) {
        bool done = true;
        for (int i = 0; i < BOARD_SIZE; i++)
            if (s_spawn_t[i] > 0.0f) { done = false; break; }
        if (done) s_phase = PHASE_IDLE;
    }

    /* tick animation timers */
    for (int i = 0; i < BOARD_SIZE; i++) {
        if (s_flip_t[i]  > 0.0f) { s_flip_t[i]  -= dt; if (s_flip_t[i]  < 0.0f) s_flip_t[i]  = 0.0f; }
        if (s_spawn_t[i] > 0.0f) { s_spawn_t[i] -= dt; if (s_spawn_t[i] < 0.0f) s_spawn_t[i] = 0.0f; }
    }

    /* lerp card visuals toward their cell centres */
    {
        float k = fminf(LERP_SPD * dt, 1.0f);
        for (int i = 0; i < BOARD_SIZE; i++) {
            if (!s_board[i].active) continue;
            Vector2 tgt = cell_centre(i);
            s_vis[i].x += (tgt.x - s_vis[i].x) * k;
            s_vis[i].y += (tgt.y - s_vis[i].y) * k;
        }
    }

    s_prev_tc = tc;
}

/* ---------------------------------------------------------- card rendering */

static void draw_card(Vector2 centre, const Card *card, float scale_x, float scale_y, float alpha)
{
    float cs   = cell_sz();
    float full = cs - 8.0f;
    float w    = full * scale_x;
    float h    = full * scale_y;
    if (w < 1.0f || h < 1.0f) return;

    Rectangle rect = { centre.x - w * 0.5f, centre.y - h * 0.5f, w, h };
    DrawRectangleRounded(rect, 0.15f, 8, Fade(card_color(card->flip_count), alpha));

    if (w < full * 0.35f || alpha < 0.2f) return;

    int  fs = (int)(full * 0.22f);
    Color tc = Fade(WHITE, alpha);
    char buf[4];
    int  tw;

    /* N — top-centre */
    snprintf(buf, sizeof(buf), "%d", card->values[0]);
    tw = MeasureText(buf, fs);
    DrawText(buf, (int)(rect.x + w * 0.5f - tw * 0.5f),
             (int)(rect.y + 4), fs, tc);

    /* E — right-centre */
    snprintf(buf, sizeof(buf), "%d", card->values[1]);
    tw = MeasureText(buf, fs);
    DrawText(buf, (int)(rect.x + w - tw - 4),
             (int)(rect.y + h * 0.5f - fs * 0.5f), fs, tc);

    /* S — bottom-centre */
    snprintf(buf, sizeof(buf), "%d", card->values[2]);
    tw = MeasureText(buf, fs);
    DrawText(buf, (int)(rect.x + w * 0.5f - tw * 0.5f),
             (int)(rect.y + h - fs - 4), fs, tc);

    /* W — left-centre */
    snprintf(buf, sizeof(buf), "%d", card->values[3]);
    DrawText(buf, (int)(rect.x + 4),
             (int)(rect.y + h * 0.5f - fs * 0.5f), fs, tc);
}

/* ---------------------------------------------------------- prototype: Draw */

static void TriadDragDraw(void)
{
    ClearBackground((Color){ 28, 28, 38, 255 });

    float cs = cell_sz();
    float gx = grid_ox();
    float gy = grid_oy();
    int   sw = GetScreenWidth();
    int   sh = GetScreenHeight();

    /* grid cell backgrounds */
    for (int i = 0; i < BOARD_SIZE; i++) {
        int r = i / BOARD_W, c = i % BOARD_W;
        Rectangle bg = { gx + c * cs + 4, gy + r * cs + 4, cs - 8, cs - 8 };
        DrawRectangleRounded(bg, 0.15f, 6, (Color){ 50, 50, 68, 255 });
    }

    /* board cards */
    for (int i = 0; i < BOARD_SIZE; i++) {
        if (!s_board[i].active) continue;

        float sx = 1.0f, sy = 1.0f;

        /* flip squish: card collapses on X axis then re-expands */
        if (s_flip_t[i] > 0.0f) {
            float p = 1.0f - s_flip_t[i] / FLIP_ANIM;
            sx = fabsf(2.0f * p - 1.0f);
            if (sx < 0.05f) sx = 0.05f;
        }

        /* spawn scale-in: grows from 0 to full size */
        if (s_spawn_t[i] > 0.0f) {
            float p = 1.0f - s_spawn_t[i] / SPAWN_ANIM;
            if (p < 0.05f) continue;
            sx = p;
            sy = p;
        }

        draw_card(s_vis[i], &s_board[i], sx, sy, 1.0f);
    }

    /* held card */
    if (s_dragging) {
        float big  = 1.15f;
        float full = cs - 8.0f;
        float hw   = full * big * 0.5f;
        float hh   = full * big * 0.5f;

        /* drop shadow */
        Rectangle shadow = { s_drag_vis.x - hw + 6, s_drag_vis.y - hh + 8,
                             full * big, full * big };
        DrawRectangleRounded(shadow, 0.15f, 6, (Color){ 0, 0, 0, 60 });

        draw_card(s_drag_vis, &s_drag_card, big, big, 1.0f);

        /* drag timer ring: green → red as time runs out */
        float frac   = s_drag_timer / DRAG_SECS;
        float ring_r = hw + 6.0f;
        Color ring_c = frac > 0.4f ? (Color){ 80, 200,  80, 200 }
                                   : (Color){ 220,  60,  60, 200 };
        if (frac > 0.001f)
            DrawRing(s_drag_vis, ring_r, ring_r + 7.0f,
                     -90.0f, -90.0f + frac * 360.0f, 32, ring_c);
    }

    /* HUD: score */
    int  hud_sz = sw / 16;
    char score_buf[32];
    snprintf(score_buf, sizeof(score_buf), "Score  %d", s_score);
    int score_w = MeasureText(score_buf, hud_sz);
    DrawText(score_buf, (sw - score_w) / 2, sh / 30, hud_sz, WHITE);

    /* first-launch hint */
    if (s_phase == PHASE_IDLE && s_score == 0) {
        int  hsz  = sw / 30;
        const char *hint = "Drag a card — high sides flip neighbours";
        int  hw2  = MeasureText(hint, hsz);
        DrawText(hint, (sw - hw2) / 2,
                 (int)(gy + BOARD_H * cs + (float)hsz * 1.2f),
                 hsz, (Color){ 140, 140, 160, 200 });
    }
}

/* -------------------------------------------------------- prototype: Deinit */

static void TriadDragDeinit(void) { /* nothing to free */ }

/* --------------------------------------------------------------- registration */

const Prototype TriadDragProto = {
    .name        = "Triad Drag",
    .description = "Drag cards P&D-style — chain flips score points, 3 flips vanishes a card.",
    .Init        = TriadDragInit,
    .Update      = TriadDragUpdate,
    .Draw        = TriadDragDraw,
    .Deinit      = TriadDragDeinit,
};
