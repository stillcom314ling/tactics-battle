using Raylib_cs;
using System.Numerics;

namespace RaylibGame;

class Program
{
    static void Main()
    {
        const int screenWidth = 800;
        const int screenHeight = 450;

        Raylib.InitWindow(screenWidth, screenHeight, "Hello Raylib-cs");
        Raylib.SetTargetFPS(60);

        // Bouncing ball state
        var ballPos = new Vector2(screenWidth / 2f, screenHeight / 2f);
        var ballSpeed = new Vector2(200f, 150f);
        const float ballRadius = 20f;

        while (!Raylib.WindowShouldClose())
        {
            float dt = Raylib.GetFrameTime();

            // Update ball
            ballPos += ballSpeed * dt;
            if (ballPos.X <= ballRadius || ballPos.X >= screenWidth - ballRadius)
                ballSpeed.X *= -1;
            if (ballPos.Y <= ballRadius || ballPos.Y >= screenHeight - ballRadius)
                ballSpeed.Y *= -1;

            // Draw
            Raylib.BeginDrawing();
            Raylib.ClearBackground(Color.RayWhite);

            Raylib.DrawCircleV(ballPos, ballRadius, Color.Maroon);

            Raylib.DrawText("Hello from Raylib-cs!", 190, 180, 30, Color.DarkGray);
            Raylib.DrawText($"Frame: {Raylib.GetTime():F1}s", 10, 10, 20, Color.Gray);
            Raylib.DrawText("Built with .NET 8 + Raylib-cs via GitHub Actions", 10, screenHeight - 30, 16, Color.Gray);

            Raylib.EndDrawing();
        }

        Raylib.CloseWindow();
    }
}
