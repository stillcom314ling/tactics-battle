#include "raylib.h"
#include "proto.h"
#include "menu.h"

/* ---- Register prototypes here (one include + one entry per prototype) ---- */
#include "../prototypes/balls/balls.h"

#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>
#ifndef EMSCRIPTEN_KEEPALIVE
#define EMSCRIPTEN_KEEPALIVE
#endif
#endif

#ifndef GIT_VERSION
#define GIT_VERSION "dev"
#endif

/* ---------------------------------------------------------------- registry */

/*
 * To add a new prototype:
 *   1. #include its header above
 *   2. Add &YourProto to PROTOTYPES below
 */
static const Prototype * const PROTOTYPES[] = {
    &BallsProto,
};

static const int PROTO_COUNT = (int)(sizeof(PROTOTYPES) / sizeof(PROTOTYPES[0]));

/* ------------------------------------------------------------------ state */

typedef enum { STATE_MENU, STATE_RUNNING } AppState;

static AppState app_state  = STATE_MENU;
static int      active_idx = -1;
static Menu     menu;

/* touch tracking for the back button (release detection) */
static Vector2 s_prev_touch_pos   = {-1.0f, -1.0f};
static int     s_prev_touch_count = 0;

/* set to 1 by JS on_browser_back() when the hardware/browser back is pressed */
static int s_back_requested = 0;

#ifdef PLATFORM_WEB
/* Called from JavaScript (History API popstate) to signal a back gesture. */
EMSCRIPTEN_KEEPALIVE
void on_browser_back(void) { s_back_requested = 1; }
#endif

/* ---------------------------------------------------- version badge overlay */

static void draw_version(void)
{
    int sh        = GetScreenHeight();
    int font_size = 18;
    const char *ver = "build: " GIT_VERSION;
    int text_w = MeasureText(ver, font_size);
    DrawText(ver,
             GetScreenWidth() - text_w - 8,
             sh - font_size - 6,
             font_size,
             (Color){ 160, 160, 160, 140 });
}

/* --------------------------------------------------------- back button overlay
 * Draws a floating X button in the top-right corner.
 * Returns true if the user wants to return to the menu. */
static bool draw_back_button(void)
{
    int sw   = GetScreenWidth();
    int size = sw / 9;
    int pad  = size / 5;

    Rectangle btn = {
        (float)(sw - size - pad),
        (float)pad,
        (float)size,
        (float)size,
    };

    /* ---- draw ---- */
    Vector2 mouse = GetMousePosition();
    bool hovered  = CheckCollisionPointRec(mouse, btn);

    Color bg = hovered
        ? (Color){220,  50,  50, 230}
        : (Color){ 20,  20,  20, 160};

    DrawRectangleRounded(btn, 0.25f, 8, bg);

    int font_size = size * 55 / 100;
    const char *label = "x";
    int text_w = MeasureText(label, font_size);
    DrawText(label,
             (int)btn.x + (size - text_w) / 2,
             (int)btn.y + (size - font_size) / 2,
             font_size, WHITE);

    /* ---- input ---- */

    /* browser / Android hardware back (set by JS) */
    if (s_back_requested) { s_back_requested = 0; return true; }

    /* keyboard fallback */
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACK)) return true;

    /* mouse click release on button */
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(mouse, btn)) return true;

    /* touch tap: fire when the last touch lifts inside the button */
    int tc = GetTouchPointCount();
    if (tc > 0) s_prev_touch_pos = GetTouchPosition(0);
    bool tapped = (tc == 0 && s_prev_touch_count > 0 &&
                   CheckCollisionPointRec(s_prev_touch_pos, btn));
    s_prev_touch_count = tc;

    return tapped;
}

/* ---------------------------------------------------------------- loop body */

static void frame(void)
{
    float dt = GetFrameTime();

    if (app_state == STATE_MENU) {
        s_prev_touch_count = 0; /* reset so back button can't fire immediately */
        s_back_requested   = 0; /* ignore back presses while in the menu */

        int sel = MenuUpdate(&menu);
        if (sel >= 0) {
            active_idx = sel;
            PROTOTYPES[active_idx]->Init();
            app_state = STATE_RUNNING;
        }

        BeginDrawing();
            MenuDraw(&menu);
            draw_version();
        EndDrawing();

    } else {
        PROTOTYPES[active_idx]->Update(dt);

        BeginDrawing();
            PROTOTYPES[active_idx]->Draw();
            if (draw_back_button()) {
                PROTOTYPES[active_idx]->Deinit();
                active_idx = -1;
                MenuInit(&menu, PROTOTYPES, PROTO_COUNT);
                app_state = STATE_MENU;
            }
            draw_version();
        EndDrawing();
    }
}

/* -------------------------------------------------------------------  main */

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);

    int w = 1080, h = 1920;
#ifdef PLATFORM_WEB
    w = EM_ASM_INT({ return window.innerWidth;  });
    h = EM_ASM_INT({ return window.innerHeight; });
#endif

    InitWindow(w, h, "Prototype Launcher");
    SetTargetFPS(60);

    MenuInit(&menu, PROTOTYPES, PROTO_COUNT);

#ifdef PLATFORM_WEB
    emscripten_set_main_loop(frame, 0, 1);
#else
    while (!WindowShouldClose()) frame();
    CloseWindow();
#endif

    return 0;
}
