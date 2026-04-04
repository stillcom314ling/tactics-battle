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

/* ---------------------------------------------------------------- loop body */

static void frame(void)
{
    float dt = GetFrameTime();

    if (app_state == STATE_MENU) {
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
        if (IsKeyPressed(KEY_ESCAPE)) {
            PROTOTYPES[active_idx]->Deinit();
            active_idx = -1;
            MenuInit(&menu, PROTOTYPES, PROTO_COUNT);
            app_state = STATE_MENU;
        } else {
            PROTOTYPES[active_idx]->Update(dt);
            BeginDrawing();
                PROTOTYPES[active_idx]->Draw();
            EndDrawing();
        }
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
