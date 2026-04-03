using System.Numerics;
using System.Runtime.InteropServices.JavaScript;
using Raylib_cs;

namespace RaylibGame;

public partial class Program
{
    private static Vector2 _ballPos;
    private static Vector2 _ballSpeed = new(200f, 150f);
    private const float BallRadius = 20f;
    private const int ScreenWidth = 800;
    private const int ScreenHeight = 450;

    public static void Main()
    {
        Raylib.InitWindow(ScreenWidth, ScreenHeight, "Hello Raylib WASM");
        Raylib.SetTargetFPS(60);
        _ballPos = new Vector2(ScreenWidth / 2f, ScreenHeight / 2f);
    }

    // Called every frame from requestAnimationFrame in main.js
    [JSExport]
    public static void DrawFrame()
    {
        float dt = Raylib.GetFrameTime();

        // Update ball
        _ballPos += _ballSpeed * dt;
        if (_ballPos.X <= BallRadius || _ballPos.X >= ScreenWidth - BallRadius)
            _ballSpeed.X *= -1;
        if (_ballPos.Y <= BallRadius || _ballPos.Y >= ScreenHeight - BallRadius)
            _ballSpeed.Y *= -1;

        // Draw
        Raylib.BeginDrawing();
        Raylib.ClearBackground(Color.RayWhite);

        Raylib.DrawCircleV(_ballPos, BallRadius, Color.Maroon);
        Raylib.DrawText("Hello World!", 270, 200, 40, Color.DarkGray);
        Raylib.DrawText("C# + Raylib-cs running in WebAssembly", 130, 260, 18, Color.Gray);
        Raylib.DrawFPS(10, 10);

        Raylib.EndDrawing();
    }
}
