#include "minigolf.h"
#include "raylib.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------------------------------------------------------------- constants */

#define DESIGN_W            600.0f
#define DESIGN_H            1000.0f

#define FRICTION_DECEL      520.0f
#define MIN_SPEED             3.0f
#define WALL_RESTITUTION      0.68f
#define CUSHION_TANGENT_KEEP  0.90f

#define MAX_SHOT_SPEED      1600.0f
#define FULL_POWER_DRAG_FRAC  0.22f
#define DEAD_ZONE            18.0f

#define HOLE_COUNT           3
#define MAX_WALLS           64
#define BALL_RADIUS         14.0f
#define CORRIDOR_HALF_W     75.0f
#define HOLE_POCKET_R       22.0f

#define MAX_PATH_NODES       8
#define SEG_LEN_MIN        140.0f
#define SEG_LEN_MAX        280.0f

/* ------------------------------------------------------------------ structs */

typedef struct {
    Vector2 a, b;
    Vector2 normal;   /* unit normal toward play area */
} WallSeg;

typedef struct {
    WallSeg walls[MAX_WALLS];
    int     wall_count;
    Vector2 tee;
    Vector2 hole_pos;
    float   hole_r;
    int     par;
} CourseHole;

typedef struct {
    Vector2 pos, vel;
    float   radius;
    bool    sunk;
} Ball;

typedef struct { unsigned int state; } Lcg;

/* --------------------------------------------------------------- module state */

static CourseHole   s_holes[HOLE_COUNT];
static Ball         s_ball;
static int          s_hole_idx;
static int          s_strokes[HOLE_COUNT];
static bool         s_aiming;
static bool         s_prev_pressed;
static Vector2      s_aim_press, s_aim_cur;
static Camera2D     s_camera;
static bool         s_hole_done;
static bool         s_show_scorecard;

/* ------------------------------------------------------------------- helpers */

static float mg_len(Vector2 v)            { return sqrtf(v.x*v.x + v.y*v.y); }
static Vector2 mg_add(Vector2 a, Vector2 b){ return (Vector2){a.x+b.x, a.y+b.y}; }
static Vector2 mg_sub(Vector2 a, Vector2 b){ return (Vector2){a.x-b.x, a.y-b.y}; }
static Vector2 mg_scale(Vector2 v, float s){ return (Vector2){v.x*s, v.y*s}; }
static float mg_dot(Vector2 a, Vector2 b)  { return a.x*b.x + a.y*b.y; }
static float mg_clamp(float v, float lo, float hi)
    { return v < lo ? lo : v > hi ? hi : v; }

static Vector2 mg_normalize(Vector2 v)
{
    float len = mg_len(v);
    if (len < 0.0001f) return (Vector2){0, -1};
    return mg_scale(v, 1.0f / len);
}

/* ---------------------------------------------------------------- LCG prng */

static unsigned int lcg_next(Lcg *r)
{
    r->state = r->state * 1664525u + 1013904223u;
    return r->state;
}

static int lcg_range(Lcg *r, int lo, int hi)
{
    return lo + (int)(lcg_next(r) % (unsigned int)(hi - lo + 1));
}

static float lcg_float(Lcg *r)
{
    return (float)(lcg_next(r) & 0xFFFFFF) / (float)0x1000000;
}

/* ---------------------------------------------------------- course generation */

static void add_wall(CourseHole *h, Vector2 a, Vector2 b, Vector2 normal)
{
    if (h->wall_count >= MAX_WALLS) {
        TraceLog(LOG_WARNING, "MINIGOLF: MAX_WALLS exceeded");
        return;
    }
    h->walls[h->wall_count++] = (WallSeg){a, b, normal};
}

/* Add the 4 outward-facing walls of a rectangular bumper */
static void add_bumper(CourseHole *h, Vector2 center, Vector2 dir, float half_len, float half_thick)
{
    /* dir is along the bumper length; perp is across its thickness */
    Vector2 perp = {-dir.y, dir.x};

    Vector2 corners[4];
    corners[0] = mg_add(mg_add(center, mg_scale(dir,  half_len)), mg_scale(perp,  half_thick));
    corners[1] = mg_add(mg_add(center, mg_scale(dir,  half_len)), mg_scale(perp, -half_thick));
    corners[2] = mg_add(mg_add(center, mg_scale(dir, -half_len)), mg_scale(perp, -half_thick));
    corners[3] = mg_add(mg_add(center, mg_scale(dir, -half_len)), mg_scale(perp,  half_thick));

    /* 4 sides; normal points outward (away from center) */
    Vector2 face_centers[4];
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        face_centers[i] = mg_scale(mg_add(corners[i], corners[j]), 0.5f);
    }
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        Vector2 out_n = mg_normalize(mg_sub(face_centers[i], center));
        add_wall(h, corners[i], corners[j], out_n);
    }
}

static void generate_hole(CourseHole *h, unsigned int seed, int difficulty)
{
    h->wall_count = 0;

    Lcg rng = {seed};
    int num_segs = 2 + difficulty;

    /* --- path nodes ---- */
    Vector2 path[MAX_PATH_NODES];
    int     node_count = num_segs + 1;

    /* start bottom-centre, going UP (0,-1) */
    path[0] = (Vector2){DESIGN_W * 0.5f, DESIGN_H - 130.0f};

    /* direction indices: 0=UP, 1=RIGHT, 2=DOWN, 3=LEFT */
    static const Vector2 DIRS[4] = {{0,-1},{1,0},{0,1},{-1,0}};
    int dir_idx = 0;

    for (int i = 0; i < num_segs; i++) {
        /* turn at every segment after the first */
        if (i > 0) {
            int turn = lcg_range(&rng, 0, 1);
            dir_idx = turn ? (dir_idx + 1) % 4 : (dir_idx + 3) % 4;
        }
        float seg_len = SEG_LEN_MIN + lcg_float(&rng) * (SEG_LEN_MAX - SEG_LEN_MIN);
        Vector2 d = DIRS[dir_idx];
        Vector2 next = mg_add(path[i], mg_scale(d, seg_len));

        /* clamp to design bounds */
        float margin = CORRIDOR_HALF_W + 20.0f;
        next.x = mg_clamp(next.x, margin, DESIGN_W - margin);
        next.y = mg_clamp(next.y, margin, DESIGN_H - margin);
        path[i + 1] = next;
    }

    /* --- wall segments from each corridor segment --- */
    for (int i = 0; i < num_segs; i++) {
        Vector2 seg_dir = mg_normalize(mg_sub(path[i+1], path[i]));
        /* left-hand perp (screen Y-down): rotate dir 90° CCW = (-dy, dx) */
        Vector2 perp_l = (Vector2){-seg_dir.y,  seg_dir.x};
        /* right-hand perp */
        Vector2 perp_r = (Vector2){ seg_dir.y, -seg_dir.x};

        float R = CORRIDOR_HALF_W;

        Vector2 lA = mg_add(path[i],   mg_scale(perp_l, R));
        Vector2 lB = mg_add(path[i+1], mg_scale(perp_l, R));
        Vector2 rA = mg_add(path[i],   mg_scale(perp_r, R));
        Vector2 rB = mg_add(path[i+1], mg_scale(perp_r, R));

        /* Left wall: normal = perp_r (inward, toward corridor center) */
        add_wall(h, lA, lB, perp_r);
        /* Right wall: normal = perp_l */
        add_wall(h, rB, rA, perp_l);
    }

    /* --- tee end cap (close the start) --- */
    {
        Vector2 seg_dir = mg_normalize(mg_sub(path[1], path[0]));
        Vector2 perp_l  = (Vector2){-seg_dir.y, seg_dir.x};
        Vector2 perp_r  = (Vector2){ seg_dir.y,-seg_dir.x};
        float R = CORRIDOR_HALF_W;
        Vector2 cap_l = mg_add(path[0], mg_scale(perp_l, R));
        Vector2 cap_r = mg_add(path[0], mg_scale(perp_r, R));
        Vector2 cap_n = mg_scale(seg_dir, -1.0f);  /* faces back */
        add_wall(h, cap_r, cap_l, cap_n);
    }

    /* --- outer corner fillers at each bend junction --- */
    for (int i = 1; i < num_segs; i++) {
        Vector2 d1 = mg_normalize(mg_sub(path[i],   path[i-1]));
        Vector2 d2 = mg_normalize(mg_sub(path[i+1], path[i]));

        float cross = d1.x*d2.y - d1.y*d2.x;
        if (fabsf(cross) < 0.01f) continue;  /* no real turn */

        float R = CORRIDOR_HALF_W;

        /* determine which side is the outer corner:
           cross > 0 → right turn (CW in screen-Y-down) → outer corner on left side */
        Vector2 perp_in_outer  = (cross > 0)
            ? (Vector2){-d1.y, d1.x}   /* left of d1 */
            : (Vector2){ d1.y,-d1.x};  /* right of d1 */
        Vector2 perp_out_outer = (cross > 0)
            ? (Vector2){-d2.y, d2.x}   /* left of d2 */
            : (Vector2){ d2.y,-d2.x};  /* right of d2 */

        Vector2 fA = mg_add(path[i], mg_scale(perp_in_outer,  R));
        Vector2 fB = mg_add(path[i], mg_scale(perp_out_outer, R));

        /* outer miter corner point */
        Vector2 miter = mg_add(fA, mg_scale(perp_out_outer, R * 0.5f));
        (void)miter;

        /* normal: inward (toward path[i] from outer corner) */
        Vector2 fN = mg_normalize(mg_sub(path[i], mg_scale(mg_add(fA, fB), 0.5f)));
        add_wall(h, fA, fB, fN);

        /* inner corner diagonal bank for difficulty >= 2 */
        if (difficulty >= 2) {
            Vector2 perp_in_inner  = (cross > 0)
                ? (Vector2){ d1.y,-d1.x}
                : (Vector2){-d1.y, d1.x};
            Vector2 perp_out_inner = (cross > 0)
                ? (Vector2){ d2.y,-d2.x}
                : (Vector2){-d2.y, d2.x};
            float bank_r = R * 0.45f;
            Vector2 bA = mg_add(path[i], mg_scale(perp_in_inner,  bank_r));
            Vector2 bB = mg_add(path[i], mg_scale(perp_out_inner, bank_r));
            Vector2 bN = mg_normalize(mg_sub(mg_scale(mg_add(bA, bB), 0.5f), path[i]));
            add_wall(h, bA, bB, bN);
        }
    }

    /* --- bumpers (difficulty >= 1): one per interior segment --- */
    if (difficulty >= 1) {
        for (int i = 0; i < num_segs; i++) {
            /* skip first and last segments to keep tee/hole clear */
            if (i == 0 || i == num_segs - 1) continue;
            Vector2 seg_dir = mg_normalize(mg_sub(path[i+1], path[i]));
            Vector2 mid = mg_scale(mg_add(path[i], path[i+1]), 0.5f);
            float half_len   = CORRIDOR_HALF_W * 0.3f;
            float half_thick = 9.0f;
            add_bumper(h, mid, seg_dir, half_len, half_thick);
        }
    }

    /* --- tee and hole --- */
    h->tee      = path[0];
    h->hole_pos = path[num_segs];
    h->hole_r   = HOLE_POCKET_R;
    h->par      = 2 + num_segs;
}

/* ------------------------------------------------------------------ physics */

static void resolve_wall_seg(Ball *b, const WallSeg *w)
{
    Vector2 ab  = mg_sub(w->b, w->a);
    float   len = mg_len(ab);
    if (len < 0.001f) return;

    Vector2 d  = mg_scale(ab, 1.0f / len);
    float   t  = mg_clamp(mg_dot(mg_sub(b->pos, w->a), d), 0.0f, len);
    Vector2 closest = mg_add(w->a, mg_scale(d, t));

    float dist = mg_dot(mg_sub(b->pos, closest), w->normal);
    if (dist >= b->radius) return;

    /* push out */
    b->pos = mg_add(b->pos, mg_scale(w->normal, b->radius - dist));

    /* reflect velocity along normal if approaching */
    float v_n = mg_dot(b->vel, w->normal);
    if (v_n < 0.0f) {
        b->vel = mg_add(b->vel, mg_scale(w->normal, -v_n * (1.0f + WALL_RESTITUTION)));
        /* tangential damping */
        Vector2 tangent = {w->normal.y, -w->normal.x};
        float v_t = mg_dot(b->vel, tangent);
        b->vel = mg_add(
            mg_scale(w->normal,  mg_dot(b->vel, w->normal)),
            mg_scale(tangent, v_t * CUSHION_TANGENT_KEEP)
        );
    }
}

static void integrate(float dt)
{
    if (s_ball.sunk) return;

    float speed = mg_len(s_ball.vel);
    int   steps = 1;
    if (speed > 0.0f) {
        float max_dist = s_ball.radius * 0.5f;
        int   needed   = (int)ceilf((speed * dt) / max_dist);
        steps = needed < 1 ? 1 : needed > 8 ? 8 : needed;
    }
    float sdt = dt / (float)steps;

    CourseHole *hole = &s_holes[s_hole_idx];

    for (int step = 0; step < steps; step++) {
        s_ball.pos = mg_add(s_ball.pos, mg_scale(s_ball.vel, sdt));

        for (int w = 0; w < hole->wall_count; w++)
            resolve_wall_seg(&s_ball, &hole->walls[w]);

        /* pocket detection */
        float d = mg_len(mg_sub(s_ball.pos, hole->hole_pos));
        if (d < hole->hole_r + s_ball.radius * 0.6f) {
            s_ball.sunk  = true;
            s_ball.vel   = (Vector2){0, 0};
            s_hole_done  = true;
            return;
        }

        /* friction */
        float sp = mg_len(s_ball.vel);
        if (sp <= MIN_SPEED) {
            s_ball.vel = (Vector2){0, 0};
        } else {
            float ns = sp - FRICTION_DECEL * sdt;
            if (ns < 0.0f) ns = 0.0f;
            s_ball.vel = mg_scale(s_ball.vel, ns / sp);
        }
    }
}

/* -------------------------------------------------------------------- input */

static bool poll_pointer(Vector2 *out)
{
    if (GetTouchPointCount() > 0) { *out = GetTouchPosition(0); return true; }
    *out = GetMousePosition();
    return IsMouseButtonDown(MOUSE_BUTTON_LEFT);
}

/* drag in screen pixels → world units/s */
static float power_gain(void)
{
    float sh = (float)GetScreenHeight();
    if (sh < 1.0f) return 0.0f;
    return MAX_SHOT_SPEED / (sh * FULL_POWER_DRAG_FRAC);
}

static void start_hole(int idx);
void MinigolfInit(void);   /* forward decl for restart-from-scorecard */

static void handle_input(void)
{
    Vector2 ptr;
    bool pressed = poll_pointer(&ptr);

    if (s_show_scorecard) {
        if (!s_prev_pressed && pressed) {
            MinigolfInit();
        }
        s_prev_pressed = pressed;
        return;
    }

    if (s_hole_done) {
        if (!s_prev_pressed && pressed) {
            s_hole_idx++;
            if (s_hole_idx >= HOLE_COUNT)
                s_show_scorecard = true;
            else
                start_hole(s_hole_idx);
        }
        s_prev_pressed = pressed;
        return;
    }

    bool at_rest = mg_len(s_ball.vel) < 0.5f && !s_ball.sunk;

    if (!s_prev_pressed && pressed) {
        if (at_rest) {
            s_aiming    = true;
            s_aim_press = ptr;
            s_aim_cur   = ptr;
        }
    } else if (s_prev_pressed && pressed) {
        if (s_aiming) s_aim_cur = ptr;
    } else if (s_prev_pressed && !pressed) {
        if (s_aiming && at_rest) {
            Vector2 drag = mg_sub(s_aim_cur, s_aim_press);
            float   dist = mg_len(drag);
            if (dist > DEAD_ZONE) {
                Vector2 shot_dir = mg_scale(drag, -1.0f / dist);
                float   speed    = mg_clamp(dist * power_gain(), 0.0f, MAX_SHOT_SPEED);
                s_ball.vel = mg_scale(shot_dir, speed);
                s_strokes[s_hole_idx]++;
            }
        }
        s_aiming = false;
    }
    s_prev_pressed = pressed;
}

/* ------------------------------------------------------------------- camera */

static void snap_camera(void)
{
    s_camera.zoom     = (float)GetScreenHeight() / DESIGN_H;
    s_camera.offset   = (Vector2){(float)GetScreenWidth() * 0.5f,
                                   (float)GetScreenHeight() * 0.5f};
    s_camera.target   = s_ball.pos;
    s_camera.rotation = 0.0f;
}

static void update_camera(float dt)
{
    s_camera.zoom   = (float)GetScreenHeight() / DESIGN_H;
    s_camera.offset = (Vector2){(float)GetScreenWidth() * 0.5f,
                                 (float)GetScreenHeight() * 0.5f};
    float lp = mg_clamp(8.0f * dt, 0.0f, 1.0f);
    s_camera.target.x += (s_ball.pos.x - s_camera.target.x) * lp;
    s_camera.target.y += (s_ball.pos.y - s_camera.target.y) * lp;
}

/* ---------------------------------------------------------------- hole start */

static void start_hole(int idx)
{
    s_hole_idx     = idx;
    s_ball.pos     = s_holes[idx].tee;
    s_ball.vel     = (Vector2){0, 0};
    s_ball.radius  = BALL_RADIUS;
    s_ball.sunk    = false;
    s_hole_done    = false;
    s_aiming       = false;
    s_prev_pressed = false;
    snap_camera();
}

/* ----------------------------------------------------------------- rendering */

static Color RAIL_COLOR    = {80,  45,  20, 255};
static Color CUSHION_COLOR = {40, 130,  65, 255};
static Color FELT_COLOR    = {30, 110,  55, 255};

static void draw_course_background(const CourseHole *h)
{
    /* Flood the design area with felt colour so the course looks like a table */
    float pad = CORRIDOR_HALF_W + 20.0f;

    /* Compute bounding box of all wall endpoints */
    float xmin = DESIGN_W, xmax = 0, ymin = DESIGN_H, ymax = 0;
    for (int i = 0; i < h->wall_count; i++) {
        const WallSeg *w = &h->walls[i];
        if (w->a.x < xmin) xmin = w->a.x;
        if (w->a.y < ymin) ymin = w->a.y;
        if (w->a.x > xmax) xmax = w->a.x;
        if (w->a.y > ymax) ymax = w->a.y;
        if (w->b.x < xmin) xmin = w->b.x;
        if (w->b.y < ymin) ymin = w->b.y;
        if (w->b.x > xmax) xmax = w->b.x;
        if (w->b.y > ymax) ymax = w->b.y;
    }
    Rectangle felt = {xmin - pad, ymin - pad,
                      xmax - xmin + pad*2, ymax - ymin + pad*2};
    /* Outer rail */
    float rail = 14.0f;
    Rectangle rail_rect = {felt.x - rail, felt.y - rail,
                            felt.width + rail*2, felt.height + rail*2};
    DrawRectangleRec(rail_rect, RAIL_COLOR);
    DrawRectangleRec(felt, FELT_COLOR);
}

static void draw_walls(const CourseHole *h)
{
    for (int i = 0; i < h->wall_count; i++) {
        const WallSeg *w = &h->walls[i];
        DrawLineEx(w->a, w->b, 11.0f, RAIL_COLOR);
        DrawLineEx(w->a, w->b,  5.0f, CUSHION_COLOR);
    }
}

static void draw_tee_marker(Vector2 tee)
{
    float r = BALL_RADIUS * 0.7f;
    DrawCircleLinesV(tee, r, (Color){255, 255, 255, 120});
    DrawLineEx((Vector2){tee.x - r, tee.y},
               (Vector2){tee.x + r, tee.y}, 2.0f, (Color){255,255,255,100});
    DrawLineEx((Vector2){tee.x, tee.y - r},
               (Vector2){tee.x, tee.y + r}, 2.0f, (Color){255,255,255,100});
}

static void draw_aim(void)
{
    Vector2 drag = mg_sub(s_aim_cur, s_aim_press);
    float   dist = mg_len(drag);

    /* dead-zone ring */
    DrawCircleLinesV(s_aim_press, DEAD_ZONE,
        dist <= DEAD_ZONE ? (Color){200,200,200,110} : (Color){180,180,180,50});
    DrawLineEx(s_aim_press, s_aim_cur, 2.0f, (Color){220,220,220,110});

    if (dist <= DEAD_ZONE) return;

    float    power_t  = mg_clamp(dist * power_gain() / MAX_SHOT_SPEED, 0.0f, 1.0f);
    Vector2  shot_dir = mg_scale(drag, -1.0f / dist);
    Vector2  aim_end  = mg_add(s_ball.pos, mg_scale(shot_dir, 220.0f));

    Color line_col = {
        (unsigned char)(80 + 175 * power_t),
        (unsigned char)(200 * (1.0f - power_t)),
        (unsigned char)(220 * (1.0f - power_t) + 35),
        220
    };
    DrawLineEx(s_ball.pos, aim_end, 3.0f, line_col);

    /* power bar */
    float bar_len = s_ball.radius * 5.0f;
    Rectangle bg = {s_ball.pos.x - bar_len*0.5f,
                    s_ball.pos.y - s_ball.radius * 3.5f,
                    bar_len, 5.0f};
    DrawRectangleRec(bg, (Color){0,0,0,120});
    Rectangle fg = bg; fg.width = bar_len * power_t;
    DrawRectangleRec(fg, line_col);
}

static void draw_hud(void)
{
    int fs = GetScreenWidth() / 28;
    if (fs < 14) fs = 14;

    char buf[64];

    /* top-left: hole number */
    snprintf(buf, sizeof(buf), "Hole %d / %d", s_hole_idx + 1, HOLE_COUNT);
    DrawText(buf, 12, 12, fs, RAYWHITE);

    /* top-right: par and strokes */
    snprintf(buf, sizeof(buf), "Par %d  |  Strokes: %d",
             s_holes[s_hole_idx].par, s_strokes[s_hole_idx]);
    int tw = MeasureText(buf, fs);
    DrawText(buf, GetScreenWidth() - tw - 12, 12, fs, RAYWHITE);
}

static const char *golf_term(int delta)
{
    if (delta <= -2) return "Eagle!";
    if (delta == -1) return "Birdie!";
    if (delta ==  0) return "Par";
    if (delta ==  1) return "Bogey";
    if (delta ==  2) return "Double Bogey";
    return "Over Par";
}

static void draw_hole_complete_overlay(void)
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int fs = sw / 22; if (fs < 16) fs = 16;

    int delta = s_strokes[s_hole_idx] - s_holes[s_hole_idx].par;
    char line1[48], line2[48];
    snprintf(line1, sizeof(line1), "%s  (%+d)", golf_term(delta), delta);
    snprintf(line2, sizeof(line2), "Tap to continue");

    int w1 = MeasureText(line1, fs);
    int w2 = MeasureText(line2, fs - 4);

    int total_h = fs + 8 + (fs - 4) + 24;
    int box_w   = (w1 > w2 ? w1 : w2) + 40;
    int box_x   = (sw - box_w) / 2;
    int box_y   = sh / 2 - total_h / 2 - 12;

    DrawRectangle(box_x, box_y, box_w, total_h + 24, (Color){0,0,0,180});
    DrawText(line1, (sw - w1) / 2, box_y + 12,      fs,     RAYWHITE);
    DrawText(line2, (sw - w2) / 2, box_y + 12 + fs + 8, fs-4, (Color){200,200,200,200});
}

static void draw_scorecard(void)
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, (Color){0, 0, 0, 200});

    int fs    = sw / 24; if (fs < 14) fs = 14;
    int row_h = fs + 10;
    int cols  = 4;
    int col_w = (sw - 40) / cols;
    int tbl_x = 20;
    int tbl_y = sh / 6;

    /* title */
    const char *title = "SCORECARD";
    int tw = MeasureText(title, fs + 6);
    DrawText(title, (sw - tw)/2, tbl_y - fs - 14, fs + 6, RAYWHITE);

    /* header */
    static const char *headers[] = {"Hole", "Par", "Strokes", "+/-"};
    for (int c = 0; c < cols; c++) {
        DrawText(headers[c], tbl_x + c * col_w, tbl_y, fs,
                 (Color){200, 200, 100, 255});
    }

    /* rows */
    int total_par = 0, total_strokes = 0;
    for (int i = 0; i < HOLE_COUNT; i++) {
        int y  = tbl_y + (i + 1) * row_h + 4;
        int delta = s_strokes[i] - s_holes[i].par;
        total_par     += s_holes[i].par;
        total_strokes += s_strokes[i];

        char cols_text[4][16];
        snprintf(cols_text[0], 16, "%d", i + 1);
        snprintf(cols_text[1], 16, "%d", s_holes[i].par);
        snprintf(cols_text[2], 16, "%d", s_strokes[i]);
        snprintf(cols_text[3], 16, "%+d", delta);

        Color row_col = (delta < 0) ? (Color){100,220,100,255}
                      : (delta > 0) ? (Color){220,100,100,255}
                      : RAYWHITE;
        for (int c = 0; c < cols; c++)
            DrawText(cols_text[c], tbl_x + c * col_w, y, fs, row_col);
    }

    /* total row */
    int ty = tbl_y + (HOLE_COUNT + 1) * row_h + 12;
    int total_delta = total_strokes - total_par;
    char totals[4][16];
    snprintf(totals[0], 16, "Total");
    snprintf(totals[1], 16, "%d", total_par);
    snprintf(totals[2], 16, "%d", total_strokes);
    snprintf(totals[3], 16, "%+d", total_delta);
    for (int c = 0; c < cols; c++)
        DrawText(totals[c], tbl_x + c * col_w, ty, fs, (Color){200,200,200,255});

    /* golf term for total */
    const char *term = golf_term(total_delta);
    int gw = MeasureText(term, fs + 4);
    DrawText(term, (sw - gw)/2, ty + row_h + 16, fs + 4, RAYWHITE);

    /* restart prompt */
    const char *restart = "Tap to play again";
    int rw = MeasureText(restart, fs - 2);
    DrawText(restart, (sw - rw)/2, sh - fs * 3, fs - 2, (Color){180,180,180,220});
}

/* --------------------------------------------------------- prototype functions */

void MinigolfInit(void)
{
    generate_hole(&s_holes[0], 0x11AA, 0);
    generate_hole(&s_holes[1], 0x22BB, 1);
    generate_hole(&s_holes[2], 0x33CC, 2);

    for (int i = 0; i < HOLE_COUNT; i++) s_strokes[i] = 0;
    s_show_scorecard = false;
    s_aiming         = false;
    s_prev_pressed   = false;

    start_hole(0);
}

static void MinigolfUpdate(float dt)
{
    handle_input();
    if (!s_hole_done && !s_show_scorecard)
        integrate(dt);
    update_camera(dt);
}

static void MinigolfDraw(void)
{
    ClearBackground((Color){15, 40, 20, 255});

    BeginMode2D(s_camera);
        draw_course_background(&s_holes[s_hole_idx]);
        draw_walls(&s_holes[s_hole_idx]);

        /* pocket */
        DrawCircleV(s_holes[s_hole_idx].hole_pos,
                    s_holes[s_hole_idx].hole_r, BLACK);
        DrawCircleV(s_holes[s_hole_idx].hole_pos,
                    s_holes[s_hole_idx].hole_r * 0.55f, (Color){10,10,10,255});

        draw_tee_marker(s_holes[s_hole_idx].tee);

        if (!s_ball.sunk) {
            /* shadow */
            DrawCircleV(mg_add(s_ball.pos, (Vector2){3, 4}),
                        s_ball.radius, (Color){0,0,0,70});
            DrawCircleV(s_ball.pos, s_ball.radius, RAYWHITE);
            DrawCircleLinesV(s_ball.pos, s_ball.radius, (Color){180,180,180,200});
        }

        if (s_aiming && !s_ball.sunk)
            draw_aim();
    EndMode2D();

    draw_hud();

    if (s_hole_done && !s_show_scorecard)
        draw_hole_complete_overlay();

    if (s_show_scorecard)
        draw_scorecard();
}

static void MinigolfDeinit(void) { /* nothing allocated */ }

/* ---------------------------------------------------------------- registration */

const Prototype MinigolfProto = {
    .name        = "Minigolf",
    .description = "3-hole procedural mini golf: drag to aim, sink the putt.",
    .Init        = MinigolfInit,
    .Update      = MinigolfUpdate,
    .Draw        = MinigolfDraw,
    .Deinit      = MinigolfDeinit,
};
