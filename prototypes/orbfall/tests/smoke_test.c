/* Headless smoke test: runs the real Orbfall game loop against stubbed
 * raylib, drives it with scripted taps + randomized drags, and checks the
 * state machine reaches a battle and resolves turns without hanging.
 * Built with ASan/UBSan by `make test-orbfall-smoke`. */

#include "../orbfall.h"

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>

extern Vector2 stub_mouse;
extern bool    stub_down, stub_pressed, stub_released;
extern int     stub_touch_count;
extern double  stub_time;

extern int OrbfallDbgState(void);
extern int OrbfallDbgPhase(void);

/* mirror of orbfall.c enums (test-only) */
enum { GS_TITLE, GS_PICK_LEADER, GS_MAP, GS_BATTLE, GS_REWARD, GS_EVENT,
       GS_REST, GS_DEFEAT, GS_VICTORY };
enum { BP_INTRO, BP_IDLE, BP_DRAG, BP_POP, BP_JACKPOT, BP_FALL,
       BP_ATTACK, BP_ENEMY, BP_WIN, BP_LOSE };

static int s_failures;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); s_failures++; } \
} while (0)

static void frame(void)
{
    stub_time += 1.0 / 60.0;
    OrbfallProto.Update(1.0f / 60.0f);
    OrbfallProto.Draw();
    stub_pressed = stub_released = false;
}

static void frames(int n) { for (int i = 0; i < n; i++) frame(); }

static void tap(float x, float y)
{
    stub_mouse = (Vector2){ x, y };
    stub_pressed = true;
    frame();
    stub_released = true;
    frame();
    frames(2);
}

/* drag from one board cell through a few cells, then release */
static void drag_path(int r0, int c0, int steps)
{
    float cs = 1080.0f / 6.0f;
    float bt = 1920.0f - cs * 5.0f;
    float x = cs * ((float)c0 + 0.5f);
    float y = bt + cs * ((float)r0 + 0.5f);
    stub_mouse = (Vector2){ x, y };
    stub_pressed = true;
    frame();
    for (int i = 0; i < steps; i++) {
        int dir = rand() % 4;
        if (dir == 0) x += cs;
        if (dir == 1) x -= cs;
        if (dir == 2) y += cs;
        if (dir == 3) y -= cs;
        if (x < cs * 0.5f) x = cs * 0.5f;
        if (x > 1080.0f - cs * 0.5f) x = 1080.0f - cs * 0.5f;
        if (y < bt + cs * 0.5f) y = bt + cs * 0.5f;
        if (y > 1920.0f - cs * 0.5f) y = 1920.0f - cs * 0.5f;
        stub_mouse = (Vector2){ x, y };
        frames(3);
    }
    stub_released = true;
    frame();
}

/* run frames until the battle phase settles back to idle/terminal */
static bool settle(int max_frames)
{
    for (int i = 0; i < max_frames; i++) {
        frame();
        int st = OrbfallDbgState();
        if (st != GS_BATTLE) return true;
        int bp = OrbfallDbgPhase();
        if (bp == BP_IDLE) return true;
    }
    return false;
}

int main(int argc, char **argv)
{
    srand(argc > 1 ? (unsigned)atoi(argv[1]) : 4242);

    OrbfallProto.Init();
    CHECK(OrbfallDbgState() == GS_TITLE, "starts at title");

    tap(540, 960);
    CHECK(OrbfallDbgState() == GS_PICK_LEADER, "title tap -> leader pick");

    /* tap the first leader card: card_rect(0, 6) geometry */
    {
        float h = 1920.0f * 0.115f;
        float total = 6.0f * (h + 1920.0f * 0.02f);
        float y0 = 1920.0f * 0.56f - total * 0.5f;
        tap(540, y0 + h * 0.5f);
    }
    CHECK(OrbfallDbgState() == GS_MAP, "leader pick -> map");

    /* play up to the whole run with random-ish input */
    int battles_seen = 0, turns_played = 0;
    for (int guard = 0; guard < 4000 && turns_played < 120; guard++) {
        int st = OrbfallDbgState();
        if (st == GS_MAP) {
            /* try tapping every possible node slot on the next floor */
            bool moved = false;
            for (int f = 0; f < 8 && !moved; f++) {
                for (int c = 0; c < 3 && !moved; c++) {
                    float x0 = 1080.0f * 0.5f - 2.0f * 1080.0f * 0.16f;
                    /* try all three count-layouts since count is seeded */
                    for (int count = 1; count <= 3 && !moved; count++) {
                        float xx = 1080.0f * 0.5f
                                 - (float)(count - 1) * 1080.0f * 0.16f
                                 + (float)c * 1080.0f * 0.32f;
                        float yy = 1920.0f * 0.86f
                                 - (float)f * (1920.0f * 0.72f / 7.0f);
                        (void)x0;
                        tap(xx, yy);
                        if (OrbfallDbgState() != GS_MAP) moved = true;
                    }
                }
            }
            if (!moved) { CHECK(false, "stuck on map"); break; }
        } else if (st == GS_BATTLE) {
            if (OrbfallDbgPhase() == BP_INTRO) { frames(40); continue; }
            if (OrbfallDbgPhase() == BP_IDLE) {
                battles_seen++;
                drag_path(rand() % 5, rand() % 6, 6 + rand() % 10);
                bool ok = settle(60 * 30);
                CHECK(ok, "battle turn resolves without hanging");
                turns_played++;
                if (!ok) break;
            } else {
                frames(10);
            }
        } else if (st == GS_REWARD) {
            float h = 1920.0f * 0.115f;
            float total = 4.0f * (h + 1920.0f * 0.02f);
            float y0 = 1920.0f * 0.56f - total * 0.5f;
            tap(540, y0 + h * 0.5f);           /* take first relic */
            CHECK(OrbfallDbgState() != GS_REWARD, "reward tap advances");
        } else if (st == GS_EVENT || st == GS_REST) {
            float h = 1920.0f * 0.115f;
            float total = 3.0f * (h + 1920.0f * 0.02f);
            float y0 = 1920.0f * 0.56f - total * 0.5f;
            tap(540, y0 + 1.0f * (h + 1920.0f * 0.02f) + h * 0.5f);
            CHECK(OrbfallDbgState() != GS_EVENT, "event choice advances");
        } else if (st == GS_DEFEAT || st == GS_VICTORY) {
            printf("run ended: %s after %d turns, %d battle turns seen\n",
                   st == GS_VICTORY ? "VICTORY" : "defeat",
                   turns_played, battles_seen);
            break;
        } else {
            frames(5);
        }
    }

    CHECK(battles_seen >= 3, "fought at least a few turns");
    printf("smoke: %d battle turns across the run\n", turns_played);

    OrbfallProto.Deinit();

    if (s_failures == 0) { printf("smoke: all checks passed\n"); return 0; }
    printf("smoke: %d FAILURES\n", s_failures);
    return 1;
}
