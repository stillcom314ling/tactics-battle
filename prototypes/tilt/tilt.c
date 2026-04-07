#include "tilt.h"
#include "raylib.h"
#include <math.h>

#define BALL_RADIUS    30.0f
#define ACCEL_SCALE   150.0f  /* px/s² per accel unit — tune if too slow/fast */
#define DAMPING         0.97f /* velocity multiplier applied each frame */
#define BOUNCE          0.55f /* velocity remaining after wall collision */
#define MAX_TOUCHES       10

static Vector2 s_pos;
static Vector2 s_vel;
static Vector3 s_offset;  /* calibration: subtracted from raw accel reading */

static int s_prev_touch_ids[MAX_TOUCHES];
static int s_prev_touch_count;

/* ----------------------------------------------------------------- helpers */

static void calibrate(void)
{
    s_offset = GetAccelerometerData();
}

/* ---------------------------------------------------------- prototype impl */

static void TiltInit(void)
{
    s_pos = (Vector2){ GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f };
    s_vel = (Vector2){ 0.0f, 0.0f };
    s_offset = (Vector3){ 0.0f, 0.0f, 0.0f };
    s_prev_touch_count = 0;
    calibrate();  /* auto-calibrate so neutral tilt = ball at rest */
}

static void TiltUpdate(float dt)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    /* calibration button rect (bottom-right, screen-relative sizes) */
    int btn_w   = sw / 5;
    int btn_h   = sh / 18;
    int btn_pad = sw / 30;
    Rectangle btn = {
        (float)(sw - btn_w - btn_pad),
        (float)(sh - btn_h - btn_pad),
        (float)btn_w,
        (float)btn_h,
    };

    /* --- button tap detection ---
     * Track touch IDs frame-to-frame; a touch that is new this frame and
     * lands on the button triggers calibration (same pattern as balls.c). */
    int cur_count = GetTouchPointCount();
    int cur_ids[MAX_TOUCHES];
    Vector2 cur_pos[MAX_TOUCHES];
    for (int i = 0; i < cur_count && i < MAX_TOUCHES; i++) {
        cur_ids[i] = GetTouchPointId(i);
        cur_pos[i] = GetTouchPosition(i);
    }

    for (int c = 0; c < cur_count && c < MAX_TOUCHES; c++) {
        bool was_prev = false;
        for (int p = 0; p < s_prev_touch_count; p++) {
            if (s_prev_touch_ids[p] == cur_ids[c]) { was_prev = true; break; }
        }
        if (!was_prev && CheckCollisionPointRec(cur_pos[c], btn))
            calibrate();
    }

    s_prev_touch_count = cur_count;
    for (int i = 0; i < cur_count && i < MAX_TOUCHES; i++)
        s_prev_touch_ids[i] = cur_ids[i];

    /* mouse fallback (desktop) */
    if (cur_count == 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(GetMousePosition(), btn))
            calibrate();
    }

    /* --- accelerometer → ball acceleration ---
     * On desktop GetAccelerometerData() returns (0,0,0); arrow keys take over. */
    Vector3 accel = GetAccelerometerData();
    float ax = (accel.x - s_offset.x) * ACCEL_SCALE;
    float ay = -(accel.y - s_offset.y) * ACCEL_SCALE; /* negate: tilt forward = ball up */

    /* arrow-key fallback so the demo works without a phone */
    float key_strength = ACCEL_SCALE * 0.5f;
    if (IsKeyDown(KEY_LEFT))  ax -= key_strength;
    if (IsKeyDown(KEY_RIGHT)) ax += key_strength;
    if (IsKeyDown(KEY_UP))    ay -= key_strength;
    if (IsKeyDown(KEY_DOWN))  ay += key_strength;

    /* integrate velocity and position */
    s_vel.x += ax * dt;
    s_vel.y += ay * dt;
    s_vel.x *= DAMPING;
    s_vel.y *= DAMPING;
    s_pos.x += s_vel.x * dt;
    s_pos.y += s_vel.y * dt;

    /* wall bouncing */
    if (s_pos.x - BALL_RADIUS < 0.0f) {
        s_pos.x = BALL_RADIUS;
        s_vel.x =  fabsf(s_vel.x) * BOUNCE;
    }
    if (s_pos.x + BALL_RADIUS > (float)sw) {
        s_pos.x = (float)sw - BALL_RADIUS;
        s_vel.x = -fabsf(s_vel.x) * BOUNCE;
    }
    if (s_pos.y - BALL_RADIUS < 0.0f) {
        s_pos.y = BALL_RADIUS;
        s_vel.y =  fabsf(s_vel.y) * BOUNCE;
    }
    if (s_pos.y + BALL_RADIUS > (float)sh) {
        s_pos.y = (float)sh - BALL_RADIUS;
        s_vel.y = -fabsf(s_vel.y) * BOUNCE;
    }
}

static void TiltDraw(void)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    ClearBackground((Color){ 20, 20, 35, 255 });

    /* faint centre crosshair as a visual reference */
    Color crosshair = (Color){ 70, 70, 90, 255 };
    DrawLine(sw/2 - 18, sh/2,      sw/2 + 18, sh/2,      crosshair);
    DrawLine(sw/2,      sh/2 - 18, sw/2,      sh/2 + 18, crosshair);

    /* ball */
    DrawCircleV(s_pos, BALL_RADIUS, SKYBLUE);
    DrawCircleLinesV(s_pos, BALL_RADIUS, (Color){ 80, 160, 220, 255 });

    /* calibration button */
    int btn_w   = sw / 5;
    int btn_h   = sh / 18;
    int btn_pad = sw / 30;
    Rectangle btn = {
        (float)(sw - btn_w - btn_pad),
        (float)(sh - btn_h - btn_pad),
        (float)btn_w,
        (float)btn_h,
    };
    DrawRectangleRec(btn, (Color){ 45, 45, 65, 255 });
    DrawRectangleLinesEx(btn, 2, (Color){ 100, 100, 150, 255 });

    int fs = btn_h * 45 / 100;
    const char *lbl = "Center";
    int lw = MeasureText(lbl, fs);
    DrawText(lbl,
             (int)btn.x + (btn_w - lw) / 2,
             (int)btn.y + (btn_h - fs) / 2,
             fs, WHITE);

    /* hint */
    int hint_fs = sw / 40;
    DrawText("Tilt to move  |  Tap Center to recalibrate",
             10, 10, hint_fs, (Color){ 160, 160, 180, 200 });
}

static void TiltDeinit(void)
{
    /* nothing to free */
}

const Prototype TiltProto = {
    .name        = "Tilt Ball",
    .description = "Tilt your phone to roll the ball. Tap Center to recalibrate.",
    .Init        = TiltInit,
    .Update      = TiltUpdate,
    .Draw        = TiltDraw,
    .Deinit      = TiltDeinit,
};
