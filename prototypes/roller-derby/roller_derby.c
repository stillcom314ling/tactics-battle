#include "roller_derby.h"
#include "raylib.h"
#include "raygui.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ----------------------------------------------------------------- constants */

#define TRACK_LEN      12
#define LANE_COUNT      3
#define UNIT_COUNT      6
#define TEAM_SIZE       3
#define MOMENTUM_MAX    5
#define WIN_SCORE       3
#define TWO_PI         6.28318530717958647692f
#define HALF_PI        1.57079632679489661923f

/* ------------------------------------------------------------------ typedefs */

typedef enum { INTENT_CRUISE, INTENT_PUSH, INTENT_BRACE, INTENT_HIT } Intent;
typedef enum { GS_PLAYING, GS_GAME_OVER } GameState;

typedef struct {
    int  pos;
    int  lane;
    int  momentum;
    bool active;
    int  cooldown;
} Unit;

typedef struct {
    int    lane_delta;
    Intent intent;
} Action;

/* --------------------------------------------------------------- module state */

static Unit      s_units[UNIT_COUNT];
static Action    s_actions[UNIT_COUNT];
static int       s_score[2];
static int       s_selected;
static GameState s_gs;
static char      s_msg[128];

/* ------------------------------------------------------------------- helpers */

static int team_of(int i)    { return i / TEAM_SIZE; }
static int role_of(int i)    { return i % TEAM_SIZE; }
static bool is_jammer(int i) { return role_of(i) == 0; }

static int circ_dist(int a, int b)
{
    int d = ((b - a + TRACK_LEN) % TRACK_LEN);
    return (d <= TRACK_LEN / 2) ? d : TRACK_LEN - d;
}

/* Forward direction distance (a → b going forward = increasing pos) */
static int circ_fwd(int from, int to)
{
    return (to - from + TRACK_LEN) % TRACK_LEN;
}

static bool is_jammer_through(int i)
{
    if (!s_units[i].active) return false;
    int opp_team = 1 - team_of(i);
    int p = s_units[i].pos;
    /* Enemy blockers: the two non-jammer units of opp_team */
    for (int j = 0; j < UNIT_COUNT; j++) {
        if (team_of(j) != opp_team) continue;
        if (is_jammer(j)) continue;
        if (!s_units[j].active) continue;
        if (s_units[j].pos == p || s_units[j].pos == (p + 1) % TRACK_LEN)
            return false;
    }
    return true;
}

static int pack_center(void)
{
    /* Average position of all active blockers (roles 1,2) */
    int sum = 0, cnt = 0;
    for (int i = 0; i < UNIT_COUNT; i++) {
        if (!s_units[i].active) continue;
        if (is_jammer(i)) continue;
        sum += s_units[i].pos;
        cnt++;
    }
    return (cnt > 0) ? (sum / cnt) : 0;
}

static void clamp_momentum(int i)
{
    if (s_units[i].momentum < 0) s_units[i].momentum = 0;
    if (s_units[i].momentum > MOMENTUM_MAX) s_units[i].momentum = MOMENTUM_MAX;
}

/* --------------------------------------------------------- resolve_turn logic */

static void resolve_turn(void)
{
    /* Step 1 — compute target positions */
    int target_pos[UNIT_COUNT], target_lane[UNIT_COUNT], move_amount[UNIT_COUNT];
    for (int i = 0; i < UNIT_COUNT; i++) {
        if (!s_units[i].active) {
            target_pos[i]  = s_units[i].pos;
            target_lane[i] = s_units[i].lane;
            move_amount[i] = 0;
            continue;
        }
        int nl = s_units[i].lane + s_actions[i].lane_delta;
        if (nl < 0) nl = 0;
        if (nl >= LANE_COUNT) nl = LANE_COUNT - 1;
        target_lane[i] = nl;

        int intent_move = 0;
        if (s_actions[i].intent == INTENT_PUSH)  intent_move =  1;
        if (s_actions[i].intent == INTENT_BRACE) intent_move = -1;
        int move = 1 + intent_move;
        if (move < 0) move = 0;
        move_amount[i] = move;
        target_pos[i]  = (s_units[i].pos + move) % TRACK_LEN;
    }

    /* Step 2 — compute strength */
    int strength[UNIT_COUNT];
    for (int i = 0; i < UNIT_COUNT; i++) {
        strength[i] = s_units[i].momentum;
        if (s_actions[i].intent == INTENT_PUSH)  strength[i]++;
        if (s_actions[i].intent == INTENT_BRACE) strength[i]++;
        if (s_actions[i].intent == INTENT_HIT)   strength[i]++;
    }

    /* Step 3 — resolve conflict clusters */
    bool final_success[UNIT_COUNT];
    bool displaced[UNIT_COUNT];
    bool was_uncontested[UNIT_COUNT];
    memset(final_success,   0, sizeof(final_success));
    memset(displaced,       0, sizeof(displaced));
    memset(was_uncontested, 0, sizeof(was_uncontested));

    /* winner_at[pos][lane] = winner unit index, -1 = empty */
    int winner_at[TRACK_LEN][LANE_COUNT];
    for (int p = 0; p < TRACK_LEN; p++)
        for (int l = 0; l < LANE_COUNT; l++)
            winner_at[p][l] = -1;

    /* For each target cell, gather contenders */
    for (int tp = 0; tp < TRACK_LEN; tp++) {
        for (int tl = 0; tl < LANE_COUNT; tl++) {
            int contenders[UNIT_COUNT];
            int cnt = 0;
            for (int i = 0; i < UNIT_COUNT; i++) {
                if (!s_units[i].active) continue;
                if (target_pos[i] == tp && target_lane[i] == tl)
                    contenders[cnt++] = i;
            }
            if (cnt == 0) continue;
            if (cnt == 1) {
                final_success[contenders[0]]   = true;
                was_uncontested[contenders[0]] = true;
                winner_at[tp][tl]              = contenders[0];
                continue;
            }
            /* Find max strength */
            int max_str = 0;
            for (int k = 0; k < cnt; k++) {
                int s = strength[contenders[k]];
                if (s > max_str) max_str = s;
            }
            int max_cnt = 0;
            int winner  = -1;
            for (int k = 0; k < cnt; k++) {
                if (strength[contenders[k]] == max_str) {
                    max_cnt++;
                    winner = contenders[k];
                }
            }
            if (max_cnt == 1) {
                /* Unique winner */
                final_success[winner]   = true;
                was_uncontested[winner] = false;
                winner_at[tp][tl]       = winner;
                for (int k = 0; k < cnt; k++)
                    if (contenders[k] != winner) displaced[contenders[k]] = true;
            } else {
                /* Tie — all displaced */
                for (int k = 0; k < cnt; k++)
                    displaced[contenders[k]] = true;
            }
        }
    }

    /* Step 4 — displacement fallback positions */
    int final_pos[UNIT_COUNT], final_lane[UNIT_COUNT];
    for (int i = 0; i < UNIT_COUNT; i++) {
        final_pos[i]  = target_pos[i];
        final_lane[i] = target_lane[i];
    }
    for (int i = 0; i < UNIT_COUNT; i++) {
        if (!displaced[i]) continue;
        int op = s_units[i].pos;
        int ol = s_units[i].lane;

        /* Try orig pos, orig lane */
        if (winner_at[op][ol] == -1) {
            final_pos[i] = op; final_lane[i] = ol; continue;
        }
        /* Try pos-1, orig lane */
        int pm1 = (op - 1 + TRACK_LEN) % TRACK_LEN;
        if (winner_at[pm1][ol] == -1) {
            final_pos[i] = pm1; final_lane[i] = ol; continue;
        }
        /* Try adjacent lanes at orig pos */
        int tried = 0;
        for (int dl = -1; dl <= 1 && !tried; dl += 2) {
            int nl = ol + dl;
            if (nl < 0 || nl >= LANE_COUNT) continue;
            if (winner_at[op][nl] == -1) {
                final_pos[i] = op; final_lane[i] = nl; tried = 1;
            }
        }
        if (!tried) {
            /* Fallback: stay at origin regardless */
            final_pos[i] = op; final_lane[i] = ol;
        }
    }
    /* Winners keep their target */
    for (int i = 0; i < UNIT_COUNT; i++) {
        if (final_success[i]) {
            final_pos[i]  = target_pos[i];
            final_lane[i] = target_lane[i];
        }
    }

    /* Step 5 — momentum update */
    int pc = pack_center();
    for (int i = 0; i < UNIT_COUNT; i++) {
        if (!s_units[i].active) continue;
        if (was_uncontested[i] && move_amount[i] > 0) s_units[i].momentum++;
        if (s_actions[i].intent == INTENT_PUSH && final_success[i]) s_units[i].momentum++;
        if (displaced[i]) s_units[i].momentum--;
        /* Pack bonus for blockers */
        if (!is_jammer(i) && circ_dist(s_units[i].pos, pc) <= 2)
            s_units[i].momentum++;
        clamp_momentum(i);
    }

    /* Step 6 — apply final positions */
    for (int i = 0; i < UNIT_COUNT; i++) {
        if (!s_units[i].active) continue;
        s_units[i].pos  = final_pos[i];
        s_units[i].lane = final_lane[i];
    }

    /* Step 7 — jammer breakthrough check */
    for (int i = 0; i < UNIT_COUNT; i++) {
        if (!s_units[i].active) continue;
        if (!is_jammer(i)) continue;
        if (is_jammer_through(i)) {
            int t = team_of(i);
            s_score[t]++;
            s_units[i].active   = false;
            s_units[i].cooldown = 2;
            snprintf(s_msg, sizeof(s_msg), "Team %d jammer scored! (%d-%d)",
                     t, s_score[0], s_score[1]);
        }
    }

    /* Step 8 — decrement cooldowns, respawn */
    for (int i = 0; i < UNIT_COUNT; i++) {
        if (s_units[i].active) continue;
        if (s_units[i].cooldown > 0) s_units[i].cooldown--;
        if (s_units[i].cooldown == 0) {
            int pc2 = pack_center();
            s_units[i].pos      = (pc2 - 2 + TRACK_LEN) % TRACK_LEN;
            s_units[i].lane     = 1;
            s_units[i].momentum = 2;
            s_units[i].active   = true;
        }
    }
}

/* --------------------------------------------------------------- AI for team 1 */

static void run_ai_team1(void)
{
    /* Unit indices 3=jammer, 4-5=blockers */
    for (int i = 3; i < UNIT_COUNT; i++) {
        if (!s_units[i].active) continue;
        s_actions[i].lane_delta = 0;
        s_actions[i].intent     = INTENT_CRUISE;

        if (is_jammer(i)) {
            /* Jammer AI */
            if (is_jammer_through(i)) {
                s_actions[i].intent     = INTENT_PUSH;
                s_actions[i].lane_delta = 0;
            } else if (s_units[i].momentum <= 1) {
                s_actions[i].intent = INTENT_CRUISE;
            } else {
                /* Find lane with fewest enemy blockers within 3 ahead */
                int best_lane  = s_units[i].lane;
                int best_count = UNIT_COUNT + 1;
                for (int tl = 0; tl < LANE_COUNT; tl++) {
                    int blocked = 0;
                    for (int j = 0; j < TEAM_SIZE; j++) {
                        if (!s_units[j].active) continue;
                        if (is_jammer(j)) continue;
                        int fd = circ_fwd(s_units[i].pos, s_units[j].pos);
                        if (fd <= 3 && s_units[j].lane == tl) blocked++;
                    }
                    if (blocked < best_count) {
                        best_count = blocked;
                        best_lane  = tl;
                    }
                }
                int dl = best_lane - s_units[i].lane;
                if (dl > 1)  dl = 1;
                if (dl < -1) dl = -1;
                s_actions[i].lane_delta = dl;
                s_actions[i].intent     = INTENT_PUSH;
            }
        } else {
            /* Blocker AI */
            int enemy_jammer_pos = s_units[0].pos;
            bool enemy_near = s_units[0].active &&
                              circ_dist(s_units[i].pos, enemy_jammer_pos) <= 2;
            if (enemy_near) {
                s_actions[i].intent     = INTENT_BRACE;
                s_actions[i].lane_delta = 0;
            } else {
                /* Check if any ally is near an enemy */
                bool should_hit = false;
                for (int a = 3; a < UNIT_COUNT && !should_hit; a++) {
                    if (a == i || !s_units[a].active) continue;
                    for (int e = 0; e < TEAM_SIZE && !should_hit; e++) {
                        if (!s_units[e].active) continue;
                        if (circ_dist(s_units[a].pos, s_units[e].pos) <= 1) {
                            should_hit = true;
                            int dl = s_units[e].lane - s_units[i].lane;
                            if (dl > 1)  dl = 1;
                            if (dl < -1) dl = -1;
                            s_actions[i].lane_delta = dl;
                        }
                    }
                }
                if (should_hit) {
                    s_actions[i].intent = INTENT_HIT;
                } else {
                    s_actions[i].intent = (GetRandomValue(0, 1) == 0)
                                          ? INTENT_CRUISE : INTENT_PUSH;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ rendering */

static void draw_track(float cx, float cy, float r)
{
    float lane_radii[LANE_COUNT] = { r * 0.55f, r * 0.75f, r * 0.95f };

    /* Track circles */
    for (int l = 0; l < LANE_COUNT; l++)
        DrawCircleLinesV((Vector2){cx, cy}, lane_radii[l], DARKGRAY);
    DrawCircleLinesV((Vector2){cx, cy}, r * 0.38f, (Color){60,60,60,255});

    /* Tick marks and position numbers */
    for (int p = 0; p < TRACK_LEN; p++) {
        float angle = -HALF_PI + TWO_PI * p / TRACK_LEN;
        float ca = cosf(angle), sa = sinf(angle);
        float ri = r * 0.38f, ro = r * 0.95f;
        DrawLineV(
            (Vector2){cx + ri*ca, cy + ri*sa},
            (Vector2){cx + ro*ca, cy + ro*sa},
            (Color){80,80,80,200}
        );
        /* Number outside outermost circle */
        float nr = r * 1.05f;
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", p);
        int fs = (int)(r * 0.07f);
        if (fs < 8) fs = 8;
        int tw = MeasureText(buf, fs);
        DrawText(buf,
                 (int)(cx + nr*ca) - tw/2,
                 (int)(cy + nr*sa) - fs/2,
                 fs, GRAY);
    }

    /* Units */
    float unit_r = r * 0.08f;
    for (int i = 0; i < UNIT_COUNT; i++) {
        if (!s_units[i].active) continue;
        float angle = -HALF_PI + TWO_PI * s_units[i].pos / TRACK_LEN;
        float lr = lane_radii[s_units[i].lane];
        float ux = cx + lr * cosf(angle);
        float uy = cy + lr * sinf(angle);

        Color col;
        if (team_of(i) == 0)
            col = is_jammer(i) ? BLUE : SKYBLUE;
        else
            col = is_jammer(i) ? RED : ORANGE;

        DrawCircleV((Vector2){ux, uy}, unit_r, col);

        /* Selection ring */
        if (i == s_selected && team_of(i) == 0)
            DrawCircleLinesV((Vector2){ux, uy}, unit_r + 2, WHITE);

        /* Momentum number */
        char mbuf[4];
        snprintf(mbuf, sizeof(mbuf), "%d", s_units[i].momentum);
        int mfs = (int)(unit_r * 1.1f);
        if (mfs < 7) mfs = 7;
        int mw = MeasureText(mbuf, mfs);
        DrawText(mbuf, (int)(ux - mw/2), (int)(uy - mfs/2), mfs, WHITE);
    }
}

static void draw_panel(int px, int py, int pw, int ph)
{
    int sw = GetScreenWidth();
    (void)sw;

    int base_fs   = ph / 28;
    if (base_fs < 10) base_fs = 10;
    int btn_h     = base_fs + 14;
    int pad       = base_fs / 2;
    int y         = py + pad;

    GuiSetStyle(DEFAULT, TEXT_SIZE, base_fs);

    /* Score */
    char score_buf[64];
    snprintf(score_buf, sizeof(score_buf), "Team 0: %d   Team 1: %d",
             s_score[0], s_score[1]);
    DrawText(score_buf, px + pad, y, base_fs, WHITE);
    y += base_fs + pad;

    DrawLine(px, y, px + pw, y, DARKGRAY);
    y += pad;

    /* "YOUR UNITS" list */
    DrawText("YOUR UNITS", px + pad, y, base_fs, LIGHTGRAY);
    y += base_fs + pad;

    const char *role_name[] = {"Jammer", "Blocker", "Blocker"};
    const char *intent_name[] = {"CRUISE", "PUSH", "BRACE", "HIT"};

    for (int i = 0; i < TEAM_SIZE; i++) {
        bool sel = (i == s_selected);
        Color row_col = sel ? (Color){60,80,120,255} : (Color){30,30,30,255};
        Rectangle row = {(float)(px + pad), (float)y, (float)(pw - pad*2), (float)(btn_h)};
        DrawRectangleRec(row, row_col);
        if (sel) DrawRectangleLinesEx(row, 2, SKYBLUE);

        char unit_buf[64];
        if (s_units[i].active)
            snprintf(unit_buf, sizeof(unit_buf), "%s  pos:%d lane:%d mom:%d",
                     role_name[i], s_units[i].pos, s_units[i].lane, s_units[i].momentum);
        else
            snprintf(unit_buf, sizeof(unit_buf), "%s  [cooldown %d]",
                     role_name[i], s_units[i].cooldown);
        DrawText(unit_buf, (int)row.x + pad, (int)row.y + (btn_h - base_fs)/2,
                 base_fs, s_units[i].active ? WHITE : GRAY);

        /* Click to select */
        Vector2 mp = GetMousePosition();
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mp, row) && s_units[i].active)
            s_selected = i;

        y += btn_h + 2;
    }
    y += pad;

    /* Controls for selected unit */
    if (s_selected >= 0 && s_selected < TEAM_SIZE && s_units[s_selected].active) {
        DrawText("Lane:", px + pad, y, base_fs, LIGHTGRAY);
        y += base_fs + 2;

        int bw = (pw - pad*2) / 3;
        const char *lane_labels[] = {"<", ".", ">"};
        int cur_dl = s_actions[s_selected].lane_delta + 1; /* -1→0,0→1,1→2 */
        for (int k = 0; k < 3; k++) {
            Rectangle br = {(float)(px + pad + k*bw), (float)y, (float)(bw-2), (float)(btn_h)};
            bool active_btn = (cur_dl == k);
            Color bcol = active_btn ? SKYBLUE : (Color){50,50,70,255};
            DrawRectangleRec(br, bcol);
            int lw = MeasureText(lane_labels[k], base_fs);
            DrawText(lane_labels[k],
                     (int)br.x + (bw - lw)/2, (int)br.y + (btn_h - base_fs)/2,
                     base_fs, WHITE);
            Vector2 mp2 = GetMousePosition();
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
                CheckCollisionPointRec(mp2, br))
                s_actions[s_selected].lane_delta = k - 1;
        }
        y += btn_h + pad;

        DrawText("Intent:", px + pad, y, base_fs, LIGHTGRAY);
        y += base_fs + 2;

        int ibw = (pw - pad*2) / 4;
        for (int k = 0; k < 4; k++) {
            Rectangle br = {(float)(px + pad + k*ibw), (float)y, (float)(ibw-2), (float)(btn_h)};
            bool sel_intent = (s_actions[s_selected].intent == (Intent)k);
            Color bcol = sel_intent ? GOLD : (Color){50,50,70,255};
            DrawRectangleRec(br, bcol);
            int iw = MeasureText(intent_name[k], base_fs - 2);
            DrawText(intent_name[k],
                     (int)br.x + (ibw - iw)/2, (int)br.y + (btn_h - (base_fs-2))/2,
                     base_fs - 2, WHITE);
            Vector2 mp3 = GetMousePosition();
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
                CheckCollisionPointRec(mp3, br))
                s_actions[s_selected].intent = (Intent)k;
        }
        y += btn_h + pad;
    }

    DrawLine(px, y, px + pw, y, DARKGRAY);
    y += pad;

    /* Confirm Turn */
    Rectangle confirm_btn = {(float)(px + pad), (float)y,
                              (float)(pw - pad*2), (float)(btn_h + 4)};
    GuiSetStyle(DEFAULT, TEXT_SIZE, base_fs);
    if (GuiButton(confirm_btn, "CONFIRM TURN")) {
        if (s_gs == GS_PLAYING) {
            run_ai_team1();
            resolve_turn();
            /* Check win */
            if (s_score[0] >= WIN_SCORE)
                s_gs = GS_GAME_OVER;
            else if (s_score[1] >= WIN_SCORE)
                s_gs = GS_GAME_OVER;
            /* Reset player actions */
            for (int i = 0; i < TEAM_SIZE; i++) {
                s_actions[i].lane_delta = 0;
                s_actions[i].intent     = INTENT_CRUISE;
            }
        }
    }
    y += btn_h + 4 + pad;

    /* Last event message */
    if (s_msg[0]) {
        DrawText(s_msg, px + pad, y, base_fs - 2, YELLOW);
        y += base_fs + pad;
    }

    (void)ph;
    (void)y;
}

/* --------------------------------------------------------- prototype interface */

static void RollerDerbyInit(void)
{
    memset(s_units,   0, sizeof(s_units));
    memset(s_actions, 0, sizeof(s_actions));
    s_score[0] = s_score[1] = 0;
    s_selected = 0;
    s_gs       = GS_PLAYING;
    s_msg[0]   = '\0';

    /* Team 0: jammer at pos 2, blockers at pos 5,6 */
    s_units[0] = (Unit){ .pos=2,  .lane=1, .momentum=2, .active=true };
    s_units[1] = (Unit){ .pos=5,  .lane=0, .momentum=2, .active=true };
    s_units[2] = (Unit){ .pos=6,  .lane=2, .momentum=2, .active=true };

    /* Team 1: jammer at pos 8, blockers at pos 11,0 */
    s_units[3] = (Unit){ .pos=8,  .lane=1, .momentum=2, .active=true };
    s_units[4] = (Unit){ .pos=11, .lane=0, .momentum=2, .active=true };
    s_units[5] = (Unit){ .pos=0,  .lane=2, .momentum=2, .active=true };
}

static void RollerDerbyUpdate(float dt)
{
    (void)dt;
}

static void RollerDerbyDraw(void)
{
    ClearBackground((Color){15, 15, 25, 255});

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    int track_w = (int)(sw * 0.65f);
    int panel_w = sw - track_w;

    /* Track view */
    float cx = track_w * 0.5f;
    float cy = sh * 0.5f;
    float r  = (track_w < sh ? track_w : sh) * 0.42f;

    draw_track(cx, cy, r);

    /* Legend */
    int fs = sh / 40;
    if (fs < 10) fs = 10;
    DrawCircleV((Vector2){(float)(track_w - 80), (float)(sh - 70)}, 8, BLUE);
    DrawText("P.Jammer", track_w - 65, sh - 75, fs - 2, WHITE);
    DrawCircleV((Vector2){(float)(track_w - 80), (float)(sh - 50)}, 8, SKYBLUE);
    DrawText("P.Blocker", track_w - 65, sh - 55, fs - 2, WHITE);
    DrawCircleV((Vector2){(float)(track_w - 80), (float)(sh - 30)}, 8, RED);
    DrawText("A.Jammer", track_w - 65, sh - 35, fs - 2, WHITE);

    /* Panel */
    DrawRectangle(track_w, 0, panel_w, sh, (Color){20, 20, 30, 255});
    draw_panel(track_w, 0, panel_w, sh);

    /* Game Over overlay */
    if (s_gs == GS_GAME_OVER) {
        DrawRectangle(0, 0, sw, sh, (Color){0, 0, 0, 160});
        const char *winner = (s_score[0] >= WIN_SCORE) ? "TEAM 0 WINS!" : "TEAM 1 WINS!";
        int big_fs = sw / 10;
        int tw     = MeasureText(winner, big_fs);
        DrawText(winner, (sw - tw)/2, sh/2 - big_fs - 10, big_fs, GOLD);

        GuiSetStyle(DEFAULT, TEXT_SIZE, big_fs / 3);
        Rectangle rb = {(float)((sw - 200)/2), (float)(sh/2 + 20), 200, 50};
        if (GuiButton(rb, "PLAY AGAIN"))
            RollerDerbyInit();
    }
}

static void RollerDerbyDeinit(void)
{
    /* nothing to free */
}

const Prototype RollerDerbyProto = {
    .name        = "Roller Derby",
    .description = "Turn-based roller derby sim on a circular track.",
    .Init        = RollerDerbyInit,
    .Update      = RollerDerbyUpdate,
    .Draw        = RollerDerbyDraw,
    .Deinit      = RollerDerbyDeinit,
};
