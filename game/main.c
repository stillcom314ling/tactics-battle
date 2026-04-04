#include "raylib.h"

#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>
#endif

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);

    int w = 1080, h = 1920;   /* fallback: portrait Android FHD */
#ifdef PLATFORM_WEB
    w = EM_ASM_INT({ return window.innerWidth;  });
    h = EM_ASM_INT({ return window.innerHeight; });
#endif

    InitWindow(w, h, "raylib");
    SetTargetFPS(60);

    Vector2 pos   = { w / 2.0f, h / 2.0f };
    Vector2 speed = { 300.0f, 220.0f };
    float   R     = w * 0.025f;   /* ball radius scales with screen */

    while (!WindowShouldClose())
    {
        /* update screen size after resize */
        w = GetScreenWidth();
        h = GetScreenHeight();
        R = w * 0.025f;

        float dt = GetFrameTime();
        pos.x += speed.x * dt;
        pos.y += speed.y * dt;

        if (pos.x >= w - R || pos.x <= R) speed.x *= -1.0f;
        if (pos.y >= h - R || pos.y <= R) speed.y *= -1.0f;

        BeginDrawing();
            ClearBackground(RAYWHITE);
            DrawCircleV(pos, R, MAROON);
            DrawText("Hello World!", w / 2 - 140, h / 2 - 40, 40, DARKGRAY);
            DrawText("C + raylib on WebAssembly", w / 2 - 155, h / 2 + 10, 18, GRAY);
            DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
