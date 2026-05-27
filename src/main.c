#include "raylib.h"
#include "proto.h"
#include "menu.h"

#include "../prototypes/balls/balls.h"
#include "../prototypes/drag_disp/drag_disp.h"
#include "../prototypes/triad/triad.h"
#include "../prototypes/realm-walk/realm_walk.h"
#include "../prototypes/pool/pool.h"
#include "../prototypes/combat/combat.h"

#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>
#endif

#ifndef GIT_VERSION
#define GIT_VERSION "dev"
#endif

/* ---------------------------------------------------------------- registry
 * To add a prototype: #include its header above, add &XxxProto below. */
static const Prototype * const PROTOTYPES[] = {
    &BallsProto,
    &DragDispProto,
    &TriadProto,
    &RealmWalkProto,
    &PoolProto,
    &CombatProto,
};
static const int PROTO_COUNT = (int)(sizeof(PROTOTYPES) / sizeof(PROTOTYPES[0]));

/* ------------------------------------------------------------------ state */

typedef enum { STATE_MENU, STATE_RUNNING } AppState;

static AppState app_state  = STATE_MENU;
static int      active_idx = -1;
static Menu     menu;

static Vector2 s_prev_touch_pos   = {-1.0f, -1.0f};
static int     s_prev_touch_count = 0;

/* ---------------------------------------------------- version badge overlay */

static void draw_version(void)
{
    int font_size = 18;
    const char *ver = "build: " GIT_VERSION;
    int w = MeasureText(ver, font_size);
    DrawText(ver,
             GetScreenWidth()  - w  - 8,
             GetScreenHeight() - font_size - 6,
             font_size, (Color){ 160, 160, 160, 140 });
}

/* --------------------------------------------------------- back button overlay
 * Returns true if the user wants to return to the menu. */
static bool draw_back_button(void)
{
    int sw   = GetScreenWidth();
    int size = sw / 9;
    int pad  = size / 5;

    Rectangle btn = {
        (float)(sw - size - pad), (float)pad,
        (float)size,              (float)size,
    };

    Vector2 mouse = GetMousePosition();
    bool hovered  = CheckCollisionPointRec(mouse, btn);

    DrawRectangleRounded(btn, 0.25f, 8,
        hovered ? (Color){220, 50, 50, 230} : (Color){20, 20, 20, 160});

    int fs = size * 55 / 100;
    const char *lbl = "x";
    int lw = MeasureText(lbl, fs);
    DrawText(lbl,
             (int)btn.x + (size - lw) / 2,
             (int)btn.y + (size - fs) / 2,
             fs, WHITE);

    /* keyboard */
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACK)) return true;

    /* Android / browser back via EM_ASM (no exported-function needed) */
#ifdef PLATFORM_WEB
    if (EM_ASM_INT({
        if (window._rlBackPressed) { window._rlBackPressed = false; return 1; }
        return 0;
    })) return true;
#endif

    /* mouse */
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(mouse, btn)) return true;

    /* touch release */
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
        s_prev_touch_count = 0;

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

    /* Set up Android/browser back-button interception. */
    EM_ASM({
        window._rlBackPressed = false;
        history.pushState({}, '');
        window.addEventListener('popstate', function () {
            window._rlBackPressed = true;
            history.pushState({}, '');
        });
    });
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
