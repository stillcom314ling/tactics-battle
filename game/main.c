#include "raylib.h"

int main(void)
{
    const int screenWidth  = 800;
    const int screenHeight = 450;

    InitWindow(screenWidth, screenHeight, "raylib - bouncing ball");
    SetTargetFPS(60);

    Vector2 pos    = { screenWidth / 2.0f, screenHeight / 2.0f };
    Vector2 speed  = { 200.0f, 150.0f };
    const float R  = 20.0f;

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        pos.x += speed.x * dt;
        pos.y += speed.y * dt;

        if (pos.x >= screenWidth  - R || pos.x <= R) speed.x *= -1.0f;
        if (pos.y >= screenHeight - R || pos.y <= R) speed.y *= -1.0f;

        BeginDrawing();
            ClearBackground(RAYWHITE);
            DrawCircleV(pos, R, MAROON);
            DrawText("Hello World!", 270, 200, 40, DARKGRAY);
            DrawText("C + raylib on WebAssembly", 195, 260, 18, GRAY);
            DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
