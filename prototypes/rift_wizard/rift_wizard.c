#include "rift_wizard.h"
#include "raylib.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

// ── Grid / limits ─────────────────────────────────────────────────────────────
#define GRID_W      55
#define GRID_H      26
#define MAX_EN      18
#define MAX_PAR     300
#define MAX_MSG     6
#define MSG_LEN     72
#define REGEN_SEC   3.0f
#define FBALL_RAD   2

// ── Spell indices ─────────────────────────────────────────────────────────────
#define SP_MISSILE  0
#define SP_FIREBALL 1
#define SP_BLINK    2
#define NUM_SPELLS  3

static const int  SP_COST[NUM_SPELLS]  = {3, 6, 4};
static const char *SP_NAMES[NUM_SPELLS] = {"Magic Missile", "Fireball", "Blink"};
static const char *SP_FULL[NUM_SPELLS]  = {
    "[1] Magic Missile  3mp",
    "[2] Fireball       6mp",
    "[3] Blink          4mp",
};

// ── Types ─────────────────────────────────────────────────────────────────────
typedef struct { int x, y; } IV2;

typedef struct {
    int x, y, hp, max_hp, mana, max_mana, spell;
} Player;

typedef struct {
    bool        alive;
    int         x, y, hp, max_hp, atk, slow, slow_cd;
    char        glyph;
    Color       col;
    const char *name;
} Enemy;

typedef struct {
    float x, y, vx, vy, life, max_life;
    char  glyph;
    Color col;
} Particle;

// ── Room layout (file-scope constant) ─────────────────────────────────────────
typedef struct { int x, y, w, h; } Rect;
#define NROOMS 6
static const Rect ROOMS[NROOMS] = {
    { 2,  1, 11, 7},   // 0: player start
    {20,  1, 11, 6},   // 1
    {39,  1, 14, 7},   // 2
    { 2, 16, 11, 8},   // 3
    {21, 16, 11, 8},   // 4: stairs
    {40, 16, 13, 8},   // 5
};

// ── State ─────────────────────────────────────────────────────────────────────
static char     s_map[GRID_H][GRID_W];
static Player   s_pl;
static Enemy    s_en[MAX_EN];
static int      s_en_n;
static Particle s_par[MAX_PAR];
static int      s_par_n;
static char     s_msg[MAX_MSG][MSG_LEN];
static int      s_msg_n;
static bool     s_targeting;
static bool     s_game_over;
static bool     s_won;
static float    s_regen_t;
static bool     s_hl[GRID_H][GRID_W];
static int      s_cell;   // pixel size of one grid cell, set in Init
static int      s_ox;     // pixel x of grid top-left corner
static int      s_oy;     // pixel y of grid top-left corner

// ── Utilities ─────────────────────────────────────────────────────────────────
static bool wall_at(int x, int y) {
    if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H) return true;
    return s_map[y][x] == '#';
}

static int en_at(int x, int y) {
    for (int i = 0; i < s_en_n; i++)
        if (s_en[i].alive && s_en[i].x == x && s_en[i].y == y) return i;
    return -1;
}

static void push_msg(const char *fmt, ...) {
    char buf[MSG_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (s_msg_n < MAX_MSG) {
        strncpy(s_msg[s_msg_n], buf, MSG_LEN - 1);
        s_msg[s_msg_n][MSG_LEN - 1] = '\0';
        s_msg_n++;
    } else {
        memmove(s_msg[0], s_msg[1], (size_t)MSG_LEN * (MAX_MSG - 1));
        strncpy(s_msg[MAX_MSG - 1], buf, MSG_LEN - 1);
        s_msg[MAX_MSG - 1][MSG_LEN - 1] = '\0';
    }
}

static IV2 mouse_to_grid(void) {
    Vector2 m = GetMousePosition();
    return (IV2){(int)((m.x - s_ox) / s_cell), (int)((m.y - s_oy) / s_cell)};
}

// ── Particles ─────────────────────────────────────────────────────────────────
static void spawn_par(float x, float y, char g, Color c,
                      float vx, float vy, float life) {
    if (s_par_n >= MAX_PAR) return;
    s_par[s_par_n++] = (Particle){x, y, vx, vy, life, life, g, c};
}

static void burst(int gx, int gy, char g, Color c, int n, float spd, float life) {
    for (int i = 0; i < n; i++) {
        float a = (float)i / (float)n * 6.2832f;
        spawn_par((float)gx + 0.5f, (float)gy + 0.5f, g, c,
                  cosf(a) * spd, sinf(a) * spd, life);
    }
}

static void update_particles(float dt) {
    for (int i = 0; i < s_par_n;) {
        Particle *p = &s_par[i];
        p->x    += p->vx * dt;
        p->y    += p->vy * dt;
        p->life -= dt;
        if (p->life <= 0.0f) s_par[i] = s_par[--s_par_n];
        else                 i++;
    }
}

// ── Map ───────────────────────────────────────────────────────────────────────
static void carve_rect(Rect r) {
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++)
            if (x >= 0 && x < GRID_W && y >= 0 && y < GRID_H)
                s_map[y][x] = '.';
}

static void carve_h(int x1, int x2, int y) {
    if (y < 0 || y >= GRID_H) return;
    int lo = x1 < x2 ? x1 : x2, hi = x1 < x2 ? x2 : x1;
    for (int x = lo; x <= hi; x++)
        if (x >= 0 && x < GRID_W) s_map[y][x] = '.';
}

static void carve_v(int x, int y1, int y2) {
    if (x < 0 || x >= GRID_W) return;
    int lo = y1 < y2 ? y1 : y2, hi = y1 < y2 ? y2 : y1;
    for (int y = lo; y <= hi; y++)
        if (y >= 0 && y < GRID_H) s_map[y][x] = '.';
}

static void gen_map(void) {
    memset(s_map, '#', sizeof(s_map));
    for (int i = 0; i < NROOMS; i++) carve_rect(ROOMS[i]);

    // Connect rooms sequentially with L-shaped corridors
    for (int i = 0; i < NROOMS - 1; i++) {
        int cx1 = ROOMS[i].x   + ROOMS[i].w   / 2;
        int cy1 = ROOMS[i].y   + ROOMS[i].h   / 2;
        int cx2 = ROOMS[i+1].x + ROOMS[i+1].w / 2;
        int cy2 = ROOMS[i+1].y + ROOMS[i+1].h / 2;
        carve_h(cx1, cx2, cy1);
        carve_v(cx2, cy1, cy2);
    }
    // Extra vertical link: room 0 → room 3
    {
        int cx1 = ROOMS[0].x + ROOMS[0].w / 2, cy1 = ROOMS[0].y + ROOMS[0].h / 2;
        int cx2 = ROOMS[3].x + ROOMS[3].w / 2, cy2 = ROOMS[3].y + ROOMS[3].h / 2;
        carve_v(cx1, cy1, cy2);
        carve_h(cx1, cx2, cy2);
    }

    // Stairs in room 4 center
    s_map[ROOMS[4].y + ROOMS[4].h / 2][ROOMS[4].x + ROOMS[4].w / 2] = '>';
}

// ── Enemies ───────────────────────────────────────────────────────────────────
#define ETYPE_GOBLIN  0
#define ETYPE_SHADOW  1
#define ETYPE_DEMON   2

static void spawn_en(int x, int y, int t) {
    if (s_en_n >= MAX_EN) return;
    Enemy *e = &s_en[s_en_n++];
    *e = (Enemy){0};
    e->alive = true;
    e->x = x; e->y = y;
    switch (t) {
    case ETYPE_GOBLIN:
        e->glyph = 'g'; e->col = (Color){80, 200, 80, 255};
        e->hp = e->max_hp = 10; e->atk = 2; e->slow = 0;
        e->name = "Goblin"; break;
    case ETYPE_SHADOW:
        e->glyph = 's'; e->col = (Color){160, 80, 220, 255};
        e->hp = e->max_hp = 16; e->atk = 4; e->slow = 0;
        e->name = "Shadow"; break;
    case ETYPE_DEMON:
        e->glyph = 'D'; e->col = (Color){220, 50, 50, 255};
        e->hp = e->max_hp = 28; e->atk = 7; e->slow = 1;
        e->name = "Demon"; break;
    }
}

static void place_enemies(void) {
    s_en_n = 0;
    spawn_en(ROOMS[1].x + 2, ROOMS[1].y + 2, ETYPE_GOBLIN);
    spawn_en(ROOMS[1].x + 7, ROOMS[1].y + 3, ETYPE_GOBLIN);
    spawn_en(ROOMS[2].x + 3, ROOMS[2].y + 3, ETYPE_SHADOW);
    spawn_en(ROOMS[2].x + 9, ROOMS[2].y + 2, ETYPE_GOBLIN);
    spawn_en(ROOMS[3].x + 3, ROOMS[3].y + 3, ETYPE_GOBLIN);
    spawn_en(ROOMS[3].x + 7, ROOMS[3].y + 5, ETYPE_SHADOW);
    spawn_en(ROOMS[4].x + 3, ROOMS[4].y + 3, ETYPE_DEMON);
    spawn_en(ROOMS[5].x + 3, ROOMS[5].y + 3, ETYPE_DEMON);
    spawn_en(ROOMS[5].x + 9, ROOMS[5].y + 5, ETYPE_SHADOW);
}

// ── Combat ────────────────────────────────────────────────────────────────────
static void player_damage(int dmg) {
    s_pl.hp -= dmg;
    if (s_pl.hp <= 0) {
        s_pl.hp = 0;
        s_game_over = true;
        push_msg("You die... Press R to restart.");
    }
}

static void en_damage(int idx, int dmg) {
    Enemy *e = &s_en[idx];
    e->hp -= dmg;
    if (e->hp <= 0) {
        e->alive = false;
        push_msg("The %s is slain!", e->name);
        burst(e->x, e->y, '*', e->col, 10, 4.0f, 0.5f);
    }
}

// ── Enemy AI ──────────────────────────────────────────────────────────────────
static void do_enemy_turns(void) {
    for (int i = 0; i < s_en_n; i++) {
        Enemy *e = &s_en[i];
        if (!e->alive) continue;
        if (e->slow_cd > 0) { e->slow_cd--; continue; }
        e->slow_cd = e->slow;

        int dx = s_pl.x - e->x, dy = s_pl.y - e->y;
        int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;

        if (adx + ady == 1) {
            push_msg("The %s strikes you for %d!", e->name, e->atk);
            player_damage(e->atk);
            burst(s_pl.x, s_pl.y, '!', RED, 6, 3.0f, 0.25f);
        } else {
            // Try preferred axis first, then alternate
            int mx = 0, my = 0;
            if (adx >= ady) mx = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
            else            my = dy > 0 ? 1 : (dy < 0 ? -1 : 0);

            int nx = e->x + mx, ny = e->y + my;
            bool ok = !wall_at(nx, ny) && en_at(nx, ny) < 0
                      && !(nx == s_pl.x && ny == s_pl.y);
            if (!ok) {
                // Slide to alternate axis
                if (mx != 0) { mx = 0; my = dy > 0 ? 1 : (dy < 0 ? -1 : 0); }
                else         { my = 0; mx = dx > 0 ? 1 : (dx < 0 ? -1 : 0); }
                nx = e->x + mx; ny = e->y + my;
                ok = (mx != 0 || my != 0) && !wall_at(nx, ny)
                     && en_at(nx, ny) < 0
                     && !(nx == s_pl.x && ny == s_pl.y);
            }
            if (ok) { e->x = nx; e->y = ny; }
        }
    }
}

// ── Spell highlight preview ───────────────────────────────────────────────────
static void compute_hl(IV2 t) {
    memset(s_hl, 0, sizeof(s_hl));
    if (t.x < 0 || t.x >= GRID_W || t.y < 0 || t.y >= GRID_H) return;

    int px = s_pl.x, py = s_pl.y, tx = t.x, ty = t.y;

    switch (s_pl.spell) {
    case SP_MISSILE: {
        // Bresenham ray; stop at wall or first enemy
        int x = px, y = py;
        int ddx = tx - px, ddy = ty - py;
        int adx = ddx < 0 ? -ddx : ddx, ady = ddy < 0 ? -ddy : ddy;
        int sx  = ddx > 0 ? 1 : -1,     sy  = ddy > 0 ? 1 : -1;
        int err = adx - ady;
        for (;;) {
            if (x != px || y != py) {
                if (wall_at(x, y)) break;
                s_hl[y][x] = true;
                if (en_at(x, y) >= 0) break;
            }
            if (x == tx && y == ty) break;
            int e2 = 2 * err;
            if (e2 > -ady) { err -= ady; x += sx; }
            if (e2 <  adx) { err += adx; y += sy; }
        }
        break;
    }
    case SP_FIREBALL:
        for (int ry = ty - FBALL_RAD; ry <= ty + FBALL_RAD; ry++)
            for (int rx = tx - FBALL_RAD; rx <= tx + FBALL_RAD; rx++) {
                int ddx = rx - tx, ddy = ry - ty;
                if (ddx*ddx + ddy*ddy <= FBALL_RAD*FBALL_RAD + 1
                    && rx >= 0 && rx < GRID_W && ry >= 0 && ry < GRID_H
                    && !wall_at(rx, ry))
                    s_hl[ry][rx] = true;
            }
        break;
    case SP_BLINK:
        if (!wall_at(tx, ty) && en_at(tx, ty) < 0)
            s_hl[ty][tx] = true;
        break;
    }
}

// ── Casting ───────────────────────────────────────────────────────────────────
// Returns true if the player spent their turn (enemies should act).
static bool try_cast(IV2 t) {
    int tx = t.x, ty = t.y;

    if (s_pl.mana < SP_COST[s_pl.spell]) {
        push_msg("Not enough mana!"); return false;
    }

    switch (s_pl.spell) {
    case SP_MISSILE: {
        s_pl.mana -= SP_COST[SP_MISSILE];
        int x = s_pl.x, y = s_pl.y;
        int ddx = tx - x, ddy = ty - y;
        int adx = ddx < 0 ? -ddx : ddx, ady = ddy < 0 ? -ddy : ddy;
        int sx  = ddx > 0 ? 1 : -1,     sy  = ddy > 0 ? 1 : -1;
        int err = adx - ady;
        int hit = -1;
        for (int step = 0; step < 64; step++) {
            if (x != s_pl.x || y != s_pl.y) {
                if (wall_at(x, y)) break;
                spawn_par((float)x + 0.3f, (float)y + 0.3f,
                          '*', (Color){255, 220, 80, 255}, 0, 0,
                          0.12f + step * 0.018f);
                hit = en_at(x, y);
                if (hit >= 0) break;
            }
            if (x == tx && y == ty) break;
            int e2 = 2 * err;
            if (e2 > -ady) { err -= ady; x += sx; }
            if (e2 <  adx) { err += adx; y += sy; }
        }
        if (hit >= 0) {
            int dmg = 8 + GetRandomValue(0, 6);
            push_msg("Magic Missile hits the %s for %d!", s_en[hit].name, dmg);
            en_damage(hit, dmg);
            burst(s_en[hit].x, s_en[hit].y, '*',
                  (Color){255, 210, 60, 255}, 8, 5.0f, 0.4f);
        } else {
            push_msg("Magic Missile dissipates.");
        }
        return true;
    }
    case SP_FIREBALL: {
        s_pl.mana -= SP_COST[SP_FIREBALL];
        int hits = 0;
        for (int i = 0; i < s_en_n; i++) {
            if (!s_en[i].alive) continue;
            int ddx = s_en[i].x - tx, ddy = s_en[i].y - ty;
            if (ddx*ddx + ddy*ddy <= FBALL_RAD*FBALL_RAD + 1) {
                int dmg = 12 + GetRandomValue(0, 8);
                push_msg("Fireball blasts %s for %d!", s_en[i].name, dmg);
                en_damage(i, dmg);
                hits++;
            }
        }
        if (!hits) push_msg("Fireball scorches empty ground.");
        burst(tx, ty, '*', (Color){255, 140, 20, 255}, 24, 7.0f, 0.7f);
        burst(tx, ty, '.', (Color){255, 60,  0, 255},  16, 4.0f, 0.4f);
        return true;
    }
    case SP_BLINK:
        if (wall_at(tx, ty))    { push_msg("Cannot blink there!");    return false; }
        if (en_at(tx, ty) >= 0) { push_msg("Blink path is blocked!"); return false; }
        s_pl.mana -= SP_COST[SP_BLINK];
        burst(s_pl.x, s_pl.y, '?', (Color){140, 80, 255, 255}, 8, 3.5f, 0.4f);
        s_pl.x = tx; s_pl.y = ty;
        burst(tx, ty, '*', (Color){180, 130, 255, 255}, 8, 3.5f, 0.4f);
        push_msg("You blink across the dungeon.");
        return true;
    }
    return false;
}

static void do_cast(IV2 t) {
    bool acted = try_cast(t);
    s_targeting = false;
    memset(s_hl, 0, sizeof(s_hl));
    if (acted && !s_game_over) do_enemy_turns();
}

// ── Init ──────────────────────────────────────────────────────────────────────
static void RiftWizardInit(void) {
    int cell_w = GetScreenWidth()  / (GRID_W + 2);
    int cell_h = (int)(GetScreenHeight() * 0.65f) / GRID_H;
    s_cell = cell_w < cell_h ? cell_w : cell_h;
    if (s_cell < 10) s_cell = 10;
    if (s_cell > 22) s_cell = 22;
    s_ox = (GetScreenWidth()  - GRID_W * s_cell) / 2;
    s_oy = s_cell / 2;

    s_par_n     = 0;
    s_msg_n     = 0;
    s_targeting = false;
    s_game_over = false;
    s_won       = false;
    s_regen_t   = 0.0f;
    memset(s_hl, 0, sizeof(s_hl));

    s_pl.hp = s_pl.max_hp     = 30;
    s_pl.mana = s_pl.max_mana = 20;
    s_pl.spell                = SP_MISSILE;
    s_pl.x = ROOMS[0].x + ROOMS[0].w / 2;
    s_pl.y = ROOMS[0].y + ROOMS[0].h / 2;

    gen_map();
    place_enemies();

    push_msg("The dungeon awaits. WASD: move  1/2/3: select spell  Click: cast");
}

// ── Update ────────────────────────────────────────────────────────────────────
static void RiftWizardUpdate(float dt) {
    if (s_game_over || s_won) {
        if (IsKeyPressed(KEY_R)) RiftWizardInit();
        return;
    }

    update_particles(dt);

    s_regen_t += dt;
    if (s_regen_t >= REGEN_SEC) {
        s_regen_t -= REGEN_SEC;
        if (s_pl.mana < s_pl.max_mana) s_pl.mana++;
    }

    // Spell selection (switches targeting mode)
    if (IsKeyPressed(KEY_ONE))   { s_pl.spell = SP_MISSILE;  s_targeting = true; memset(s_hl, 0, sizeof(s_hl)); }
    if (IsKeyPressed(KEY_TWO))   { s_pl.spell = SP_FIREBALL; s_targeting = true; memset(s_hl, 0, sizeof(s_hl)); }
    if (IsKeyPressed(KEY_THREE)) { s_pl.spell = SP_BLINK;    s_targeting = true; memset(s_hl, 0, sizeof(s_hl)); }

    if (s_targeting) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            s_targeting = false;
            memset(s_hl, 0, sizeof(s_hl));
            push_msg("Spell cancelled.");
            return;
        }
        IV2 mg = mouse_to_grid();
        compute_hl(mg);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
            && mg.x >= 0 && mg.x < GRID_W
            && mg.y >= 0 && mg.y < GRID_H) {
            do_cast(mg);
        }
        return;
    }

    // Movement (one step per keypress — roguelike style)
    int mdx = 0, mdy = 0;
    if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP))    mdy = -1;
    if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN))  mdy =  1;
    if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT))  mdx = -1;
    if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT)) mdx =  1;

    if (mdx != 0 || mdy != 0) {
        int nx = s_pl.x + mdx, ny = s_pl.y + mdy;
        if (!wall_at(nx, ny)) {
            int ei = en_at(nx, ny);
            if (ei >= 0) {
                int dmg = 4 + GetRandomValue(0, 3);
                push_msg("You strike the %s for %d!", s_en[ei].name, dmg);
                en_damage(ei, dmg);
                burst(nx, ny, '!', WHITE, 6, 4.0f, 0.3f);
            } else {
                s_pl.x = nx; s_pl.y = ny;
                if (s_map[ny][nx] == '>') {
                    s_won = true;
                    push_msg("You descend the stairs! Victory! Press R to play again.");
                }
            }
            if (!s_game_over && !s_won) do_enemy_turns();
        }
    }
}

// ── Draw ──────────────────────────────────────────────────────────────────────
static void draw_cell(int gx, int gy, char ch, Color c) {
    char s[2] = {ch, '\0'};
    DrawText(s, s_ox + gx * s_cell + 1, s_oy + gy * s_cell, s_cell, c);
}

static void RiftWizardDraw(void) {
    ClearBackground((Color){8, 8, 18, 255});

    // Grid border
    DrawRectangleLines(s_ox - 1, s_oy - 1,
                       GRID_W * s_cell + 2, GRID_H * s_cell + 2,
                       (Color){40, 40, 65, 255});

    // Map tiles
    for (int gy = 0; gy < GRID_H; gy++) {
        for (int gx = 0; gx < GRID_W; gx++) {
            char c = s_map[gy][gx];
            if      (c == '#') draw_cell(gx, gy, '#', (Color){55, 55, 80, 255});
            else if (c == '.') draw_cell(gx, gy, '.', (Color){28, 28, 48, 255});
            else if (c == '>') draw_cell(gx, gy, '>', GOLD);

            if (s_hl[gy][gx]) {
                Color hc = (s_pl.spell == SP_FIREBALL)
                           ? (Color){200, 80, 0, 70}
                           : (Color){200, 200, 0, 55};
                DrawRectangle(s_ox + gx * s_cell, s_oy + gy * s_cell,
                              s_cell, s_cell, hc);
            }
        }
    }

    // Enemies (glyph + mini HP bar above)
    for (int i = 0; i < s_en_n; i++) {
        if (!s_en[i].alive) continue;
        Enemy *e = &s_en[i];
        draw_cell(e->x, e->y, e->glyph, e->col);
        int bx = s_ox + e->x * s_cell;
        int by = s_oy + e->y * s_cell - 2;
        DrawRectangle(bx, by, s_cell, 2, DARKGRAY);
        DrawRectangle(bx, by, (e->hp * s_cell) / e->max_hp, 2, RED);
    }

    // Player
    draw_cell(s_pl.x, s_pl.y, '@', YELLOW);

    // Targeting cursor outline
    if (s_targeting) {
        IV2 mg = mouse_to_grid();
        if (mg.x >= 0 && mg.x < GRID_W && mg.y >= 0 && mg.y < GRID_H)
            DrawRectangleLines(s_ox + mg.x * s_cell, s_oy + mg.y * s_cell,
                               s_cell, s_cell, (Color){255, 255, 100, 220});
    }

    // Particles
    for (int i = 0; i < s_par_n; i++) {
        Particle *p = &s_par[i];
        Color c = p->col;
        c.a = (unsigned char)((p->life / p->max_life) * 255.0f);
        char s[2] = {p->glyph, '\0'};
        DrawText(s, s_ox + (int)(p->x * s_cell),
                    s_oy + (int)(p->y * s_cell), s_cell, c);
    }

    // ── UI strip ──────────────────────────────────────────────────────────────
    int grid_bot = s_oy + GRID_H * s_cell;
    int sw = GetScreenWidth();

    DrawRectangle(0, grid_bot, sw, GetScreenHeight() - grid_bot, (Color){12, 12, 24, 255});
    DrawLine(0, grid_bot, sw, grid_bot, (Color){50, 50, 80, 255});

    int fs = sw / 68;
    if (fs < 10) fs = 10;
    if (fs > 16) fs = 16;
    int lh  = fs + 4;
    int pad = 10;

    // HP bar
    int bw = sw / 5;
    DrawText("HP", pad, grid_bot + pad, fs, RED);
    DrawRectangle(pad + fs * 3, grid_bot + pad, bw, fs - 1, DARKGRAY);
    DrawRectangle(pad + fs * 3, grid_bot + pad,
                  s_pl.max_hp > 0 ? (s_pl.hp * bw) / s_pl.max_hp : 0,
                  fs - 1, RED);
    char hp_s[24]; snprintf(hp_s, sizeof(hp_s), "%d/%d", s_pl.hp, s_pl.max_hp);
    DrawText(hp_s, pad + fs * 3 + bw + 4, grid_bot + pad, fs, RED);

    // Mana bar
    DrawText("MP", pad, grid_bot + pad + lh, fs, SKYBLUE);
    DrawRectangle(pad + fs * 3, grid_bot + pad + lh, bw, fs - 1, DARKGRAY);
    DrawRectangle(pad + fs * 3, grid_bot + pad + lh,
                  s_pl.max_mana > 0 ? (s_pl.mana * bw) / s_pl.max_mana : 0,
                  fs - 1, SKYBLUE);
    char mp_s[24]; snprintf(mp_s, sizeof(mp_s), "%d/%d", s_pl.mana, s_pl.max_mana);
    DrawText(mp_s, pad + fs * 3 + bw + 4, grid_bot + pad + lh, fs, SKYBLUE);

    // Spell list
    static const Color SP_COLS[NUM_SPELLS] = {
        {255, 230,  80, 255},
        {255, 120,  30, 255},
        {180, 130, 255, 255},
    };
    int spell_x = sw / 4;
    DrawText("SPELLS", spell_x, grid_bot + pad, fs, GRAY);
    for (int i = 0; i < NUM_SPELLS; i++) {
        bool sel = (s_targeting && s_pl.spell == i);
        Color tc = sel ? SP_COLS[i]
                       : (Color){SP_COLS[i].r/2, SP_COLS[i].g/2, SP_COLS[i].b/2, 255};
        int sy = grid_bot + pad + (i + 1) * lh;
        if (sel) DrawRectangle(spell_x - 2, sy - 1,
                               MeasureText(SP_FULL[i], fs) + 4, fs + 2,
                               (Color){40, 40, 15, 200});
        DrawText(SP_FULL[i], spell_x, sy, fs, tc);
    }

    // Message log
    int log_x = sw / 2 + 10;
    DrawText("LOG", log_x, grid_bot + pad, fs, GRAY);
    for (int i = 0; i < s_msg_n; i++) {
        float alpha = 1.0f - (float)(s_msg_n - 1 - i) * 0.22f;
        if (alpha < 0.25f) alpha = 0.25f;
        Color mc = {200, 200, 200, (unsigned char)(alpha * 255.0f)};
        DrawText(s_msg[i], log_x, grid_bot + pad + (i + 1) * lh, fs, mc);
    }

    // Status line: targeting hint or controls
    int hint_y = grid_bot + pad + 4 * lh;
    if (s_targeting) {
        DrawText(TextFormat("Targeting: %s  (ESC to cancel)", SP_NAMES[s_pl.spell]),
                 pad, hint_y, fs, YELLOW);
    } else {
        DrawText("WASD/arrows: move  |  1/2/3: select spell  |  click: cast  |  bump enemy: melee",
                 pad, hint_y, fs - 1, (Color){80, 80, 100, 255});
    }

    // Game-over / win overlay
    if (s_game_over || s_won) {
        DrawRectangle(0, 0, sw, GetScreenHeight(), (Color){0, 0, 0, 160});
        const char *msg = s_won ? "YOU ESCAPED!" : "YOU DIED";
        Color mc = s_won ? GOLD : RED;
        int ofs = 40, tw = MeasureText(msg, ofs);
        int cx = (sw - tw) / 2, cy = GetScreenHeight() / 2 - 30;
        DrawText(msg, cx, cy, ofs, mc);
        const char *sub = "Press R to restart";
        DrawText(sub, (sw - MeasureText(sub, 18)) / 2, cy + 55, 18, LIGHTGRAY);
    }
}

// ── Deinit ────────────────────────────────────────────────────────────────────
static void RiftWizardDeinit(void) { /* all state is static */ }

// ── Export ────────────────────────────────────────────────────────────────────
const Prototype RiftWizardProto = {
    .name        = "Rift Wizard",
    .description = "ASCII dungeon crawler: move, melee, and cast 3 spells.",
    .Init        = RiftWizardInit,
    .Update      = RiftWizardUpdate,
    .Draw        = RiftWizardDraw,
    .Deinit      = RiftWizardDeinit,
};
