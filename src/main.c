#include "raylib.h"
#include "proto.h"
#include "menu.h"

/* ---- Register prototypes here (one include + one entry per prototype) ---- */
#include "../prototypes/balls/balls.h"

#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>
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

/* --------------------------------------------------------- back button overlay
 * Draws a floating X button in the top-right corner.
 * Returns true if the user wants to return to the menu (tap, mouse click,
 * ESC, or Android hardware back button). */
static bool draw_back_button(void)
{
    int sw   = GetScreenWidth();
    int size = sw / 9;             /* ~120 px on a 1080-wide screen */
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

    /* keyboard / Android hardware back */
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
        /* reset touch tracker so the back button doesn't fire on first frame */
        s_prev_touch_count = 0;

        int sel = MenuUpdate(&menu);
        if (sel >= 0) {
            active_idx = sel;
            PROTOTYPES[active_idx]->Init();
            app_state = STATE_RUNNING;
        }

        BeginDrawing();
            MenuDraw(&menu);
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
