#include "dots_boxes.h"
#include "raylib.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define DOTS_R    6
#define DOTS_C    6
#define BOX_R     5
#define BOX_C     5
#define WIN_SCORE 15
#define MAX_EDGES 60

#define EDGE_EMPTY  0
#define EDGE_PLAYER 1
#define EDGE_SYSTEM 2

#define PHASE_PLAYING 0
#define PHASE_WIN     1
#define PHASE_LOSE    2

typedef struct { int is_h; int r; int c; } Edge;

static int    s_h[DOTS_R][BOX_C];
static int    s_v[BOX_R][DOTS_C];
static int    s_box[BOX_R][BOX_C];
static int    s_score;
static int    s_phase;
static bool   s_stabilizer;
static bool   s_stab_active;
static char   s_status[80];
static int    s_prev_tc;
static Vector2 s_tap_pos;
static bool   s_tapped;
static float  s_ox, s_oy, s_cs;

static void compute_layout(void)
{
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    s_cs = fminf(sw * 0.80f / BOX_C, sh * 0.60f / BOX_R);
    s_ox = (sw - s_cs * BOX_C) * 0.5f;
    s_oy = sh * 0.15f;
}

static Vector2 dot_pos(int r, int c)
{
    return (Vector2){ s_ox + c * s_cs, s_oy + r * s_cs };
}

static float pt_seg_dist(Vector2 p, Vector2 a, Vector2 b)
{
    float dx = b.x - a.x, dy = b.y - a.y;
    float len2 = dx*dx + dy*dy;
    if (len2 < 1e-4f) {
        float ex = p.x - a.x, ey = p.y - a.y;
        return sqrtf(ex*ex + ey*ey);
    }
    float t = ((p.x - a.x)*dx + (p.y - a.y)*dy) / len2;
    t = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f : t;
    float qx = a.x + t*dx - p.x;
    float qy = a.y + t*dy - p.y;
    return sqrtf(qx*qx + qy*qy);
}

static int check_new_boxes(int etype)
{
    int n = 0;
    for (int r = 0; r < BOX_R; r++)
        for (int c = 0; c < BOX_C; c++)
            if (!s_box[r][c] &&
                s_h[r][c] && s_h[r+1][c] && s_v[r][c] && s_v[r][c+1]) {
                s_box[r][c] = etype;
                n++;
            }
    return n;
}

static bool board_full(void)
{
    for (int r = 0; r < DOTS_R; r++)
        for (int c = 0; c < BOX_C; c++)
            if (!s_h[r][c]) return false;
    for (int r = 0; r < BOX_R; r++)
        for (int c = 0; c < DOTS_C; c++)
            if (!s_v[r][c]) return false;
    return true;
}

static void do_corruption(void)
{
    Edge list[MAX_EDGES];
    int  n = 0;
    for (int r = 0; r < DOTS_R; r++)
        for (int c = 0; c < BOX_C; c++)
            if (!s_h[r][c]) list[n++] = (Edge){1, r, c};
    for (int r = 0; r < BOX_R; r++)
        for (int c = 0; c < DOTS_C; c++)
            if (!s_v[r][c]) list[n++] = (Edge){0, r, c};
    if (!n) return;
    Edge e = list[GetRandomValue(0, n - 1)];
    if (e.is_h) s_h[e.r][e.c] = EDGE_SYSTEM;
    else        s_v[e.r][e.c] = EDGE_SYSTEM;
    check_new_boxes(EDGE_SYSTEM);
    snprintf(s_status, sizeof(s_status), "Corruption struck!");
}

static void check_game_over(void)
{
    if (s_score >= WIN_SCORE) { s_phase = PHASE_WIN;  return; }
    if (board_full())           s_phase = PHASE_LOSE;
}

static bool find_edge_at(Vector2 pos, int *out_is_h, int *out_r, int *out_c)
{
    float best  = s_cs * 0.38f;
    bool  found = false;
    for (int r = 0; r < DOTS_R; r++)
        for (int c = 0; c < BOX_C; c++) {
            if (s_h[r][c]) continue;
            float d = pt_seg_dist(pos, dot_pos(r, c), dot_pos(r, c+1));
            if (d < best) {
                best = d; *out_is_h = 1; *out_r = r; *out_c = c; found = true;
            }
        }
    for (int r = 0; r < BOX_R; r++)
        for (int c = 0; c < DOTS_C; c++) {
            if (s_v[r][c]) continue;
            float d = pt_seg_dist(pos, dot_pos(r, c), dot_pos(r+1, c));
            if (d < best) {
                best = d; *out_is_h = 0; *out_r = r; *out_c = c; found = true;
            }
        }
    return found;
}

static void place_player_edge(int is_h, int r, int c)
{
    if (is_h) s_h[r][c] = EDGE_PLAYER;
    else      s_v[r][c] = EDGE_PLAYER;

    int boxes = check_new_boxes(EDGE_PLAYER);
    s_score  += boxes;
    check_game_over();
    if (s_phase != PHASE_PLAYING) return;

    if (boxes > 0) {
        snprintf(s_status, sizeof(s_status),
                 "Box! +%d  (score: %d / %d)", boxes, s_score, WIN_SCORE);
    } else {
        if (s_stab_active) {
            s_stab_active = false;
            snprintf(s_status, sizeof(s_status), "Stabilizer blocked corruption!");
        } else {
            do_corruption();
        }
        check_game_over();
    }
}

/* ------------------------------------------------------------------ init */

static void DotsBoxesInit(void)
{
    for (int r = 0; r < DOTS_R; r++)
        for (int c = 0; c < BOX_C; c++)
            s_h[r][c] = EDGE_EMPTY;
    for (int r = 0; r < BOX_R; r++)
        for (int c = 0; c < DOTS_C; c++)
            s_v[r][c] = EDGE_EMPTY;
    for (int r = 0; r < BOX_R; r++)
        for (int c = 0; c < BOX_C; c++)
            s_box[r][c] = EDGE_EMPTY;
    s_score      = 0;
    s_phase      = PHASE_PLAYING;
    s_stabilizer = true;
    s_stab_active = false;
    s_prev_tc    = 0;
    s_tapped     = false;
    snprintf(s_status, sizeof(s_status), "Place an edge to begin");
}

/* ---------------------------------------------------------------- update */

static void DotsBoxesUpdate(float dt)
{
    (void)dt;
    compute_layout();

    s_tapped = false;
    int tc = GetTouchPointCount();
    if (tc > 0) s_tap_pos = GetTouchPosition(0);
    if (tc == 0 && s_prev_tc > 0) s_tapped = true;
    s_prev_tc = tc;

    if (tc == 0) {
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            s_tapped  = true;
            s_tap_pos = GetMousePosition();
        } else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            s_tap_pos = GetMousePosition();
        }
    }

    if (!s_tapped) return;

    if (s_phase != PHASE_PLAYING) { DotsBoxesInit(); return; }

    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();

    if (s_stabilizer || s_stab_active) {
        Rectangle btn = { sw * 0.15f, sh * 0.87f, sw * 0.70f, sh * 0.06f };
        if (CheckCollisionPointRec(s_tap_pos, btn)) {
            if (!s_stab_active) {
                s_stab_active = true;
                s_stabilizer  = false;
                snprintf(s_status, sizeof(s_status), "Stabilizer armed!");
            } else {
                s_stab_active = false;
                s_stabilizer  = true;
                snprintf(s_status, sizeof(s_status), "Stabilizer disarmed");
            }
            return;
        }
    }

    int is_h, er, ec;
    if (find_edge_at(s_tap_pos, &is_h, &er, &ec))
        place_player_edge(is_h, er, ec);
}

/* ------------------------------------------------------------------ draw */

static void DotsBoxesDraw(void)
{
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    ClearBackground((Color){250, 248, 239, 255});
    compute_layout();

    int fs = (int)(sh * 0.030f);
    if (fs < 14) fs = 14;
    int fs_big = (int)(sh * 0.055f);
    if (fs_big < 24) fs_big = 24;

    char buf[80];
    snprintf(buf, sizeof(buf), "Score: %d / %d", s_score, WIN_SCORE);
    DrawText(buf, (int)(sw * 0.05f), (int)(sh * 0.03f), fs,
             (Color){119, 110, 101, 255});

    int status_w = MeasureText(s_status, fs - 2);
    DrawText(s_status, (int)((sw - status_w) * 0.5f), (int)(sh * 0.075f),
             fs - 2, (Color){150, 120, 90, 255});

    float bw = sw * 0.70f, bx = (sw - bw) * 0.5f, by = sh * 0.11f, bh = sh * 0.016f;
    DrawRectangle((int)bx, (int)by, (int)bw, (int)bh, (Color){205, 193, 180, 255});
    float fill = (float)s_score / WIN_SCORE;
    if (fill > 1.0f) fill = 1.0f;
    if (fill > 0.0f)
        DrawRectangle((int)bx, (int)by, (int)(bw * fill), (int)bh,
                      (Color){100, 160, 100, 255});

    for (int r = 0; r < BOX_R; r++)
        for (int c = 0; c < BOX_C; c++) {
            if (!s_box[r][c]) continue;
            Vector2 tl  = dot_pos(r, c);
            Color   col = (s_box[r][c] == EDGE_PLAYER)
                        ? (Color){173, 216, 230, 210}
                        : (Color){255, 160, 122, 180};
            DrawRectangle((int)tl.x + 2, (int)tl.y + 2,
                          (int)s_cs - 4, (int)s_cs - 4, col);
        }

    Vector2 mpos = (GetTouchPointCount() > 0)
                 ? GetTouchPosition(0) : GetMousePosition();
    int  hov_is_h = 0, hov_r = -1, hov_c = -1;
    bool hov = (s_phase == PHASE_PLAYING) &&
               find_edge_at(mpos, &hov_is_h, &hov_r, &hov_c);

    int thick_set   = (int)(s_cs * 0.12f);
    if (thick_set   < 4) thick_set   = 4;
    int thick_empty = (int)(s_cs * 0.045f);
    if (thick_empty < 2) thick_empty = 2;

    for (int r = 0; r < DOTS_R; r++)
        for (int c = 0; c < BOX_C; c++) {
            Vector2 a = dot_pos(r, c), b = dot_pos(r, c+1);
            int v = s_h[r][c];
            if (v == EDGE_PLAYER)
                DrawLineEx(a, b, (float)thick_set, (Color){50, 80, 160, 255});
            else if (v == EDGE_SYSTEM)
                DrawLineEx(a, b, (float)thick_set, (Color){180, 60, 50, 255});
            else if (hov && hov_is_h && hov_r == r && hov_c == c)
                DrawLineEx(a, b, (float)(thick_empty * 2), (Color){100, 150, 220, 200});
            else
                DrawLineEx(a, b, (float)thick_empty, (Color){180, 170, 160, 130});
        }

    for (int r = 0; r < BOX_R; r++)
        for (int c = 0; c < DOTS_C; c++) {
            Vector2 a = dot_pos(r, c), b = dot_pos(r+1, c);
            int v = s_v[r][c];
            if (v == EDGE_PLAYER)
                DrawLineEx(a, b, (float)thick_set, (Color){50, 80, 160, 255});
            else if (v == EDGE_SYSTEM)
                DrawLineEx(a, b, (float)thick_set, (Color){180, 60, 50, 255});
            else if (hov && !hov_is_h && hov_r == r && hov_c == c)
                DrawLineEx(a, b, (float)(thick_empty * 2), (Color){100, 150, 220, 200});
            else
                DrawLineEx(a, b, (float)thick_empty, (Color){180, 170, 160, 130});
        }

    int dot_rad = (int)(s_cs * 0.085f);
    if (dot_rad < 5) dot_rad = 5;
    for (int r = 0; r < DOTS_R; r++)
        for (int c = 0; c < DOTS_C; c++)
            DrawCircleV(dot_pos(r, c), (float)dot_rad, (Color){119, 110, 101, 255});

    if (s_stabilizer || s_stab_active) {
        Rectangle btn    = { sw * 0.15f, sh * 0.87f, sw * 0.70f, sh * 0.06f };
        Color     btn_bg = s_stab_active ? (Color){100, 180, 100, 220}
                                         : (Color){187, 173, 160, 200};
        DrawRectangleRounded(btn, 0.30f, 8, btn_bg);
        const char *lbl = s_stab_active
                        ? "STABILIZER ARMED  (tap to cancel)"
                        : "USE STABILIZER  (blocks next corruption)";
        int lfs = fs - 2;
        int lw  = MeasureText(lbl, lfs);
        DrawText(lbl,
                 (int)(btn.x + (btn.width  - lw)  * 0.5f),
                 (int)(btn.y + (btn.height - lfs) * 0.5f),
                 lfs, WHITE);
    }

    {
        int lfs = fs - 4;
        if (lfs < 10) lfs = 10;
        int ly = (int)(sh * 0.83f);
        DrawRectangle((int)(sw * 0.15f), ly, lfs, lfs, (Color){173, 216, 230, 210});
        DrawText("your box", (int)(sw * 0.15f + lfs + 6), ly, lfs,
                 (Color){119, 110, 101, 200});
        DrawRectangle((int)(sw * 0.52f), ly, lfs, lfs, (Color){255, 160, 122, 180});
        DrawText("corrupted", (int)(sw * 0.52f + lfs + 6), ly, lfs,
                 (Color){119, 110, 101, 200});
    }

    if (s_phase != PHASE_PLAYING) {
        DrawRectangle(0, 0, (int)sw, (int)sh, (Color){0, 0, 0, 150});
        const char *title = (s_phase == PHASE_WIN) ? "YOU WIN!" : "BOARD CORRUPTED";
        Color tcol = (s_phase == PHASE_WIN) ? (Color){100, 220, 100, 255}
                                            : (Color){220, 80, 80, 255};
        int tw = MeasureText(title, fs_big);
        DrawText(title, (int)((sw - tw) * 0.5f), (int)(sh * 0.40f), fs_big, tcol);

        snprintf(buf, sizeof(buf), "Final score: %d / %d", s_score, WIN_SCORE);
        int bw2 = MeasureText(buf, fs);
        DrawText(buf, (int)((sw - bw2) * 0.5f), (int)(sh * 0.49f), fs, WHITE);

        const char *hint = "Tap anywhere to play again";
        int hw = MeasureText(hint, fs - 2);
        DrawText(hint, (int)((sw - hw) * 0.5f), (int)(sh * 0.56f),
                 fs - 2, (Color){200, 200, 200, 255});
    }
}

/* ---------------------------------------------------------------- deinit */

static void DotsBoxesDeinit(void) {}

/* ---------------------------------------------------------------- export */

const Prototype DotsBoxesProto = {
    .name        = "Dots & Boxes",
    .description = "Corrupted Grid: score 15 boxes before corruption fills the board.",
    .Init        = DotsBoxesInit,
    .Update      = DotsBoxesUpdate,
    .Draw        = DotsBoxesDraw,
    .Deinit      = DotsBoxesDeinit,
};
