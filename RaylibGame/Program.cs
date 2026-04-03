using System.Numerics;
using Raylib_cs;

namespace RaylibGame;

class Program
{
    static async Task Main()
    {
        const int W = 800, H = 450;
        Raylib.InitWindow(W, H, "Hello Raylib WASM");
        Raylib.SetTargetFPS(60);

        var pos   = new Vector2(W / 2f, H / 2f);
        var speed = new Vector2(200f, 150f);
        const float R = 20f;

        while (true)   // WindowShouldClose() is always false in browser
        {
            float dt = Raylib.GetFrameTime();
            pos += speed * dt;
            if (pos.X <= R || pos.X >= W - R) speed.X *= -1;
            if (pos.Y <= R || pos.Y >= H - R) speed.Y *= -1;

            Raylib.BeginDrawing();
            Raylib.ClearBackground(Color.RayWhite);
            Raylib.DrawCircleV(pos, R, Color.Maroon);
            Raylib.DrawText("Hello World!", 270, 200, 40, Color.DarkGray);
            Raylib.DrawText("C# + Raylib-cs on WebAssembly", 175, 260, 18, Color.Gray);
            Raylib.DrawFPS(10, 10);
            Raylib.EndDrawing();

            await Task.Yield();   // yield to JS event loop each frame
        }
    }
}
