#include "drag_disp.h"
#include "raylib.h"
#include <math.h>

#define NUM_NODES     20
#define SPRING_K      8.0f    /* spring stiffness toward rest position        */
#define DAMPING       0.85f   /* velocity damping multiplier per frame        */
#define REPEL_RADIUS  200.0f  /* drag influence radius in pixels              */
#define REPEL_STR     1200.0f /* repulsion impulse strength                   */
#define CONNECT_DIST  240.0f  /* max distance to draw a connecting line       */

typedef struct {
    Vector2 rest;
    Vector2 pos;
    Vector2 vel;
    float   r;
    Color   col;
} Node;

static Node    s_nodes[NUM_NODES];
static Vector2 s_drag_pos;
static Vector2 s_drag_vel;
static bool    s_drag_active;
static Vector2 s_drag_prev;

static void DragDispInit(void)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    /* Arrange nodes in a 4×5 grid centred on the screen. */
    float cx = sw * 0.5f,  cy = sh * 0.5f;
    float gw = sw * 0.72f, gh = sh * 0.68f;
    int   cols = 4,        rows = 5;

    for (int i = 0; i < NUM_NODES; i++) {
        int   col = i % cols;
        int   row = i / cols;
        float x   = cx - gw * 0.5f + col * (gw / (cols - 1));
        float y   = cy - gh * 0.5f + row * (gh / (rows - 1));

        s_nodes[i].rest = (Vector2){ x, y };
        s_nodes[i].pos  = (Vector2){ x, y };
        s_nodes[i].vel  = (Vector2){ 0.0f, 0.0f };
        s_nodes[i].r    = 18.0f + (float)(i % 3) * 7.0f;

        unsigned char rb = (unsigned char)(60  + (i * 37) % 80);
        unsigned char gb = (unsigned char)(90  + (i * 53) % 70);
        unsigned char bb = (unsigned char)(180 + (i * 29) % 75);
        s_nodes[i].col = (Color){ rb, gb, bb, 230 };
    }

    s_drag_active = false;
    s_drag_pos    = (Vector2){ 0.0f, 0.0f };
    s_drag_vel    = (Vector2){ 0.0f, 0.0f };
    s_drag_prev   = (Vector2){ 0.0f, 0.0f };
}

static void DragDispUpdate(float dt)
{
    /* ---- track input ---- */
    Vector2 new_pos;
    bool    new_active;

    int tc = GetTouchPointCount();
    if (tc > 0) {
        new_pos    = GetTouchPosition(0);
        new_active = true;
    } else {
        new_pos    = GetMousePosition();
        new_active = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    }

    if (new_active && s_drag_active && dt > 0.0f) {
        s_drag_vel.x = (new_pos.x - s_drag_prev.x) / dt;
        s_drag_vel.y = (new_pos.y - s_drag_prev.y) / dt;
    } else if (!new_active) {
        s_drag_vel.x *= 0.75f;
        s_drag_vel.y *= 0.75f;
    }
    s_drag_prev   = s_drag_pos;
    s_drag_pos    = new_pos;
    s_drag_active = new_active;

    /* ---- update nodes ---- */
    for (int i = 0; i < NUM_NODES; i++) {
        Node *n = &s_nodes[i];

        /* Spring pulls node back to its rest position. */
        float ax = (n->rest.x - n->pos.x) * SPRING_K;
        float ay = (n->rest.y - n->pos.y) * SPRING_K;

        /* Repulsion from the drag point. */
        if (s_drag_active) {
            float dx   = n->pos.x - s_drag_pos.x;
            float dy   = n->pos.y - s_drag_pos.y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist < REPEL_RADIUS && dist > 0.1f) {
                float t     = 1.0f - dist / REPEL_RADIUS;
                float mag   = REPEL_STR * t * t / dist;
                /* Drag velocity boosts repulsion (cap at 3×). */
                float speed = sqrtf(s_drag_vel.x * s_drag_vel.x +
                                    s_drag_vel.y * s_drag_vel.y);
                mag *= 1.0f + fminf(speed / 300.0f, 2.0f);
                ax += dx * mag;
                ay += dy * mag;
            }
        }

        n->vel.x = (n->vel.x + ax * dt) * DAMPING;
        n->vel.y = (n->vel.y + ay * dt) * DAMPING;
        n->pos.x += n->vel.x * dt;
        n->pos.y += n->vel.y * dt;
    }
}

static void DragDispDraw(void)
{
    ClearBackground((Color){ 12, 12, 24, 255 });

    /* ---- connecting lines between nearby nodes ---- */
    for (int i = 0; i < NUM_NODES; i++) {
        for (int j = i + 1; j < NUM_NODES; j++) {
            float dx   = s_nodes[j].pos.x - s_nodes[i].pos.x;
            float dy   = s_nodes[j].pos.y - s_nodes[i].pos.y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist < CONNECT_DIST) {
                unsigned char a = (unsigned char)((1.0f - dist / CONNECT_DIST) * 110.0f);
                DrawLineEx(s_nodes[i].pos, s_nodes[j].pos, 1.5f,
                           (Color){ 100, 130, 220, a });
            }
        }
    }

    /* ---- nodes ---- */
    for (int i = 0; i < NUM_NODES; i++) {
        DrawCircleV(s_nodes[i].pos, s_nodes[i].r, s_nodes[i].col);
        DrawCircleLinesV(s_nodes[i].pos, s_nodes[i].r,
                         (Color){ 200, 220, 255, 70 });
    }

    /* ---- drag indicator ---- */
    if (s_drag_active) {
        DrawCircleLinesV(s_drag_pos, REPEL_RADIUS,
                         (Color){ 180, 180, 255, 35 });
        DrawCircleV(s_drag_pos, 10.0f, (Color){ 255, 255, 255, 160 });
    }

    /* ---- hint ---- */
    int sh = GetScreenHeight();
    DrawText("Click / drag to displace nodes", 20, sh - 44, 24,
             (Color){ 140, 150, 200, 160 });
}

static void DragDispDeinit(void) { /* nothing to free */ }

const Prototype DragDispProto = {
    .name        = "Drag & Displacement",
    .description = "Click or drag to push nodes; springs pull them back.",
    .Init        = DragDispInit,
    .Update      = DragDispUpdate,
    .Draw        = DragDispDraw,
    .Deinit      = DragDispDeinit,
};
