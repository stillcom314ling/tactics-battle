/* Minimal raylib + raygui stubs so the Orbfall game loop can run headless.
 * Only the functions orbfall.c actually calls are implemented; drawing is a
 * no-op, input is driven by the test through the stub_* globals. */

#include "raylib.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Vector2 stub_mouse;
bool    stub_down, stub_pressed, stub_released;
int     stub_touch_count;
double  stub_time;

int GetScreenWidth(void)  { return 1080; }
int GetScreenHeight(void) { return 1920; }
double GetTime(void) { return stub_time; }
int GetFPS(void) { return 60; }

int GetTouchPointCount(void) { return stub_touch_count; }
Vector2 GetTouchPosition(int i) { (void)i; return stub_mouse; }
Vector2 GetMousePosition(void) { return stub_mouse; }
bool IsMouseButtonPressed(int b)  { (void)b; return stub_pressed; }
bool IsMouseButtonReleased(int b) { (void)b; return stub_released; }

int GetRandomValue(int min, int max)
{
    if (max <= min) return min;
    return min + rand() % (max - min + 1);
}

bool CheckCollisionPointRec(Vector2 p, Rectangle r)
{
    return p.x >= r.x && p.x <= r.x + r.width &&
           p.y >= r.y && p.y <= r.y + r.height;
}

Color Fade(Color c, float a)
{
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    c.a = (unsigned char)(255.0f * a);
    return c;
}

void TraceLog(int level, const char *fmt, ...)
{
    (void)level;
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

const char *TextFormat(const char *fmt, ...)
{
    static char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}

void *MemAlloc(unsigned int size) { return calloc(1, size); }

/* audio */
static bool s_audio_on;
void InitAudioDevice(void) { s_audio_on = true; }
bool IsAudioDeviceReady(void) { return s_audio_on; }
Sound LoadSoundFromWave(Wave w) { (void)w; Sound s = { 0 }; return s; }
Sound LoadSoundAlias(Sound s) { return s; }
void UnloadWave(Wave w) { free(w.data); }
void UnloadSound(Sound s) { (void)s; }
void UnloadSoundAlias(Sound s) { (void)s; }
void SetSoundPitch(Sound s, float p) { (void)s; (void)p; }
void PlaySound(Sound s) { (void)s; }

/* drawing: all no-ops */
void ClearBackground(Color c) { (void)c; }
int MeasureText(const char *t, int size) { return (int)strlen(t) * size / 2; }
void DrawText(const char *t, int x, int y, int s, Color c)
{ (void)t; (void)x; (void)y; (void)s; (void)c; }
void DrawCircleV(Vector2 p, float r, Color c) { (void)p; (void)r; (void)c; }
void DrawCircleLinesV(Vector2 p, float r, Color c) { (void)p; (void)r; (void)c; }
void DrawRing(Vector2 p, float ri, float ro, float a0, float a1, int seg, Color c)
{ (void)p; (void)ri; (void)ro; (void)a0; (void)a1; (void)seg; (void)c; }
void DrawPoly(Vector2 p, int sides, float r, float rot, Color c)
{ (void)p; (void)sides; (void)r; (void)rot; (void)c; }
void DrawLineEx(Vector2 a, Vector2 b, float t, Color c)
{ (void)a; (void)b; (void)t; (void)c; }
void DrawTriangle(Vector2 a, Vector2 b, Vector2 cc, Color c)
{ (void)a; (void)b; (void)cc; (void)c; }
void DrawRectangle(int x, int y, int w, int h, Color c)
{ (void)x; (void)y; (void)w; (void)h; (void)c; }
void DrawRectangleRec(Rectangle r, Color c) { (void)r; (void)c; }
void DrawRectangleRounded(Rectangle r, float rd, int s, Color c)
{ (void)r; (void)rd; (void)s; (void)c; }
void DrawRectangleRoundedLinesEx(Rectangle r, float rd, int s, float t, Color c)
{ (void)r; (void)rd; (void)s; (void)t; (void)c; }
void DrawRectanglePro(Rectangle r, Vector2 o, float rot, Color c)
{ (void)r; (void)o; (void)rot; (void)c; }

/* raygui */
void GuiSetStyle(int c, int p, int v) { (void)c; (void)p; (void)v; }
int GuiButton(Rectangle b, const char *t) { (void)b; (void)t; return 0; }
int GuiSlider(Rectangle b, const char *l, const char *r, float *v,
              float mn, float mx)
{ (void)b; (void)l; (void)r; (void)v; (void)mn; (void)mx; return 0; }
