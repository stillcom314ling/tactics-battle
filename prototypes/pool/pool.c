#include "pool.h"
#include "raylib.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* -----------------------------------------------------------------------------
 * Pool / Billiards prototype.
 *
 * Physics modeled after Evan Kiefl's pooltool theory (ekiefl.github.io). Each
 * ball carries position, velocity, and a full 3-component angular velocity
 * (wx, wy = in-plane rolling axes; wz = vertical "English" axis). Per frame a
 * ball is in one of three motion phases:
 *
 *   SLIDING — the contact-point slip velocity u = v + R*(z_hat x w_xy) is
 *             non-zero. Friction force F = -mu_s * m * g * u_hat acts at the
 *             contact, producing both linear deceleration and an angular
 *             acceleration that drives the ball toward the rolling condition.
 *   ROLLING — when |u| reaches ~0, the ball rolls without slipping. Only
 *             rolling friction (mu_r) decelerates v, and w_xy is locked to v.
 *   SPINNING — wz decays independently via spinning friction (mu_sp).
 *
 * Ball-ball collision: Han-style instantaneous frictionless impulse along the
 * line of centers, equal mass -> swap normal velocity components scaled by a
 * coefficient of restitution.
 *
 * Cushion: reverse component normal to the rail with restitution e_c. Vertical
 * spin (wz) imparts a small tangential kick on rebound (English carry).
 *
 * Input: a single touch/drag anywhere on screen aims a pull-back shot from the
 * cue ball. Drag length < DEAD_ZONE cancels the shot.
 * -------------------------------------------------------------------------- */

#define MAX_BALLS          16
#define NUM_POCKETS         6
#define MAX_TOUCHES        10

/* physics tuning — units are pixels and seconds. G_EFF is the scaled "gravity"
 * that translates the dimensionless friction coefficients into px/s^2. */
#define G_EFF            3000.0f
#define MU_SLIDE            0.20f
#define MU_ROLL             0.012f
#define MU_SPIN             0.044f
#define BALL_M              1.0f      /* mass cancels for equal balls */

/* numerical tolerances */
#define EPS_SLIP            2.0f      /* px/s at contact -> rolling */
#define EPS_VEL             1.5f      /* px/s linear -> stop */
#define EPS_SPIN            0.2f      /* rad/s vertical -> stop */

#define E_BALL              0.96f     /* ball-ball restitution */
#define E_CUSHION           0.78f     /* cushion restitution */
#define CUSH_SPIN_KICK      0.18f     /* fraction of R*wz added tangentially */
#define SUBSTEPS            4

/* input */
#define DEAD_ZONE_FRAC      0.025f    /* fraction of min screen dim */
#define MAX_PULL_FRAC       0.35f
#define MAX_SHOT_SPEED   2400.0f      /* px/s at full pull */

typedef struct {
    Vector2 pos;
    Vector2 vel;
    float   wx, wy, wz;  /* angular velocity, rad/s */
    bool    active;
    bool    is_cue;
    int     number;      /* 1..15, 0 for cue */
    Color   color;
    bool    striped;
} Ball;

typedef struct {
    Vector2 pos;
    float   radius;
} Pocket;

typedef enum {
    GS_AIM,
    GS_MOVING,
} GameState;

/* -------- module state ---------------------------------------------------- */

static Ball       s_balls[MAX_BALLS];
static Pocket     s_pockets[NUM_POCKETS];
static Rectangle  s_play;        /* playable felt area (inside cushions) */
static float      s_R;           /* ball radius (px) */
static float      s_dead_zone;
static float      s_max_pull;
static Vector2    s_cue_spot;    /* respot for the cue ball */
static GameState  s_state;
static int        s_pocketed_count;

/* input — one active drag at a time (first finger or mouse) */
static bool    s_drag_active;
static int     s_drag_touch_id;   /* -1 = mouse */
static Vector2 s_drag_start;
static Vector2 s_drag_cur;
static int     s_prev_touch_ids[MAX_TOUCHES];
static int     s_prev_touch_count;

/* visual flair */
static float   s_shot_flash;

/* -------- helpers --------------------------------------------------------- */

static Color ball_palette(int n)
{
    /* solids 1..7, 8 black, stripes 9..15 share solid colors */
    static const Color tbl[16] = {
        WHITE,                          /* 0 cue */
        (Color){240, 200,  40, 255},    /* 1 yellow */
        (Color){ 40,  90, 200, 255},    /* 2 blue */
        (Color){210,  50,  40, 255},    /* 3 red */
        (Color){120,  40, 160, 255},    /* 4 purple */
        (Color){230, 120,  30, 255},    /* 5 orange */
        (Color){ 30, 130,  70, 255},    /* 6 green */
        (Color){140,  40,  40, 255},    /* 7 maroon */
        (Color){ 20,  20,  20, 255},    /* 8 black */
        (Color){240, 200,  40, 255},    /* 9 yellow stripe */
        (Color){ 40,  90, 200, 255},    /* 10 blue stripe */
        (Color){210,  50,  40, 255},    /* 11 red stripe */
        (Color){120,  40, 160, 255},    /* 12 purple stripe */
        (Color){230, 120,  30, 255},    /* 13 orange stripe */
        (Color){ 30, 130,  70, 255},    /* 14 green stripe */
        (Color){140,  40,  40, 255},    /* 15 maroon stripe */
    };
    if (n < 0 || n > 15) return WHITE;
    return tbl[n];
}

static float v2_len(Vector2 v) { return sqrtf(v.x*v.x + v.y*v.y); }

static Vector2 v2_norm(Vector2 v)
{
    float l = v2_len(v);
    if (l < 1e-6f) return (Vector2){0, 0};
    return (Vector2){ v.x / l, v.y / l };
}

static Vector2 v2_add(Vector2 a, Vector2 b) { return (Vector2){a.x+b.x, a.y+b.y}; }
static Vector2 v2_sub(Vector2 a, Vector2 b) { return (Vector2){a.x-b.x, a.y-b.y}; }
static Vector2 v2_scale(Vector2 a, float s) { return (Vector2){a.x*s, a.y*s}; }
static float   v2_dot(Vector2 a, Vector2 b) { return a.x*b.x + a.y*b.y; }

/* slip velocity at the ball-cloth contact point.
 *   u = v + (-R) * (z_hat x w)     (contact is at -R*z_hat below center)
 *   With w_xy = (wx, wy), z_hat x w_xy = (-wy, wx), so
 *   u = (vx - R*wy, vy + R*wx)
 */
static Vector2 contact_slip(const Ball *b)
{
    return (Vector2){
        b->vel.x - s_R * b->wy,
        b->vel.y + s_R * b->wx,
    };
}

/* -------- table layout ---------------------------------------------------- */

static void layout_table(void)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    bool portrait = (sh >= sw);

    /* outer margin for cushions + UI */
    float top_ui = (float)(sh / 12);    /* room for hint text at top */
    float bot_ui = (float)(sh / 14);    /* room for back button etc */
    float side = (float)(sw / 22);

    float avail_w = sw - 2*side;
    float avail_h = sh - top_ui - bot_ui;

    float tw, th;
    if (portrait) {
        /* table long axis vertical, ratio 1:2 */
        th = avail_h;
        tw = th * 0.5f;
        if (tw > avail_w) { tw = avail_w; th = tw * 2.0f; }
    } else {
        tw = avail_w;
        th = tw * 0.5f;
        if (th > avail_h) { th = avail_h; tw = th * 2.0f; }
    }

    s_play.x = (sw - tw) * 0.5f;
    s_play.y = top_ui + (avail_h - th) * 0.5f;
    s_play.width  = tw;
    s_play.height = th;

    /* scale gameplay-critical sizes off the short table edge */
    float short_edge = (tw < th) ? tw : th;
    s_R = short_edge / 24.0f;            /* ~22 ball-diameters across short axis */

    float pr = s_R * 1.85f;
    s_pockets[0] = (Pocket){ {s_play.x,            s_play.y            }, pr };
    s_pockets[1] = (Pocket){ {s_play.x + tw,       s_play.y            }, pr };
    s_pockets[2] = (Pocket){ {s_play.x,            s_play.y + th       }, pr };
    s_pockets[3] = (Pocket){ {s_play.x + tw,       s_play.y + th       }, pr };
    if (portrait) {
        s_pockets[4] = (Pocket){ {s_play.x,        s_play.y + th*0.5f  }, pr*0.92f };
        s_pockets[5] = (Pocket){ {s_play.x + tw,   s_play.y + th*0.5f  }, pr*0.92f };
    } else {
        s_pockets[4] = (Pocket){ {s_play.x + tw*0.5f, s_play.y         }, pr*0.92f };
        s_pockets[5] = (Pocket){ {s_play.x + tw*0.5f, s_play.y + th    }, pr*0.92f };
    }

    /* head spot at 3/4 length from foot (foot = top in portrait, left in landscape) */
    if (portrait) {
        s_cue_spot = (Vector2){ s_play.x + tw*0.5f, s_play.y + th*0.75f };
    } else {
        s_cue_spot = (Vector2){ s_play.x + tw*0.25f, s_play.y + th*0.5f };
    }

    float min_dim = (sw < sh) ? sw : sh;
    s_dead_zone = min_dim * DEAD_ZONE_FRAC;
    s_max_pull  = min_dim * MAX_PULL_FRAC;
}

/* -------- racking --------------------------------------------------------- */

static void place_ball(int slot, Vector2 pos, int number, bool is_cue)
{
    s_balls[slot] = (Ball){
        .pos     = pos,
        .vel     = (Vector2){0, 0},
        .wx = 0, .wy = 0, .wz = 0,
        .active  = true,
        .is_cue  = is_cue,
        .number  = number,
        .color   = ball_palette(number),
        .striped = (number >= 9 && number <= 15),
    };
}

static void rack_balls(void)
{
    for (int i = 0; i < MAX_BALLS; i++) s_balls[i].active = false;

    /* rack apex = foot spot at 1/4 length from foot rail */
    bool portrait = (s_play.height >= s_play.width);
    Vector2 apex;
    Vector2 row_step;   /* between rows (toward foot rail) */
    Vector2 col_step;   /* between balls in a row */
    if (portrait) {
        apex = (Vector2){ s_play.x + s_play.width*0.5f, s_play.y + s_play.height*0.25f };
        row_step = (Vector2){ 0.0f, -s_R * 1.7320508f };  /* sqrt(3) */
        col_step = (Vector2){ s_R, 0.0f };
    } else {
        apex = (Vector2){ s_play.x + s_play.width*0.75f, s_play.y + s_play.height*0.5f };
        row_step = (Vector2){ s_R * 1.7320508f, 0.0f };
        col_step = (Vector2){ 0.0f, s_R };
    }

    /* number assignment: 1 at apex, 8 in middle (row 2 col 1), then fill
     * solids/stripes alternately so corners are mixed. */
    static const int rack_nums[15] = {
        1,
        14, 2,
        15, 8, 9,
        7, 12, 4, 13,
        3, 11, 6, 10, 5
    };

    int idx = 0;
    int slot = 1; /* slot 0 reserved for cue */
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col <= row; col++) {
            float r = (float)row;
            float c = (float)col - r * 0.5f;
            Vector2 p = {
                apex.x + row_step.x * r + col_step.x * c * 2.0f,
                apex.y + row_step.y * r + col_step.y * c * 2.0f,
            };
            place_ball(slot++, p, rack_nums[idx++], false);
        }
    }

    place_ball(0, s_cue_spot, 0, true);
}

/* -------- physics: motion update for a single ball ----------------------- */

static void integrate_ball(Ball *b, float dt)
{
    /* always decay vertical spin (English) — independent of slide/roll. */
    if (fabsf(b->wz) > EPS_SPIN) {
        float dec = (5.0f * MU_SPIN * G_EFF) / (2.0f * s_R);
        float dw  = dec * dt;
        if (dw > fabsf(b->wz)) b->wz = 0.0f;
        else                   b->wz -= (b->wz > 0 ? dw : -dw);
    } else {
        b->wz = 0.0f;
    }

    Vector2 u = contact_slip(b);
    float u_mag = v2_len(u);

    if (u_mag > EPS_SLIP) {
        /* SLIDING — friction opposes slip direction at the contact. */
        Vector2 uhat = (Vector2){ u.x / u_mag, u.y / u_mag };
        float a = MU_SLIDE * G_EFF;

        b->vel.x -= a * uhat.x * dt;
        b->vel.y -= a * uhat.y * dt;

        /* Angular response: alpha = (5 / (2R)) * (z_hat x a_linear) inverted.
         * F = -mu_s*m*g*uhat acts at r = -R*z_hat. tau = r x F gives
         *   tau_x =  R*F_y, tau_y = -R*F_x.
         * For a solid sphere I = (2/5)mR^2, so
         *   alpha_x =  (5/(2R)) *  ( -mu_s*g*uhat_y )  =  -(5*mu_s*g)/(2R) * uhat_y
         *   alpha_y = -(5/(2R)) *  ( -mu_s*g*uhat_x )  =   (5*mu_s*g)/(2R) * uhat_x
         */
        float alpha = (5.0f * MU_SLIDE * G_EFF) / (2.0f * s_R);
        b->wx += -alpha * uhat.y * dt;
        b->wy +=  alpha * uhat.x * dt;
    } else {
        /* ROLLING — lock w_xy to v, apply rolling friction. */
        float vmag = v2_len(b->vel);
        if (vmag > EPS_VEL) {
            Vector2 vhat = (Vector2){ b->vel.x / vmag, b->vel.y / vmag };
            float a = MU_ROLL * G_EFF;
            float dv = a * dt;
            if (dv >= vmag) {
                b->vel = (Vector2){0, 0};
                b->wx = b->wy = 0.0f;
            } else {
                b->vel.x -= vhat.x * dv;
                b->vel.y -= vhat.y * dv;
            }
        } else {
            b->vel = (Vector2){0, 0};
            b->wx = b->wy = 0.0f;
        }
        /* maintain rolling: w_xy = (1/R) * (z_hat x v) = (-v_y, v_x)/R  */
        b->wx = -b->vel.y / s_R;
        b->wy =  b->vel.x / s_R;
    }

    b->pos.x += b->vel.x * dt;
    b->pos.y += b->vel.y * dt;
}

/* -------- collisions ------------------------------------------------------ */

static void resolve_ball_ball(Ball *a, Ball *b)
{
    Vector2 d = v2_sub(b->pos, a->pos);
    float dist = v2_len(d);
    float min_d = 2.0f * s_R;
    if (dist >= min_d || dist < 1e-6f) return;

    Vector2 n = (Vector2){ d.x / dist, d.y / dist };

    /* positional correction to remove overlap */
    float overlap = min_d - dist;
    Vector2 corr = v2_scale(n, overlap * 0.5f);
    a->pos = v2_sub(a->pos, corr);
    b->pos = v2_add(b->pos, corr);

    /* velocities along normal */
    Vector2 rel = v2_sub(b->vel, a->vel);
    float rel_n = v2_dot(rel, n);
    if (rel_n > 0.0f) return; /* separating */

    /* equal mass frictionless impulse with restitution */
    float j = -(1.0f + E_BALL) * rel_n * 0.5f;
    Vector2 jn = v2_scale(n, j);
    a->vel = v2_sub(a->vel, jn);
    b->vel = v2_add(b->vel, jn);
}

/* Cushion rebound for a ball whose center crossed a wall. axis: 0 = vertical
 * wall (x normal), 1 = horizontal wall (y normal). sign = +1 if normal points
 * in +x/+y, -1 otherwise. */
static void cushion_bounce(Ball *b, int axis, float sign)
{
    if (axis == 0) {
        /* normal along x */
        if (sign * b->vel.x > 0.0f) return; /* already moving away */
        float vn = b->vel.x;
        float vt = b->vel.y;
        b->vel.x = -vn * E_CUSHION;
        /* English carry: side-spin (wz) tweaks tangential velocity. The sign
         * depends on which cushion is hit. */
        b->vel.y = vt + sign * CUSH_SPIN_KICK * s_R * b->wz;
        /* spin loses some energy on cushion */
        b->wz *= 0.85f;
        b->wx *= 0.6f;
        b->wy *= 0.6f;
    } else {
        if (sign * b->vel.y > 0.0f) return;
        float vn = b->vel.y;
        float vt = b->vel.x;
        b->vel.y = -vn * E_CUSHION;
        b->vel.x = vt - sign * CUSH_SPIN_KICK * s_R * b->wz;
        b->wz *= 0.85f;
        b->wx *= 0.6f;
        b->wy *= 0.6f;
    }
}

static void resolve_cushions(Ball *b)
{
    /* a ball is in a pocket-mouth zone when it's within pocket radius of any
     * pocket center; in that zone we suppress cushion collision so balls can
     * fall in instead of bouncing off a half-rail. */
    bool in_mouth = false;
    for (int i = 0; i < NUM_POCKETS; i++) {
        float dx = b->pos.x - s_pockets[i].pos.x;
        float dy = b->pos.y - s_pockets[i].pos.y;
        if (dx*dx + dy*dy <= s_pockets[i].radius * s_pockets[i].radius) {
            in_mouth = true; break;
        }
    }
    if (in_mouth) return;

    float xl = s_play.x + s_R;
    float xr = s_play.x + s_play.width - s_R;
    float yt = s_play.y + s_R;
    float yb = s_play.y + s_play.height - s_R;

    if (b->pos.x < xl) { b->pos.x = xl; cushion_bounce(b, 0, +1.0f); }
    if (b->pos.x > xr) { b->pos.x = xr; cushion_bounce(b, 0, -1.0f); }
    if (b->pos.y < yt) { b->pos.y = yt; cushion_bounce(b, 1, +1.0f); }
    if (b->pos.y > yb) { b->pos.y = yb; cushion_bounce(b, 1, -1.0f); }
}

static void check_pockets(void)
{
    for (int i = 0; i < MAX_BALLS; i++) {
        Ball *b = &s_balls[i];
        if (!b->active) continue;
        for (int p = 0; p < NUM_POCKETS; p++) {
            float dx = b->pos.x - s_pockets[p].pos.x;
            float dy = b->pos.y - s_pockets[p].pos.y;
            float r  = s_pockets[p].radius * 0.85f;
            if (dx*dx + dy*dy <= r*r) {
                if (b->is_cue) {
                    /* respot cue */
                    b->pos = s_cue_spot;
                    b->vel = (Vector2){0, 0};
                    b->wx = b->wy = b->wz = 0.0f;
                } else {
                    b->active = false;
                    s_pocketed_count++;
                }
                break;
            }
        }
    }
}

static bool all_at_rest(void)
{
    for (int i = 0; i < MAX_BALLS; i++) {
        if (!s_balls[i].active) continue;
        if (v2_len(s_balls[i].vel) > EPS_VEL) return false;
    }
    return true;
}

/* -------- input ----------------------------------------------------------- */

static void begin_drag(int touch_id, Vector2 p)
{
    if (s_drag_active) return;
    s_drag_active   = true;
    s_drag_touch_id = touch_id;
    s_drag_start    = p;
    s_drag_cur      = p;
}

static void update_drag_pos(int touch_id, Vector2 p)
{
    if (!s_drag_active || s_drag_touch_id != touch_id) return;
    s_drag_cur = p;
}

/* Compute shot direction (unit) and power (0..1) from pull-back drag. */
static bool drag_to_shot(Vector2 *out_dir, float *out_power)
{
    Vector2 pull = v2_sub(s_drag_start, s_drag_cur);  /* opposite to drag */
    float plen = v2_len(pull);
    if (plen < s_dead_zone) return false;
    *out_dir = (Vector2){ pull.x / plen, pull.y / plen };
    float p = (plen - s_dead_zone) / (s_max_pull - s_dead_zone);
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    *out_power = p;
    return true;
}

static void release_drag(int touch_id)
{
    if (!s_drag_active || s_drag_touch_id != touch_id) return;

    if (s_state == GS_AIM) {
        Vector2 dir; float power;
        if (drag_to_shot(&dir, &power)) {
            /* find cue ball and apply velocity */
            for (int i = 0; i < MAX_BALLS; i++) {
                if (s_balls[i].active && s_balls[i].is_cue) {
                    s_balls[i].vel = v2_scale(dir, power * MAX_SHOT_SPEED);
                    /* sliding initial state: angular velocity = 0 so the ball
                     * starts in stick-shot mode and naturally transitions to
                     * rolling via cloth friction. */
                    s_balls[i].wx = 0.0f;
                    s_balls[i].wy = 0.0f;
                    s_balls[i].wz = 0.0f;
                    s_shot_flash = 0.25f;
                    s_state = GS_MOVING;
                    break;
                }
            }
        }
    }

    s_drag_active = false;
    s_drag_touch_id = -1;
}

static void poll_input(void)
{
    /* touch points */
    int cur_count = GetTouchPointCount();
    int     cur_ids[MAX_TOUCHES];
    Vector2 cur_pos[MAX_TOUCHES];
    if (cur_count > MAX_TOUCHES) cur_count = MAX_TOUCHES;
    for (int i = 0; i < cur_count; i++) {
        cur_ids[i] = GetTouchPointId(i);
        cur_pos[i] = GetTouchPosition(i);
    }

    /* releases */
    for (int p = 0; p < s_prev_touch_count; p++) {
        bool still = false;
        for (int c = 0; c < cur_count; c++)
            if (cur_ids[c] == s_prev_touch_ids[p]) { still = true; break; }
        if (!still) release_drag(s_prev_touch_ids[p]);
    }
    /* new + moved */
    for (int c = 0; c < cur_count; c++) {
        bool was = false;
        for (int p = 0; p < s_prev_touch_count; p++)
            if (s_prev_touch_ids[p] == cur_ids[c]) { was = true; break; }
        if (!was) begin_drag(cur_ids[c], cur_pos[c]);
        else      update_drag_pos(cur_ids[c], cur_pos[c]);
    }
    s_prev_touch_count = cur_count;
    for (int i = 0; i < cur_count; i++) s_prev_touch_ids[i] = cur_ids[i];

    /* mouse fallback for desktop */
    if (cur_count == 0) {
        Vector2 mp = GetMousePosition();
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))       begin_drag(-1, mp);
        else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))     update_drag_pos(-1, mp);
        else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) release_drag(-1);
    }
}

/* -------- drawing --------------------------------------------------------- */

static void draw_table(void)
{
    /* outer rail (wood) */
    float rail = s_R * 1.4f;
    Rectangle outer = {
        s_play.x - rail, s_play.y - rail,
        s_play.width + 2*rail, s_play.height + 2*rail,
    };
    DrawRectangleRounded(outer, 0.06f, 8, (Color){75, 45, 22, 255});
    DrawRectangleRoundedLines(outer, 0.06f, 8, (Color){40, 22, 10, 255});

    /* felt */
    DrawRectangleRec(s_play, (Color){32, 110, 60, 255});

    /* head string and foot spot */
    bool portrait = (s_play.height >= s_play.width);
    Color marker = (Color){255, 255, 255, 35};
    if (portrait) {
        float y = s_play.y + s_play.height * 0.75f;
        DrawLineEx((Vector2){s_play.x, y}, (Vector2){s_play.x + s_play.width, y}, 1.0f, marker);
        float fy = s_play.y + s_play.height * 0.25f;
        DrawCircleV((Vector2){s_play.x + s_play.width*0.5f, fy}, 2.0f, marker);
    } else {
        float x = s_play.x + s_play.width * 0.25f;
        DrawLineEx((Vector2){x, s_play.y}, (Vector2){x, s_play.y + s_play.height}, 1.0f, marker);
        float fx = s_play.x + s_play.width * 0.75f;
        DrawCircleV((Vector2){fx, s_play.y + s_play.height*0.5f}, 2.0f, marker);
    }

    /* pockets */
    for (int i = 0; i < NUM_POCKETS; i++) {
        DrawCircleV(s_pockets[i].pos, s_pockets[i].radius, (Color){20, 18, 14, 255});
        DrawCircleLines((int)s_pockets[i].pos.x, (int)s_pockets[i].pos.y,
                        s_pockets[i].radius, (Color){10, 8, 4, 255});
    }
}

static void draw_ball(const Ball *b)
{
    /* shadow */
    DrawCircleV((Vector2){b->pos.x + s_R*0.18f, b->pos.y + s_R*0.18f},
                s_R, (Color){0, 0, 0, 70});

    if (b->is_cue) {
        DrawCircleV(b->pos, s_R, RAYWHITE);
        DrawCircleLines((int)b->pos.x, (int)b->pos.y, s_R, (Color){120, 120, 120, 255});
        return;
    }

    if (b->striped) {
        /* white base */
        DrawCircleV(b->pos, s_R, RAYWHITE);
        /* color stripe through center */
        Rectangle stripe = {
            b->pos.x - s_R, b->pos.y - s_R*0.45f,
            s_R*2.0f, s_R*0.9f,
        };
        BeginScissorMode((int)(b->pos.x - s_R), (int)(b->pos.y - s_R),
                         (int)(s_R*2.0f), (int)(s_R*2.0f));
            DrawRectangleRec(stripe, b->color);
        EndScissorMode();
    } else {
        DrawCircleV(b->pos, s_R, b->color);
    }

    /* number disc */
    float disc_r = s_R * 0.45f;
    DrawCircleV(b->pos, disc_r, RAYWHITE);

    char buf[4];
    int n = b->number;
    buf[0] = (n >= 10) ? ('0' + n/10) : ('0' + n);
    buf[1] = (n >= 10) ? ('0' + n%10) : 0;
    buf[2] = 0;
    int fs = (int)(s_R * 0.9f);
    int tw = MeasureText(buf, fs);
    DrawText(buf, (int)(b->pos.x - tw*0.5f), (int)(b->pos.y - fs*0.55f),
             fs, (Color){20, 20, 20, 255});

    DrawCircleLines((int)b->pos.x, (int)b->pos.y, s_R, (Color){50, 30, 10, 180});
}

static void draw_aim_overlay(void)
{
    if (s_state != GS_AIM || !s_drag_active) return;

    Vector2 dir; float power;
    if (!drag_to_shot(&dir, &power)) {
        /* in dead zone — show cancel hint */
        DrawCircleLines((int)s_drag_start.x, (int)s_drag_start.y,
                        s_dead_zone, (Color){240, 80, 80, 180});
        DrawCircleV(s_drag_cur, 6.0f, (Color){240, 80, 80, 220});
        return;
    }

    /* find cue ball */
    int cue = -1;
    for (int i = 0; i < MAX_BALLS; i++)
        if (s_balls[i].active && s_balls[i].is_cue) { cue = i; break; }
    if (cue < 0) return;

    Vector2 cp = s_balls[cue].pos;

    /* aim ray, clipped to table */
    float max_ray = sqrtf(s_play.width*s_play.width + s_play.height*s_play.height);
    Vector2 ray_end = v2_add(cp, v2_scale(dir, max_ray));
    Color aim_col = (Color){255, 245, 200, (unsigned char)(180.0f + 60.0f*power)};
    DrawLineEx(cp, ray_end, 2.0f, aim_col);

    /* dashed power markers along the aim line */
    int marks = 8;
    for (int i = 1; i <= marks; i++) {
        float t = (float)i / (float)marks;
        if (t > power) break;
        Vector2 m = v2_add(cp, v2_scale(dir, s_R * 6.0f * (float)i));
        DrawCircleV(m, 2.5f, (Color){255, 230, 120, 200});
    }

    /* pull-back chevron near the touch */
    DrawCircleV(s_drag_start, s_dead_zone, (Color){255, 255, 255, 25});
    DrawLineEx(s_drag_start, s_drag_cur, 3.0f, (Color){255, 235, 180, 160});
    DrawCircleV(s_drag_cur, 8.0f, (Color){255, 235, 180, 220});

    /* power bar at bottom of table */
    float bar_w = s_play.width * 0.6f;
    float bar_h = s_R * 0.5f;
    float bx = s_play.x + (s_play.width - bar_w) * 0.5f;
    float by = s_play.y + s_play.height + s_R * 1.6f;
    DrawRectangleRounded((Rectangle){bx, by, bar_w, bar_h}, 0.5f, 6, (Color){0, 0, 0, 120});
    DrawRectangleRounded((Rectangle){bx, by, bar_w * power, bar_h}, 0.5f, 6,
                         (Color){240, 200, 80, 240});
}

static void draw_hud(void)
{
    int fs = GetScreenWidth() / 38;
    const char *hint;
    if (s_state == GS_MOVING) hint = "Balls in motion...";
    else if (s_drag_active)   hint = "Release to shoot — release in red zone to cancel";
    else                      hint = "Drag anywhere to aim (pull back like a cue)";
    DrawText(hint, 12, 10, fs, (Color){240, 240, 230, 220});

    char score[64];
    snprintf(score, sizeof score, "Pocketed: %d / 15", s_pocketed_count);
    int sw = MeasureText(score, fs);
    DrawText(score, GetScreenWidth() - sw - 12, 10, fs, (Color){240, 240, 230, 220});

    if (s_shot_flash > 0.0f) {
        unsigned char a = (unsigned char)(s_shot_flash * 90.0f);
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                      (Color){255, 245, 200, a});
    }
}

/* -------- prototype interface -------------------------------------------- */

static void PoolInit(void)
{
    layout_table();
    rack_balls();
    s_state = GS_AIM;
    s_pocketed_count = 0;
    s_drag_active = false;
    s_drag_touch_id = -1;
    s_prev_touch_count = 0;
    s_shot_flash = 0.0f;
}

static void PoolUpdate(float dt)
{
    /* re-layout if the window resized */
    static int s_last_w = 0, s_last_h = 0;
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    if (sw != s_last_w || sh != s_last_h) {
        Rectangle old_play = s_play;
        layout_table();
        /* scale ball positions into the new play area */
        if (s_last_w > 0 && old_play.width > 0 && old_play.height > 0) {
            for (int i = 0; i < MAX_BALLS; i++) {
                if (!s_balls[i].active) continue;
                float fx = (s_balls[i].pos.x - old_play.x) / old_play.width;
                float fy = (s_balls[i].pos.y - old_play.y) / old_play.height;
                s_balls[i].pos.x = s_play.x + fx * s_play.width;
                s_balls[i].pos.y = s_play.y + fy * s_play.height;
            }
        }
        s_last_w = sw; s_last_h = sh;
    }

    if (s_shot_flash > 0.0f) {
        s_shot_flash -= dt * 4.0f;
        if (s_shot_flash < 0.0f) s_shot_flash = 0.0f;
    }

    if (s_state == GS_AIM) {
        poll_input();
        return;
    }

    /* GS_MOVING: ignore input, advance physics with sub-steps. */
    float sub = dt / (float)SUBSTEPS;
    for (int s = 0; s < SUBSTEPS; s++) {
        for (int i = 0; i < MAX_BALLS; i++) {
            if (!s_balls[i].active) continue;
            integrate_ball(&s_balls[i], sub);
        }
        for (int i = 0; i < MAX_BALLS; i++) {
            if (!s_balls[i].active) continue;
            for (int j = i + 1; j < MAX_BALLS; j++) {
                if (!s_balls[j].active) continue;
                resolve_ball_ball(&s_balls[i], &s_balls[j]);
            }
        }
        for (int i = 0; i < MAX_BALLS; i++) {
            if (!s_balls[i].active) continue;
            resolve_cushions(&s_balls[i]);
        }
        check_pockets();
    }

    if (all_at_rest()) {
        s_state = GS_AIM;
        s_prev_touch_count = 0;
        s_drag_active = false;
    }
}

static void PoolDraw(void)
{
    ClearBackground((Color){18, 22, 24, 255});

    draw_table();

    /* draw non-cue first then cue on top */
    for (int i = 0; i < MAX_BALLS; i++) {
        if (s_balls[i].active && !s_balls[i].is_cue) draw_ball(&s_balls[i]);
    }
    for (int i = 0; i < MAX_BALLS; i++) {
        if (s_balls[i].active && s_balls[i].is_cue) draw_ball(&s_balls[i]);
    }

    draw_aim_overlay();
    draw_hud();
}

static void PoolDeinit(void)
{
    /* no dynamic allocations to free */
}

const Prototype PoolProto = {
    .name        = "Pool",
    .description = "Top-down billiards with sliding/rolling/spin friction. Drag to aim.",
    .Init        = PoolInit,
    .Update      = PoolUpdate,
    .Draw        = PoolDraw,
    .Deinit      = PoolDeinit,
};
