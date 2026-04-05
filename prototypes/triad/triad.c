#include "triad.h"
#include "raylib.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ----------------------------------------------------------------- constants */

#define HAND_MAX        4
#define CHAIN_QUEUE_MAX 16
#define CONFIG_COUNT    5

/* Direction order: N=0, E=1, S=2, W=3.  Opposite = (d+2)%4. */
static const int DR[4] = {-1,  0, 1, 0};
static const int DC[4] = { 0,  1, 0, -1};

/* ------------------------------------------------------------------- typedefs */

typedef struct {
    int  values[4]; /* N, E, S, W */
    int  flip_count;
    bool is_neutral;
    bool active;
} Tile;

typedef struct {
    int neutral_count;
    int val_min, val_max;
    int target_score;
    int vanish_at;
} RoundConfig;

typedef enum {
    PHASE_PLAYING,
    PHASE_NEXT_ROUND,
    PHASE_GAME_OVER,
} GamePhase;

static const RoundConfig ROUND_CONFIGS[CONFIG_COUNT] = {
    { 3, 1, 4, 1000, 3 },   /* round 1 */
    { 3, 1, 5, 1500, 3 },   /* round 2 */
    { 4, 1, 6, 2000, 4 },   /* round 3 */
    { 4, 2, 7, 2750, 4 },   /* round 4 */
    { 5, 2, 7, 3500, 5 },   /* round 5+ */
};

/* ---------------------------------------------------------------- module state */

static Tile      s_board[4][4];
static Tile      s_hand[HAND_MAX];
static int       s_selected;       /* -1 = none, 0-3 = hand slot index */
static int       s_score;
static int       s_round;
static GamePhase s_phase;
static Vector2   s_prev_touch_pos;
static int       s_prev_touch_count;

/* -------------------------------------------------------------------- helpers */

static bool cell_valid(int r, int c)
{
    if (r < 0 || r >= 4 || c < 0 || c >= 4) return false;
    /* corners are blocked */
    if ((r == 0 || r == 3) && (c == 0 || c == 3)) return false;
    return true;
}

static Tile make_tile(int val_min, int val_max, bool is_neutral)
{
    Tile t;
    memset(&t, 0, sizeof(t));
    for (int i = 0; i < 4; i++)
        t.values[i] = GetRandomValue(val_min, val_max);
    t.is_neutral = is_neutral;
    t.flip_count = 0;
    t.active     = true;
    return t;
}

static void rotate_tile(Tile *t)
{
    /* clockwise: [N,E,S,W] -> [W,N,E,S] */
    int tmp      = t->values[3];
    t->values[3] = t->values[2];
    t->values[2] = t->values[1];
    t->values[1] = t->values[0];
    t->values[0] = tmp;
}

static Color tile_color(const Tile *t)
{
    if (t->is_neutral && t->flip_count == 0)
        return (Color){184, 169, 154, 255}; /* #b8a99a neutral beige */
    switch (t->flip_count) {
        case 0:  return (Color){238, 228, 218, 255}; /* #eee4da light */
        case 1:  return (Color){242, 177, 121, 255}; /* #f2b179 orange */
        case 2:  return (Color){246, 124,  95, 255}; /* #f67c5f red-orange */
        default: return (Color){232,  93,  58, 255}; /* #e85d3a deep red */
    }
}

static const RoundConfig *current_config(void)
{
    int idx = s_round - 1;
    if (idx >= CONFIG_COUNT) idx = CONFIG_COUNT - 1;
    return &ROUND_CONFIGS[idx];
}

static bool board_full(void)
{
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (cell_valid(r, c) && !s_board[r][c].active)
                return false;
    return true;
}

static bool hand_empty(void)
{
    for (int i = 0; i < HAND_MAX; i++)
        if (s_hand[i].active) return false;
    return true;
}

/* --------------------------------------------------------------- layout rects */

static Rectangle cell_rect(int r, int c)
{
    int sw        = GetScreenWidth();
    int sh        = GetScreenHeight();
    int cell_size = sw * 22 / 100;
    int board_x   = (sw - cell_size * 4) / 2;
    int board_y   = sh * 20 / 100;
    return (Rectangle){
        (float)(board_x + c * cell_size + 2),
        (float)(board_y + r * cell_size + 2),
        (float)(cell_size - 4),
        (float)(cell_size - 4),
    };
}

static Rectangle hand_slot_rect(int i)
{
    int sw        = GetScreenWidth();
    int sh        = GetScreenHeight();
    int cell_size = sw * 22 / 100;
    int hand_x    = (sw - cell_size * 4) / 2;
    int hand_y    = sh * 82 / 100;
    return (Rectangle){
        (float)(hand_x + i * cell_size + 2),
        (float)(hand_y + 2),
        (float)(cell_size - 4),
        (float)(cell_size - 4),
    };
}

/* ------------------------------------------------------------- round/game setup */

static void place_neutrals(int count, int val_min, int val_max)
{
    /* collect valid cells, Fisher-Yates shuffle, pick first `count` */
    int vr[12], vc[12], n = 0;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (cell_valid(r, c)) { vr[n] = r; vc[n] = c; n++; }

    for (int i = n - 1; i > 0; i--) {
        int j  = GetRandomValue(0, i);
        int tr = vr[i], tc = vc[i];
        vr[i]  = vr[j]; vc[i] = vc[j];
        vr[j]  = tr;    vc[j] = tc;
    }

    for (int k = 0; k < count && k < n; k++) {
        s_board[vr[k]][vc[k]] = make_tile(val_min, val_max, true);
    }
}

static void init_round(void)
{
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            s_board[r][c].active = false;

    const RoundConfig *cfg = current_config();
    place_neutrals(cfg->neutral_count, cfg->val_min, cfg->val_max);

    for (int i = 0; i < HAND_MAX; i++)
        s_hand[i] = make_tile(1, 9, false);

    s_selected = -1;
    s_score    = 0;
    s_phase    = PHASE_PLAYING;
}

static void draw_new_tiles(void)
{
    /* draw up to 3 tiles into empty hand slots */
    int drawn = 0;
    for (int i = 0; i < HAND_MAX && drawn < 3; i++) {
        if (!s_hand[i].active) {
            s_hand[i] = make_tile(1, 9, false);
            drawn++;
        }
    }

    /* round 3+: spawn one extra neutral on the board */
    if (s_round >= 3 && !board_full()) {
        const RoundConfig *cfg = current_config();
        int er[12], ec[12], en = 0;
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                if (cell_valid(r, c) && !s_board[r][c].active) {
                    er[en] = r; ec[en] = c; en++;
                }
        if (en > 0) {
            int k = GetRandomValue(0, en - 1);
            s_board[er[k]][ec[k]] = make_tile(cfg->val_min, cfg->val_max, true);
        }
    }
}

/* ----------------------------------------------------------- flip resolution */

static int resolve_placement(int pr, int pc)
{
    const RoundConfig *cfg = current_config();
    int points = 0;
    Tile *placed = &s_board[pr][pc];

    /* gather occupied neighbors */
    int nb_r[4], nb_c[4], nb_d[4], nb_n = 0;
    for (int d = 0; d < 4; d++) {
        int nr = pr + DR[d], nc = pc + DC[d];
        if (cell_valid(nr, nc) && s_board[nr][nc].active) {
            nb_r[nb_n] = nr;
            nb_c[nb_n] = nc;
            nb_d[nb_n] = d;
            nb_n++;
        }
    }
    if (nb_n == 0) return 0;

    /* per-neighbor: same flag and sum value */
    bool nb_same[4]  = {false};
    int  nb_sum[4]   = {0};
    int  sum_freq[19] = {0};   /* sums range 2..18 */
    int  same_count  = 0;

    for (int i = 0; i < nb_n; i++) {
        int our   = placed->values[nb_d[i]];
        int their = s_board[nb_r[i]][nb_c[i]].values[(nb_d[i] + 2) % 4];
        if (our == their) { nb_same[i] = true; same_count++; }
        nb_sum[i] = our + their;
        if (nb_sum[i] >= 2 && nb_sum[i] <= 18)
            sum_freq[nb_sum[i]]++;
    }

    /* chain BFS queue — at most 11 non-placed valid cells */
    int  cq_r[CHAIN_QUEUE_MAX], cq_c[CHAIN_QUEUE_MAX];
    int  cq_back   = 0;
    bool in_chain[4][4];
    memset(in_chain, 0, sizeof(in_chain));

    /* track neighbors already flipped by Same/Sum (skip for Higher) */
    bool handled[4] = {false};

    /* Same rule: 2+ equal pairs */
    if (same_count >= 2) {
        for (int i = 0; i < nb_n; i++) {
            if (!nb_same[i]) continue;
            s_board[nb_r[i]][nb_c[i]].flip_count++;
            points += 15;
            handled[i] = true;
            if (!in_chain[nb_r[i]][nb_c[i]] && cq_back < CHAIN_QUEUE_MAX) {
                in_chain[nb_r[i]][nb_c[i]] = true;
                cq_r[cq_back] = nb_r[i];
                cq_c[cq_back] = nb_c[i];
                cq_back++;
            }
        }
    }

    /* Sum rule: 2+ neighbors with the same (our + their) sum */
    for (int i = 0; i < nb_n; i++) {
        if (handled[i]) continue;
        int sv = nb_sum[i];
        if (sv >= 2 && sv <= 18 && sum_freq[sv] >= 2) {
            s_board[nb_r[i]][nb_c[i]].flip_count++;
            points += 15;
            handled[i] = true;
            if (!in_chain[nb_r[i]][nb_c[i]] && cq_back < CHAIN_QUEUE_MAX) {
                in_chain[nb_r[i]][nb_c[i]] = true;
                cq_r[cq_back] = nb_r[i];
                cq_c[cq_back] = nb_c[i];
                cq_back++;
            }
        }
    }

    /* Higher rule: your side > their opposing side (no chain) */
    for (int i = 0; i < nb_n; i++) {
        if (handled[i]) continue;
        int our   = placed->values[nb_d[i]];
        int their = s_board[nb_r[i]][nb_c[i]].values[(nb_d[i] + 2) % 4];
        if (our > their) {
            s_board[nb_r[i]][nb_c[i]].flip_count++;
            points += 5;
        }
    }

    /* BFS chain from Same/Sum-flipped tiles (Higher rule, escalating points) */
    int wave_start = 0;
    int wave_end   = cq_back;
    int depth      = 2;

    while (wave_start < wave_end) {
        for (int qi = wave_start; qi < wave_end; qi++) {
            int fr = cq_r[qi], fc = cq_c[qi];
            Tile *flipped = &s_board[fr][fc];
            for (int d = 0; d < 4; d++) {
                int nr = fr + DR[d], nc = fc + DC[d];
                if (!cell_valid(nr, nc)) continue;
                if (!s_board[nr][nc].active) continue;
                if (in_chain[nr][nc]) continue;
                if (nr == pr && nc == pc) continue; /* skip the just-placed tile */
                int our   = flipped->values[d];
                int their = s_board[nr][nc].values[(d + 2) % 4];
                if (our > their) {
                    s_board[nr][nc].flip_count++;
                    points += 10 * depth;
                    in_chain[nr][nc] = true;
                    if (cq_back < CHAIN_QUEUE_MAX) {
                        cq_r[cq_back] = nr;
                        cq_c[cq_back] = nc;
                        cq_back++;
                    }
                }
            }
        }
        wave_start = wave_end;
        wave_end   = cq_back;
        depth++;
    }

    /* vanish: tiles at or beyond threshold are removed */
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (cell_valid(r, c) && s_board[r][c].active &&
                s_board[r][c].flip_count >= cfg->vanish_at) {
                s_board[r][c].active = false;
                points += 25;
            }

    return points;
}

/* -------------------------------------------------------------- input handling */

static void handle_tap(Vector2 pos)
{
    /* any tap during overlay advances the phase */
    if (s_phase != PHASE_PLAYING) {
        if (s_phase == PHASE_NEXT_ROUND) s_round++;
        else                             s_round = 1;
        init_round();
        return;
    }

    /* hand tile: select or rotate */
    for (int i = 0; i < HAND_MAX; i++) {
        if (!s_hand[i].active) continue;
        if (CheckCollisionPointRec(pos, hand_slot_rect(i))) {
            if (s_selected == i) rotate_tile(&s_hand[i]);
            else                 s_selected = i;
            return;
        }
    }

    /* board cell: place selected tile */
    if (s_selected >= 0) {
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                if (!cell_valid(r, c)) continue;
                if (s_board[r][c].active) continue;
                if (!CheckCollisionPointRec(pos, cell_rect(r, c))) continue;

                s_board[r][c]             = s_hand[s_selected];
                s_board[r][c].is_neutral  = false;
                s_hand[s_selected].active = false;
                int prev_sel              = s_selected;
                s_selected                = -1;
                (void)prev_sel;

                s_score += resolve_placement(r, c);

                if (hand_empty()) draw_new_tiles();
                if (board_full()) {
                    s_phase = (s_score >= current_config()->target_score)
                              ? PHASE_NEXT_ROUND : PHASE_GAME_OVER;
                }
                return;
            }
        }
    }

    s_selected = -1; /* tap on empty space deselects */
}

/* -------------------------------------------------------------- tile rendering */

static void draw_tile_at(Rectangle rect, const Tile *t, bool selected)
{
    Color bg = tile_color(t);
    DrawRectangleRounded(rect, 0.15f, 8, bg);

    if (selected)
        DrawRectangleLinesEx(rect, 4.0f, WHITE);

    int fs = (int)(rect.width * 0.28f);
    /* dark text on light beige, white text on coloured tiles */
    Color tc = (t->flip_count == 0 && !t->is_neutral)
               ? (Color){119, 110, 101, 255}
               : WHITE;

    char buf[4];
    int  tw;

    /* North — top-center */
    snprintf(buf, sizeof(buf), "%d", t->values[0]);
    tw = MeasureText(buf, fs);
    DrawText(buf, (int)(rect.x + rect.width  / 2 - tw / 2),
             (int)(rect.y + 4), fs, tc);

    /* East — right-center */
    snprintf(buf, sizeof(buf), "%d", t->values[1]);
    tw = MeasureText(buf, fs);
    DrawText(buf, (int)(rect.x + rect.width - tw - 4),
             (int)(rect.y + rect.height / 2 - fs / 2), fs, tc);

    /* South — bottom-center */
    snprintf(buf, sizeof(buf), "%d", t->values[2]);
    tw = MeasureText(buf, fs);
    DrawText(buf, (int)(rect.x + rect.width  / 2 - tw / 2),
             (int)(rect.y + rect.height - fs - 4), fs, tc);

    /* West — left-center */
    snprintf(buf, sizeof(buf), "%d", t->values[3]);
    DrawText(buf, (int)(rect.x + 4),
             (int)(rect.y + rect.height / 2 - fs / 2), fs, tc);
}

/* --------------------------------------------------------- prototype callbacks */

static void TriadInit(void)
{
    s_round            = 1;
    s_prev_touch_count = 0;
    s_prev_touch_pos   = (Vector2){-1.0f, -1.0f};
    init_round();
}

static void TriadUpdate(float dt)
{
    (void)dt;

    int     tc      = GetTouchPointCount();
    Vector2 tap_pos = {-1.0f, -1.0f};
    bool    tapped  = false;

    if (tc > 0)
        s_prev_touch_pos = GetTouchPosition(0);
    if (tc == 0 && s_prev_touch_count > 0) {
        tap_pos = s_prev_touch_pos;
        tapped  = true;
    }
    s_prev_touch_count = tc;

    /* mouse fallback on desktop */
    if (tc == 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        tap_pos = GetMousePosition();
        tapped  = true;
    }

    if (tapped) handle_tap(tap_pos);
}

static void TriadDraw(void)
{
    ClearBackground((Color){250, 248, 239, 255}); /* #faf8ef warm cream */

    int sw        = GetScreenWidth();
    int sh        = GetScreenHeight();
    int cell_size = sw * 22 / 100;
    int board_x   = (sw - cell_size * 4) / 2;
    int board_y   = sh * 20 / 100;
    int fs_hud    = sw / 22;
    int fs_small  = sw / 38;

    const RoundConfig *cfg      = current_config();
    Color              text_col = (Color){119, 110, 101, 255}; /* #776e65 */
    char               buf[64];

    /* ---- HUD ---- */
    snprintf(buf, sizeof(buf), "Round %d", s_round);
    DrawText(buf, sw * 5 / 100, sh * 3 / 100, fs_hud, text_col);

    snprintf(buf, sizeof(buf), "%d / %d", s_score, cfg->target_score);
    Color score_col = (s_score >= cfg->target_score)
                      ? (Color){119, 172, 80, 255} : text_col;
    DrawText(buf, (sw - MeasureText(buf, fs_hud)) / 2, sh * 3 / 100,
             fs_hud, score_col);

    snprintf(buf, sizeof(buf), "Vanish %dx", cfg->vanish_at);
    DrawText(buf, sw - MeasureText(buf, fs_hud) - sw * 5 / 100,
             sh * 3 / 100, fs_hud, text_col);

    /* hint line */
    const char *hint = "Tap: select  *  Tap again: rotate  *  Tap cell: place";
    DrawText(hint, (sw - MeasureText(hint, fs_small)) / 2,
             sh * 8 / 100, fs_small, (Color){160, 150, 140, 255});

    /* ---- Board container ---- */
    int pad = sw / 50;
    DrawRectangle(board_x - pad, board_y - pad,
                  cell_size * 4 + pad * 2, cell_size * 4 + pad * 2,
                  (Color){187, 173, 160, 255}); /* #bbada0 */

    /* ---- Board cells ---- */
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            Rectangle rect = cell_rect(r, c);
            if (!cell_valid(r, c)) {
                DrawRectangleRec(rect, (Color){143, 122, 102, 255}); /* corner */
            } else if (!s_board[r][c].active) {
                /* empty valid cell; tint on hover when a tile is selected */
                Color empty = (Color){205, 193, 180, 255};
                if (s_selected >= 0 &&
                    CheckCollisionPointRec(GetMousePosition(), rect))
                    empty = (Color){220, 212, 200, 255};
                DrawRectangleRec(rect, empty);
            } else {
                draw_tile_at(rect, &s_board[r][c], false);
            }
        }
    }

    /* ---- Hand ---- */
    int hand_y = sh * 82 / 100;
    int hand_x = (sw - cell_size * 4) / 2;

    DrawText("Hand:", hand_x, hand_y - fs_hud - 4, fs_hud, text_col);

    for (int i = 0; i < HAND_MAX; i++) {
        Rectangle rect = hand_slot_rect(i);
        if (!s_hand[i].active) {
            DrawRectangleRec(rect, (Color){205, 193, 180, 100});
            DrawRectangleLinesEx(rect, 2.0f, (Color){180, 170, 160, 180});
        } else {
            draw_tile_at(rect, &s_hand[i], s_selected == i);
        }
    }

    /* ---- Phase overlay ---- */
    if (s_phase != PHASE_PLAYING) {
        DrawRectangle(0, 0, sw, sh, (Color){0, 0, 0, 180});

        int bw = sw * 70 / 100;
        int bh = sh * 38 / 100;
        int bx = (sw - bw) / 2;
        int by = (sh - bh) / 2;
        DrawRectangleRounded(
            (Rectangle){(float)bx, (float)by, (float)bw, (float)bh},
            0.1f, 8, (Color){250, 248, 239, 255});

        int fs_big = sw / 12;
        int fs_med = sw / 22;

        if (s_phase == PHASE_NEXT_ROUND) {
            const char *title = "Round Clear!";
            DrawText(title, bx + (bw - MeasureText(title, fs_big)) / 2,
                     by + bh / 8, fs_big, (Color){119, 172, 80, 255});
        } else {
            const char *title = "Game Over";
            DrawText(title, bx + (bw - MeasureText(title, fs_big)) / 2,
                     by + bh / 8, fs_big, (Color){196, 107, 100, 255});
        }

        snprintf(buf, sizeof(buf), "Score: %d  /  Target: %d",
                 s_score, cfg->target_score);
        DrawText(buf, bx + (bw - MeasureText(buf, fs_med)) / 2,
                 by + bh * 2 / 5, fs_med, text_col);

        const char *sub = (s_phase == PHASE_NEXT_ROUND)
                          ? "Tap to continue" : "Tap to restart";
        DrawText(sub, bx + (bw - MeasureText(sub, fs_med)) / 2,
                 by + bh * 3 / 4, fs_med, (Color){150, 140, 130, 255});
    }
}

static void TriadDeinit(void)
{
    /* nothing to free */
}

const Prototype TriadProto = {
    .name        = "Triad",
    .description = "Triple Triad puzzle: place tiles, flip by Higher/Same/Sum, chain reactions.",
    .Init        = TriadInit,
    .Update      = TriadUpdate,
    .Draw        = TriadDraw,
    .Deinit      = TriadDeinit,
};
