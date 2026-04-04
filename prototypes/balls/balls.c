#include "balls.h"
#include "raylib.h"
#include <math.h>

#define MAX_BALLS       64
#define MAX_TOUCHES     10
#define DRAG_THRESHOLD   8.0f
#define DOUBLE_TAP_SECS  0.35
#define GRAVITY        900.0f

typedef struct {
    Vector2 pos, vel;
    float   radius;
    Color   color;
    bool    active;
} Ball;

typedef struct {
    int     id;
    bool    active;
    bool    dragging;
    int     ball_idx;   /* -1 = not on a ball */
    Vector2 start_pos;
    Vector2 cur_pos;
    double  down_time;
} TouchSlot;

static Ball      s_balls[MAX_BALLS];
static TouchSlot s_slots[MAX_TOUCHES];
static int       s_prev_ids[MAX_TOUCHES];
static int       s_prev_count = 0;

/* double-tap state */
static int    s_dt_ball = -1;
static double s_dt_time = -1.0;

/* ----------------------------------------------------------------- helpers */

static Color rand_color(void)
{
    static Color pal[] = {
        MAROON, DARKBLUE, DARKGREEN, PURPLE, ORANGE,
        RED, BLUE, GREEN, VIOLET, GOLD, PINK, SKYBLUE
    };
    return pal[GetRandomValue(0, 11)];
}

static int ball_at(Vector2 p)
{
    for (int i = 0; i < MAX_BALLS; i++) {
        if (!s_balls[i].active) continue;
        float dx = s_balls[i].pos.x - p.x;
        float dy = s_balls[i].pos.y - p.y;
        if (dx*dx + dy*dy <= s_balls[i].radius * s_balls[i].radius) return i;
    }
    return -1;
}

static void add_ball(Vector2 pos)
{
    for (int i = 0; i < MAX_BALLS; i++) {
        if (s_balls[i].active) continue;
        float r = (float)GetRandomValue(28, 60);
        s_balls[i] = (Ball){
            .pos    = pos,
            .vel    = { (float)GetRandomValue(-200, 200), (float)GetRandomValue(-100, 0) },
            .radius = r,
            .color  = rand_color(),
            .active = true,
        };
        return;
    }
}

static TouchSlot *find_slot(int id)
{
    for (int i = 0; i < MAX_TOUCHES; i++)
        if (s_slots[i].active && s_slots[i].id == id) return &s_slots[i];
    return NULL;
}

/* ------------------------------------------------------------------ events */

static void on_down(int id, Vector2 pos)
{
    for (int i = 0; i < MAX_TOUCHES; i++) {
        if (s_slots[i].active) continue;
        s_slots[i] = (TouchSlot){
            .id        = id,
            .active    = true,
            .dragging  = false,
            .ball_idx  = ball_at(pos),
            .start_pos = pos,
            .cur_pos   = pos,
            .down_time = GetTime(),
        };
        return;
    }
}

static void on_move(int id, Vector2 pos)
{
    TouchSlot *s = find_slot(id);
    if (!s) return;
    float dx = pos.x - s->start_pos.x;
    float dy = pos.y - s->start_pos.y;
    if (!s->dragging && sqrtf(dx*dx + dy*dy) > DRAG_THRESHOLD)
        s->dragging = true;
    s->cur_pos = pos;
}

static void on_up(int id)
{
    TouchSlot *s = find_slot(id);
    if (!s) return;

    if (!s->dragging) {
        int b = ball_at(s->cur_pos);
        if (b >= 0) {
            double now = GetTime();
            if (s_dt_ball == b && (now - s_dt_time) < DOUBLE_TAP_SECS) {
                s_balls[b].active = false;
                s_dt_ball = -1;
            } else {
                s_dt_ball = b;
                s_dt_time = now;
            }
        } else {
            add_ball(s->cur_pos);
            s_dt_ball = -1;
        }
    }

    s->active = false;
}

/* ---------------------------------------------------------- prototype impl */

static void BallsInit(void)
{
    for (int i = 0; i < MAX_BALLS; i++) s_balls[i].active = false;
    for (int i = 0; i < MAX_TOUCHES; i++) s_slots[i].active = false;
    s_prev_count = 0;
    s_dt_ball = -1;
    s_dt_time = -1.0;

    int w = GetScreenWidth();
    int h = GetScreenHeight();
    add_ball((Vector2){ w / 2.0f, h / 3.0f });
}

static void BallsUpdate(float dt)
{
    int w = GetScreenWidth();
    int h = GetScreenHeight();

    /* touch input */
    int cur_count = GetTouchPointCount();
    int     cur_ids[MAX_TOUCHES];
    Vector2 cur_pos[MAX_TOUCHES];
    for (int i = 0; i < cur_count && i < MAX_TOUCHES; i++) {
        cur_ids[i] = GetTouchPointId(i);
        cur_pos[i] = GetTouchPosition(i);
    }

    /* releases: was in prev, gone now */
    for (int p = 0; p < s_prev_count; p++) {
        bool found = false;
        for (int c = 0; c < cur_count; c++)
            if (cur_ids[c] == s_prev_ids[p]) { found = true; break; }
        if (!found) on_up(s_prev_ids[p]);
    }
    /* new & moved */
    for (int c = 0; c < cur_count && c < MAX_TOUCHES; c++) {
        if (!find_slot(cur_ids[c])) on_down(cur_ids[c], cur_pos[c]);
        else                        on_move(cur_ids[c], cur_pos[c]);
    }
    s_prev_count = cur_count;
    for (int i = 0; i < cur_count && i < MAX_TOUCHES; i++)
        s_prev_ids[i] = cur_ids[i];

    /* mouse fallback (desktop) */
    if (cur_count == 0) {
        Vector2 mp = GetMousePosition();
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))      on_down(0, mp);
        else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))    on_move(0, mp);
        else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) on_up(0);
    }

    /* physics */
    for (int i = 0; i < MAX_BALLS; i++) {
        if (!s_balls[i].active) continue;

        TouchSlot *dragger = NULL;
        for (int t = 0; t < MAX_TOUCHES; t++) {
            if (s_slots[t].active && s_slots[t].ball_idx == i && s_slots[t].dragging) {
                dragger = &s_slots[t];
                break;
            }
        }
        if (!dragger) {
            TouchSlot *ms = find_slot(0);
            if (ms && ms->ball_idx == i && ms->dragging) dragger = ms;
        }

        if (dragger) {
            Vector2 prev = s_balls[i].pos;
            s_balls[i].pos = dragger->cur_pos;
            if (dt > 0.0f) {
                s_balls[i].vel.x = (s_balls[i].pos.x - prev.x) / dt;
                s_balls[i].vel.y = (s_balls[i].pos.y - prev.y) / dt;
            }
        } else {
            s_balls[i].vel.y += GRAVITY * dt;
            s_balls[i].pos.x += s_balls[i].vel.x * dt;
            s_balls[i].pos.y += s_balls[i].vel.y * dt;

            float r = s_balls[i].radius;
            if (s_balls[i].pos.x < r)   { s_balls[i].pos.x = r;   s_balls[i].vel.x =  fabsf(s_balls[i].vel.x) * 0.75f; }
            if (s_balls[i].pos.x > w-r) { s_balls[i].pos.x = w-r; s_balls[i].vel.x = -fabsf(s_balls[i].vel.x) * 0.75f; }
            if (s_balls[i].pos.y < r)   { s_balls[i].pos.y = r;   s_balls[i].vel.y =  fabsf(s_balls[i].vel.y) * 0.75f; }
            if (s_balls[i].pos.y > h-r) { s_balls[i].pos.y = h-r; s_balls[i].vel.y = -fabsf(s_balls[i].vel.y) * 0.75f; }
        }
    }
}

static void BallsDraw(void)
{
    ClearBackground(RAYWHITE);

    for (int i = 0; i < MAX_BALLS; i++) {
        if (!s_balls[i].active) continue;
        DrawCircleV(s_balls[i].pos, s_balls[i].radius, s_balls[i].color);
        DrawCircleLinesV(s_balls[i].pos, s_balls[i].radius,
                         ColorBrightness(s_balls[i].color, -0.3f));
    }

    int font_size = GetScreenWidth() / 36;
    DrawText("Tap: add  |  Drag: move  |  Double-tap: delete  |  ESC: menu",
             10, 10, font_size, DARKGRAY);
}

static void BallsDeinit(void)
{
    /* nothing to free */
}

const Prototype BallsProto = {
    .name        = "Balls",
    .description = "Physics demo: tap to spawn, drag to throw, double-tap to delete.",
    .Init        = BallsInit,
    .Update      = BallsUpdate,
    .Draw        = BallsDraw,
    .Deinit      = BallsDeinit,
};
