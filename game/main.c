#include "raylib.h"
#include <math.h>
#include <string.h>

#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>
#endif

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
    int     ballIdx;    /* -1 = not on a ball */
    Vector2 startPos;
    Vector2 curPos;
    double  downTime;
} TouchSlot;

static Ball      balls[MAX_BALLS];
static TouchSlot slots[MAX_TOUCHES];

static int    prevIds[MAX_TOUCHES];
static int    prevCount = 0;

/* double-tap state */
static int    dtBall = -1;
static double dtTime = -1.0;

/* ------------------------------------------------------------------ helpers */

static Color randColor(void)
{
    static Color pal[] = {
        MAROON, DARKBLUE, DARKGREEN, PURPLE, ORANGE,
        RED, BLUE, GREEN, VIOLET, GOLD, PINK, SKYBLUE
    };
    return pal[GetRandomValue(0, 11)];
}

static int ballAt(Vector2 p)
{
    for (int i = 0; i < MAX_BALLS; i++) {
        if (!balls[i].active) continue;
        float dx = balls[i].pos.x - p.x, dy = balls[i].pos.y - p.y;
        if (dx*dx + dy*dy <= balls[i].radius * balls[i].radius) return i;
    }
    return -1;
}

static void addBall(Vector2 pos)
{
    for (int i = 0; i < MAX_BALLS; i++) {
        if (balls[i].active) continue;
        float r = GetRandomValue(28, 60);
        balls[i] = (Ball){
            .pos    = pos,
            .vel    = { (float)GetRandomValue(-200, 200), (float)GetRandomValue(-100, 0) },
            .radius = r,
            .color  = randColor(),
            .active = true,
        };
        return;
    }
}

static TouchSlot* findSlot(int id)
{
    for (int i = 0; i < MAX_TOUCHES; i++)
        if (slots[i].active && slots[i].id == id) return &slots[i];
    return NULL;
}

/* ------------------------------------------------------------------ events */

static void onDown(int id, Vector2 pos)
{
    for (int i = 0; i < MAX_TOUCHES; i++) {
        if (slots[i].active) continue;
        slots[i] = (TouchSlot){
            .id       = id,
            .active   = true,
            .dragging = false,
            .ballIdx  = ballAt(pos),
            .startPos = pos,
            .curPos   = pos,
            .downTime = GetTime(),
        };
        return;
    }
}

static void onMove(int id, Vector2 pos)
{
    TouchSlot* s = findSlot(id);
    if (!s) return;
    float dx = pos.x - s->startPos.x, dy = pos.y - s->startPos.y;
    if (!s->dragging && sqrtf(dx*dx + dy*dy) > DRAG_THRESHOLD)
        s->dragging = true;
    s->curPos = pos;
}

static void onUp(int id)
{
    TouchSlot* s = findSlot(id);
    if (!s) return;

    if (!s->dragging) {
        int b = ballAt(s->curPos);
        if (b >= 0) {
            /* tap on a ball — double-tap check */
            double now = GetTime();
            if (dtBall == b && (now - dtTime) < DOUBLE_TAP_SECS) {
                balls[b].active = false;
                dtBall = -1;
            } else {
                dtBall = b;
                dtTime = now;
            }
        } else {
            /* tap on empty space — spawn ball */
            addBall(s->curPos);
            dtBall = -1;
        }
    }

    s->active = false;
}

/* ================================================================== main == */

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);

    int w = 1080, h = 1920;
#ifdef PLATFORM_WEB
    w = EM_ASM_INT({ return window.innerWidth;  });
    h = EM_ASM_INT({ return window.innerHeight; });
#endif

    InitWindow(w, h, "raylib balls");
    SetTargetFPS(60);

    addBall((Vector2){ w / 2.0f, h / 3.0f });

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        w = GetScreenWidth();
        h = GetScreenHeight();

        /* ---- touch input ---- */
        int curCount = GetTouchPointCount();
        int     curIds[MAX_TOUCHES];
        Vector2 curPos[MAX_TOUCHES];
        for (int i = 0; i < curCount && i < MAX_TOUCHES; i++) {
            curIds[i] = GetTouchPointId(i);
            curPos[i] = GetTouchPosition(i);
        }

        /* releases: was in prev, gone now */
        for (int p = 0; p < prevCount; p++) {
            bool found = false;
            for (int c = 0; c < curCount; c++)
                if (curIds[c] == prevIds[p]) { found = true; break; }
            if (!found) onUp(prevIds[p]);
        }
        /* new & moved */
        for (int c = 0; c < curCount && c < MAX_TOUCHES; c++) {
            if (!findSlot(curIds[c])) onDown(curIds[c], curPos[c]);
            else                      onMove(curIds[c], curPos[c]);
        }
        prevCount = curCount;
        for (int i = 0; i < curCount && i < MAX_TOUCHES; i++)
            prevIds[i] = curIds[i];

        /* ---- mouse fallback (desktop) ---- */
        if (curCount == 0) {
            Vector2 mp = GetMousePosition();
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))   onDown(0, mp);
            else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) onMove(0, mp);
            else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) onUp(0);
        }

        /* ---- physics ---- */
        for (int i = 0; i < MAX_BALLS; i++) {
            if (!balls[i].active) continue;

            TouchSlot* dragger = NULL;
            for (int t = 0; t < MAX_TOUCHES; t++) {
                if (slots[t].active && slots[t].ballIdx == i && slots[t].dragging) {
                    dragger = &slots[t]; break;
                }
            }
            /* also check mouse slot (id 0) */
            if (!dragger) {
                TouchSlot* ms = findSlot(0);
                if (ms && ms->ballIdx == i && ms->dragging) dragger = ms;
            }

            if (dragger) {
                Vector2 prev = balls[i].pos;
                balls[i].pos = dragger->curPos;
                if (dt > 0.0f) {
                    balls[i].vel.x = (balls[i].pos.x - prev.x) / dt;
                    balls[i].vel.y = (balls[i].pos.y - prev.y) / dt;
                }
            } else {
                balls[i].vel.y += GRAVITY * dt;

                balls[i].pos.x += balls[i].vel.x * dt;
                balls[i].pos.y += balls[i].vel.y * dt;

                float r = balls[i].radius;
                if (balls[i].pos.x < r)   { balls[i].pos.x = r;   balls[i].vel.x =  fabsf(balls[i].vel.x) * 0.75f; }
                if (balls[i].pos.x > w-r) { balls[i].pos.x = w-r; balls[i].vel.x = -fabsf(balls[i].vel.x) * 0.75f; }
                if (balls[i].pos.y < r)   { balls[i].pos.y = r;   balls[i].vel.y =  fabsf(balls[i].vel.y) * 0.75f; }
                if (balls[i].pos.y > h-r) { balls[i].pos.y = h-r; balls[i].vel.y = -fabsf(balls[i].vel.y) * 0.75f; }
            }
        }

        /* ---- draw ---- */
        BeginDrawing();
            ClearBackground(RAYWHITE);

            for (int i = 0; i < MAX_BALLS; i++) {
                if (!balls[i].active) continue;
                DrawCircleV(balls[i].pos, balls[i].radius, balls[i].color);
                DrawCircleLinesV(balls[i].pos, balls[i].radius,
                                 ColorBrightness(balls[i].color, -0.3f));
            }

            DrawText("Tap: add  |  Drag: move  |  Double-tap: delete",
                     10, 10, 22, DARKGRAY);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
