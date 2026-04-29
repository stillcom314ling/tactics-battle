#include "pool.h"
#include "raylib.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_BALLS         16
#define POCKET_COUNT       6
#define RACK_BALLS        10  /* cue + 9 object balls */

#define BALL_R_FRAC      0.018f   /* fraction of long table side */
#define POCKET_R_FRAC    0.032f
#define CUSHION_FRAC     0.045f
#define TABLE_PAD_FRAC   0.04f    /* outer margin around the cushions */

#define FRICTION_DECEL   240.0f   /* px/s² rolling deceleration */
#define MIN_SPEED          6.0f
#define WALL_RESTITUTION   0.92f
#define BALL_RESTITUTION   0.96f

#define MAX_SHOT_SPEED   1800.0f
#define POWER_GAIN          3.5f  /* drag-pixel → velocity-px/s */
#define AIM_MIN_DRAG        4.0f

typedef struct {
    Vector2 pos, vel;
    Color   color;
    bool    active;
    bool    is_cue;
} Ball;

typedef struct {
    Rectangle outer;       /* table including cushions */
    Rectangle felt;        /* play area inside cushions */
    Vector2   pocket[POCKET_COUNT];
    float     ball_r;
    float     pocket_r;
    float     cushion_w;
} Table;

static Ball    s_balls[MAX_BALLS];
static Table   s_table;
static int     s_cue_idx;

/* aim state */
static bool    s_aiming;
static Vector2 s_aim_pos;       /* current pointer location while aiming */

/* input edge detection (mouse + touch unified) */
static bool    s_prev_pressed;
static Vector2 s_prev_pointer;

/* ------------------------------------------------------------------ helpers */

static float v_len(Vector2 v) { return sqrtf(v.x*v.x + v.y*v.y); }

static Vector2 v_scale(Vector2 v, float s) { return (Vector2){ v.x*s, v.y*s }; }

static Vector2 v_sub(Vector2 a, Vector2 b) { return (Vector2){ a.x-b.x, a.y-b.y }; }

static Vector2 v_add(Vector2 a, Vector2 b) { return (Vector2){ a.x+b.x, a.y+b.y }; }

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void compute_table(Table *t)
{
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();

    bool landscape = sw >= sh;
    float long_side  = landscape ? sw : sh;
    float short_side = landscape ? sh : sw;

    float pad = long_side * TABLE_PAD_FRAC;

    /* table aspect 2:1; fit inside (sw,sh) leaving pad */
    float avail_long  = long_side  - 2.0f * pad;
    float avail_short = short_side - 2.0f * pad;
    float table_long  = avail_long;
    float table_short = table_long * 0.5f;
    if (table_short > avail_short) {
        table_short = avail_short;
        table_long  = table_short * 2.0f;
    }

    float ox, oy, ow, oh;
    if (landscape) {
        ow = table_long; oh = table_short;
    } else {
        ow = table_short; oh = table_long;
    }
    ox = (sw - ow) * 0.5f;
    oy = (sh - oh) * 0.5f;

    t->outer = (Rectangle){ ox, oy, ow, oh };

    t->cushion_w = long_side * CUSHION_FRAC;
    t->felt = (Rectangle){
        ox + t->cushion_w, oy + t->cushion_w,
        ow - 2.0f * t->cushion_w, oh - 2.0f * t->cushion_w,
    };

    t->ball_r   = long_side * BALL_R_FRAC;
    t->pocket_r = long_side * POCKET_R_FRAC;

    /* pockets in cushion corners and middles of long sides */
    float fx0 = t->felt.x, fy0 = t->felt.y;
    float fx1 = t->felt.x + t->felt.width;
    float fy1 = t->felt.y + t->felt.height;

    if (landscape) {
        t->pocket[0] = (Vector2){ fx0,             fy0 };
        t->pocket[1] = (Vector2){ (fx0 + fx1)*0.5f, fy0 };
        t->pocket[2] = (Vector2){ fx1,             fy0 };
        t->pocket[3] = (Vector2){ fx0,             fy1 };
        t->pocket[4] = (Vector2){ (fx0 + fx1)*0.5f, fy1 };
        t->pocket[5] = (Vector2){ fx1,             fy1 };
    } else {
        t->pocket[0] = (Vector2){ fx0, fy0 };
        t->pocket[1] = (Vector2){ fx1, fy0 };
        t->pocket[2] = (Vector2){ fx0, (fy0 + fy1)*0.5f };
        t->pocket[3] = (Vector2){ fx1, (fy0 + fy1)*0.5f };
        t->pocket[4] = (Vector2){ fx0, fy1 };
        t->pocket[5] = (Vector2){ fx1, fy1 };
    }
}

static Color object_ball_color(int n)
{
    static const Color pal[] = {
        GOLD, BLUE, RED, PURPLE, ORANGE,
        DARKGREEN, MAROON, BLACK, SKYBLUE,
    };
    return pal[n % (int)(sizeof(pal)/sizeof(pal[0]))];
}

static Vector2 random_kitchen_pos(void)
{
    /* place cue ball somewhere in the lower/short half of the felt */
    float r = s_table.ball_r * 1.2f;
    bool landscape = s_table.outer.width >= s_table.outer.height;

    float x_lo, x_hi, y_lo, y_hi;
    if (landscape) {
        /* left quarter of the felt */
        x_lo = s_table.felt.x + r;
        x_hi = s_table.felt.x + s_table.felt.width * 0.25f;
        y_lo = s_table.felt.y + r;
        y_hi = s_table.felt.y + s_table.felt.height - r;
    } else {
        x_lo = s_table.felt.x + r;
        x_hi = s_table.felt.x + s_table.felt.width  - r;
        y_lo = s_table.felt.y + s_table.felt.height * 0.75f;
        y_hi = s_table.felt.y + s_table.felt.height - r;
    }

    /* try a few times to avoid overlap with active object balls */
    for (int attempt = 0; attempt < 32; attempt++) {
        float x = (float)GetRandomValue((int)x_lo, (int)x_hi);
        float y = (float)GetRandomValue((int)y_lo, (int)y_hi);
        Vector2 p = { x, y };
        bool ok = true;
        for (int i = 0; i < MAX_BALLS; i++) {
            if (!s_balls[i].active || s_balls[i].is_cue) continue;
            Vector2 d = v_sub(s_balls[i].pos, p);
            if (v_len(d) < s_table.ball_r * 2.2f) { ok = false; break; }
        }
        if (ok) return p;
    }
    /* fallback: just return centre of kitchen */
    return (Vector2){ (x_lo + x_hi)*0.5f, (y_lo + y_hi)*0.5f };
}

static void rack_balls(void)
{
    for (int i = 0; i < MAX_BALLS; i++) s_balls[i].active = false;

    bool landscape = s_table.outer.width >= s_table.outer.height;
    float r = s_table.ball_r;

    /* foot spot: 3/4 along the long axis, on the centre line */
    float fx, fy;
    if (landscape) {
        fx = s_table.felt.x + s_table.felt.width * 0.75f;
        fy = s_table.felt.y + s_table.felt.height * 0.5f;
    } else {
        fx = s_table.felt.x + s_table.felt.width  * 0.5f;
        fy = s_table.felt.y + s_table.felt.height * 0.25f;
    }

    /* triangular rack of 9 balls along the long axis pointing toward kitchen */
    int idx = 1;  /* cue is index 0 */
    int rack_n = 0;
    int rows = 4;  /* 1+2+3+ part of 4 = 9 -- last row has 3 */
    int per_row[4] = { 1, 2, 3, 3 };
    float row_step = r * 1.732f; /* sqrt(3) */
    float ball_step = r * 2.0f;

    for (int row = 0; row < rows && rack_n < 9; row++) {
        for (int j = 0; j < per_row[row] && rack_n < 9; j++) {
            float along = (float)row * row_step;        /* deeper into rack */
            float across = ((float)j - (per_row[row] - 1) * 0.5f) * ball_step;

            Vector2 p;
            if (landscape) {
                /* rack opens toward -x (toward kitchen on left) */
                p = (Vector2){ fx + along, fy + across };
            } else {
                /* rack opens toward +y (kitchen on bottom) */
                p = (Vector2){ fx + across, fy - along };
            }

            s_balls[idx] = (Ball){
                .pos    = p,
                .vel    = { 0, 0 },
                .color  = object_ball_color(rack_n),
                .active = true,
                .is_cue = false,
            };
            idx++;
            rack_n++;
        }
    }

    /* cue ball at kitchen */
    s_cue_idx = 0;
    s_balls[0] = (Ball){
        .pos    = random_kitchen_pos(),
        .vel    = { 0, 0 },
        .color  = RAYWHITE,
        .active = true,
        .is_cue = true,
    };
}

static bool all_balls_at_rest(void)
{
    for (int i = 0; i < MAX_BALLS; i++) {
        if (!s_balls[i].active) continue;
        if (v_len(s_balls[i].vel) > 0.01f) return false;
    }
    return true;
}

/* ------------------------------------------------------- collision response */

static void resolve_ball_pair(Ball *a, Ball *b, float r)
{
    Vector2 d = v_sub(b->pos, a->pos);
    float dist = v_len(d);
    float min_dist = 2.0f * r;
    if (dist <= 0.0001f) {
        /* perfectly overlapping — separate along an arbitrary axis */
        d = (Vector2){ 1.0f, 0.0f };
        dist = 0.0001f;
    }
    if (dist >= min_dist) return;

    Vector2 n = v_scale(d, 1.0f / dist);

    /* positional correction */
    float overlap = (min_dist - dist);
    Vector2 corr = v_scale(n, overlap * 0.5f);
    a->pos = v_sub(a->pos, corr);
    b->pos = v_add(b->pos, corr);

    /* relative velocity along normal */
    Vector2 rv = v_sub(b->vel, a->vel);
    float rv_n = rv.x * n.x + rv.y * n.y;
    if (rv_n >= 0) return;  /* separating — nothing to do */

    float j = -(1.0f + BALL_RESTITUTION) * rv_n * 0.5f;  /* equal mass */
    Vector2 imp = v_scale(n, j);
    a->vel = v_sub(a->vel, imp);
    b->vel = v_add(b->vel, imp);
}

static void resolve_cushion(Ball *bl, float r)
{
    float left   = s_table.felt.x + r;
    float right  = s_table.felt.x + s_table.felt.width  - r;
    float top    = s_table.felt.y + r;
    float bottom = s_table.felt.y + s_table.felt.height - r;

    if (bl->pos.x < left)   { bl->pos.x = left;   bl->vel.x =  fabsf(bl->vel.x) * WALL_RESTITUTION; }
    if (bl->pos.x > right)  { bl->pos.x = right;  bl->vel.x = -fabsf(bl->vel.x) * WALL_RESTITUTION; }
    if (bl->pos.y < top)    { bl->pos.y = top;    bl->vel.y =  fabsf(bl->vel.y) * WALL_RESTITUTION; }
    if (bl->pos.y > bottom) { bl->pos.y = bottom; bl->vel.y = -fabsf(bl->vel.y) * WALL_RESTITUTION; }
}

static void check_pockets(void)
{
    float pr = s_table.pocket_r;
    for (int i = 0; i < MAX_BALLS; i++) {
        if (!s_balls[i].active) continue;
        for (int p = 0; p < POCKET_COUNT; p++) {
            Vector2 d = v_sub(s_balls[i].pos, s_table.pocket[p]);
            if (v_len(d) < pr) {
                if (s_balls[i].is_cue) {
                    /* respawn cue at random kitchen position */
                    s_balls[i].pos = random_kitchen_pos();
                    s_balls[i].vel = (Vector2){ 0, 0 };
                } else {
                    s_balls[i].active = false;
                }
                break;
            }
        }
    }
}

static void integrate(float dt)
{
    /* substep to avoid tunneling at high speeds */
    float r = s_table.ball_r;
    float max_speed = 0.0f;
    for (int i = 0; i < MAX_BALLS; i++) {
        if (!s_balls[i].active) continue;
        float s = v_len(s_balls[i].vel);
        if (s > max_speed) max_speed = s;
    }

    int steps = 1;
    if (max_speed > 0.0f) {
        float max_step = r * 0.5f;
        steps = (int)ceilf((max_speed * dt) / max_step);
        if (steps < 1) steps = 1;
        if (steps > 8) steps = 8;
    }
    float sdt = dt / (float)steps;

    for (int s = 0; s < steps; s++) {
        for (int i = 0; i < MAX_BALLS; i++) {
            if (!s_balls[i].active) continue;
            s_balls[i].pos = v_add(s_balls[i].pos, v_scale(s_balls[i].vel, sdt));
        }
        for (int i = 0; i < MAX_BALLS; i++) {
            if (!s_balls[i].active) continue;
            for (int j = i + 1; j < MAX_BALLS; j++) {
                if (!s_balls[j].active) continue;
                resolve_ball_pair(&s_balls[i], &s_balls[j], r);
            }
        }
        for (int i = 0; i < MAX_BALLS; i++) {
            if (!s_balls[i].active) continue;
            resolve_cushion(&s_balls[i], r);
        }
        check_pockets();
    }

    /* friction + stop threshold (applied once per frame, not per substep) */
    for (int i = 0; i < MAX_BALLS; i++) {
        if (!s_balls[i].active) continue;
        float speed = v_len(s_balls[i].vel);
        if (speed <= MIN_SPEED) {
            s_balls[i].vel = (Vector2){ 0, 0 };
            continue;
        }
        float new_speed = speed - FRICTION_DECEL * dt;
        if (new_speed < 0) new_speed = 0;
        s_balls[i].vel = v_scale(s_balls[i].vel, new_speed / speed);
    }
}

/* ------------------------------------------------------------ unified input */

static bool poll_pointer(Vector2 *out)
{
    if (GetTouchPointCount() > 0) {
        *out = GetTouchPosition(0);
        return true;
    }
    Vector2 m = GetMousePosition();
    *out = m;
    return IsMouseButtonDown(MOUSE_BUTTON_LEFT);
}

static void handle_input(void)
{
    Vector2 ptr;
    bool pressed = poll_pointer(&ptr);

    Ball *cue = &s_balls[s_cue_idx];

    if (!s_prev_pressed && pressed) {
        /* press */
        if (cue->active && all_balls_at_rest()) {
            s_aiming = true;
            s_aim_pos = ptr;
        }
    } else if (s_prev_pressed && pressed) {
        /* drag */
        if (s_aiming) s_aim_pos = ptr;
    } else if (s_prev_pressed && !pressed) {
        /* release */
        if (s_aiming && cue->active) {
            Vector2 d = v_sub(s_aim_pos, cue->pos);
            float dist = v_len(d);
            if (dist > AIM_MIN_DRAG) {
                float speed = clampf(dist * POWER_GAIN, 0, MAX_SHOT_SPEED);
                Vector2 dir = v_scale(d, 1.0f / dist);
                cue->vel = v_scale(dir, speed);
            }
        }
        s_aiming = false;
    }

    s_prev_pressed = pressed;
    s_prev_pointer = ptr;
}

/* ------------------------------------------------------------- prototype impl */

static void PoolInit(void)
{
    compute_table(&s_table);
    rack_balls();
    s_aiming = false;
    s_prev_pressed = false;
}

static void PoolUpdate(float dt)
{
    /* recompute table each frame so the game adapts to window resizes;
       ball positions remain in absolute pixels and are kept on the felt
       by the cushion clamp inside integrate(). */
    compute_table(&s_table);

    handle_input();
    integrate(dt);

    if (IsKeyPressed(KEY_R)) {
        rack_balls();
        s_aiming = false;
    }
}

static void PoolDraw(void)
{
    ClearBackground((Color){ 18, 50, 30, 255 });

    /* cushion frame */
    DrawRectangleRec(s_table.outer, (Color){ 80, 45, 20, 255 });
    /* felt */
    DrawRectangleRec(s_table.felt, (Color){ 30, 110, 60, 255 });

    /* pockets */
    for (int p = 0; p < POCKET_COUNT; p++) {
        DrawCircleV(s_table.pocket[p], s_table.pocket_r, BLACK);
    }

    /* aim line */
    Ball *cue = &s_balls[s_cue_idx];
    if (s_aiming && cue->active) {
        Vector2 d = v_sub(s_aim_pos, cue->pos);
        float dist = v_len(d);
        if (dist > AIM_MIN_DRAG) {
            float power_t = clampf((dist * POWER_GAIN) / MAX_SHOT_SPEED, 0.0f, 1.0f);
            Color line_col = (Color){
                (unsigned char)(255 * power_t + 200 * (1 - power_t)),
                (unsigned char)(80  * (1 - power_t) + 80 * power_t),
                (unsigned char)(80  * (1 - power_t)),
                220
            };
            DrawLineEx(cue->pos, s_aim_pos, 3.0f, line_col);
            /* power bar near cue ball */
            float bar_len = s_table.ball_r * 4.0f;
            Rectangle bg = {
                cue->pos.x - bar_len * 0.5f,
                cue->pos.y - s_table.ball_r * 2.5f,
                bar_len, 6.0f
            };
            DrawRectangleRec(bg, (Color){0, 0, 0, 120});
            Rectangle fg = bg;
            fg.width = bar_len * power_t;
            DrawRectangleRec(fg, line_col);
        }
    }

    /* balls */
    for (int i = 0; i < MAX_BALLS; i++) {
        if (!s_balls[i].active) continue;
        DrawCircleV(s_balls[i].pos, s_table.ball_r, s_balls[i].color);
        DrawCircleLinesV(s_balls[i].pos, s_table.ball_r,
                         ColorBrightness(s_balls[i].color, -0.4f));
    }

    int fs = GetScreenWidth() / 50;
    if (fs < 14) fs = 14;
    DrawText("Drag from cue ball to aim & set power. Release to shoot.  R: rerack",
             10, 10, fs, RAYWHITE);
}

static void PoolDeinit(void)
{
    /* nothing to free */
}

const Prototype PoolProto = {
    .name        = "Pool",
    .description = "Top-down billiards: drag to aim and shoot the cue ball.",
    .Init        = PoolInit,
    .Update      = PoolUpdate,
    .Draw        = PoolDraw,
    .Deinit      = PoolDeinit,
};
