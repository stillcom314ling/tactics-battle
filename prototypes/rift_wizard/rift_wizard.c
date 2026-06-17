#include "rift_wizard.h"
#include "raylib.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

// ── Constants ─────────────────────────────────────────────────────────────────
#define GRID_W      55
#define GRID_H      26
#define VP_W_TGT    24   // target columns visible on phone (~390px / 16px)
#define MAX_EN      18
#define MAX_PAR     300
#define MAX_MSG     5
#define MSG_LEN     64
#define REGEN_SEC   3.0f
#define FBALL_RAD   2

#define SP_MISSILE  0
#define SP_FIREBALL 1
#define SP_BLINK    2
#define NUM_SPELLS  3

static const int   SP_COST[NUM_SPELLS]  = {3, 6, 4};
static const char *SP_NAMES[NUM_SPELLS] = {"Magic Missile", "Fireball", "Blink"};
static const char *SP_MP[NUM_SPELLS]    = {"3mp", "6mp", "4mp"};

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

typedef struct { int x, y, w, h; } Rect;
#define NROOMS 6
static const Rect ROOMS[NROOMS] = {
    { 2,  1, 11, 7},
    {20,  1, 11, 6},
    {39,  1, 14, 7},
    { 2, 16, 11, 8},
    {21, 16, 11, 8},
    {40, 16, 13, 8},
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
static int      s_cell;   // px per grid cell
static int      s_ox;     // grid pixel origin x
static int      s_oy;     // grid pixel origin y
static int      s_vp_w;   // viewport columns
static int      s_cam_x;  // camera offset (dungeon columns)
static int      s_prev_tc;
static Vector2  s_last_tp;

// ── Camera ────────────────────────────────────────────────────────────────────
static void update_camera(void) {
    s_cam_x = s_pl.x - s_vp_w / 2;
    if (s_cam_x < 0) s_cam_x = 0;
    if (s_cam_x + s_vp_w > GRID_W) s_cam_x = GRID_W - s_vp_w;
}

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
        s_msg[s_msg_n++][MSG_LEN - 1] = '\0';
    } else {
        memmove(s_msg[0], s_msg[1], (size_t)MSG_LEN * (MAX_MSG - 1));
        strncpy(s_msg[MAX_MSG - 1], buf, MSG_LEN - 1);
        s_msg[MAX_MSG - 1][MSG_LEN - 1] = '\0';
    }
}

static IV2 screen_to_dungeon(Vector2 p) {
    return (IV2){(int)((p.x - s_ox) / s_cell) + s_cam_x,
                 (int)((p.y - s_oy) / s_cell)};
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
        p->x += p->vx * dt; p->y += p->vy * dt; p->life -= dt;
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
    int lo = x1<x2?x1:x2, hi = x1<x2?x2:x1;
    for (int x = lo; x <= hi; x++)
        if (x >= 0 && x < GRID_W) s_map[y][x] = '.';
}
static void carve_v(int x, int y1, int y2) {
    if (x < 0 || x >= GRID_W) return;
    int lo = y1<y2?y1:y2, hi = y1<y2?y2:y1;
    for (int y = lo; y <= hi; y++)
        if (y >= 0 && y < GRID_H) s_map[y][x] = '.';
}
static void gen_map(void) {
    memset(s_map, '#', sizeof(s_map));
    for (int i = 0; i < NROOMS; i++) carve_rect(ROOMS[i]);
    for (int i = 0; i < NROOMS - 1; i++) {
        int cx1 = ROOMS[i].x   + ROOMS[i].w/2,   cy1 = ROOMS[i].y   + ROOMS[i].h/2;
        int cx2 = ROOMS[i+1].x + ROOMS[i+1].w/2, cy2 = ROOMS[i+1].y + ROOMS[i+1].h/2;
        carve_h(cx1, cx2, cy1); carve_v(cx2, cy1, cy2);
    }
    {
        int cx1 = ROOMS[0].x + ROOMS[0].w/2, cy1 = ROOMS[0].y + ROOMS[0].h/2;
        int cx2 = ROOMS[3].x + ROOMS[3].w/2, cy2 = ROOMS[3].y + ROOMS[3].h/2;
        carve_v(cx1, cy1, cy2); carve_h(cx1, cx2, cy2);
    }
    s_map[ROOMS[4].y + ROOMS[4].h/2][ROOMS[4].x + ROOMS[4].w/2] = '>';
}

// ── Enemies ───────────────────────────────────────────────────────────────────
static void spawn_en(int x, int y, int t) {
    if (s_en_n >= MAX_EN) return;
    Enemy *e = &s_en[s_en_n++];
    *e = (Enemy){0}; e->alive = true; e->x = x; e->y = y;
    switch (t) {
    case 0: e->glyph='g'; e->col=(Color){80,200,80,255};
            e->hp=e->max_hp=10; e->atk=2; e->slow=0; e->name="Goblin"; break;
    case 1: e->glyph='s'; e->col=(Color){160,80,220,255};
            e->hp=e->max_hp=16; e->atk=4; e->slow=0; e->name="Shadow"; break;
    case 2: e->glyph='D'; e->col=(Color){220,50,50,255};
            e->hp=e->max_hp=28; e->atk=7; e->slow=1; e->name="Demon";  break;
    }
}
static void place_enemies(void) {
    s_en_n = 0;
    spawn_en(ROOMS[1].x+2, ROOMS[1].y+2, 0); spawn_en(ROOMS[1].x+7, ROOMS[1].y+3, 0);
    spawn_en(ROOMS[2].x+3, ROOMS[2].y+3, 1); spawn_en(ROOMS[2].x+9, ROOMS[2].y+2, 0);
    spawn_en(ROOMS[3].x+3, ROOMS[3].y+3, 0); spawn_en(ROOMS[3].x+7, ROOMS[3].y+5, 1);
    spawn_en(ROOMS[4].x+3, ROOMS[4].y+3, 2);
    spawn_en(ROOMS[5].x+3, ROOMS[5].y+3, 2); spawn_en(ROOMS[5].x+9, ROOMS[5].y+5, 1);
}

// ── Combat ────────────────────────────────────────────────────────────────────
static void player_damage(int dmg) {
    s_pl.hp -= dmg;
    if (s_pl.hp <= 0) { s_pl.hp = 0; s_game_over = true; push_msg("You die..."); }
}
static void en_damage(int idx, int dmg) {
    Enemy *e = &s_en[idx]; e->hp -= dmg;
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
        int adx = dx<0?-dx:dx, ady = dy<0?-dy:dy;
        if (adx + ady == 1) {
            push_msg("The %s hits you for %d!", e->name, e->atk);
            player_damage(e->atk);
            burst(s_pl.x, s_pl.y, '!', RED, 6, 3.0f, 0.25f);
        } else {
            int mx=0, my=0;
            if (adx>=ady) mx = dx>0?1:(dx<0?-1:0);
            else          my = dy>0?1:(dy<0?-1:0);
            int nx=e->x+mx, ny=e->y+my;
            bool ok = !wall_at(nx,ny) && en_at(nx,ny)<0
                      && !(nx==s_pl.x && ny==s_pl.y);
            if (!ok) {
                if (mx) { mx=0; my=dy>0?1:(dy<0?-1:0); }
                else    { my=0; mx=dx>0?1:(dx<0?-1:0); }
                nx=e->x+mx; ny=e->y+my;
                ok = (mx||my) && !wall_at(nx,ny) && en_at(nx,ny)<0
                              && !(nx==s_pl.x && ny==s_pl.y);
            }
            if (ok) { e->x=nx; e->y=ny; }
        }
    }
}

// ── Spell highlight ───────────────────────────────────────────────────────────
static void compute_hl(IV2 t) {
    memset(s_hl, 0, sizeof(s_hl));
    if (t.x<0||t.x>=GRID_W||t.y<0||t.y>=GRID_H) return;
    int px=s_pl.x, py=s_pl.y, tx=t.x, ty=t.y;
    switch (s_pl.spell) {
    case SP_MISSILE: {
        int x=px, y=py;
        int ddx=tx-px, ddy=ty-py;
        int adx=ddx<0?-ddx:ddx, ady=ddy<0?-ddy:ddy;
        int sx=ddx>=0?1:-1, sy=ddy>=0?1:-1;
        int err=adx-ady;
        for (;;) {
            if (x!=px||y!=py) {
                if (wall_at(x,y)) break;
                s_hl[y][x] = true;
                if (en_at(x,y)>=0) break;
            }
            if (x==tx&&y==ty) break;
            int e2=2*err;
            if (e2>-ady){err-=ady;x+=sx;}
            if (e2< adx){err+=adx;y+=sy;}
        }
        break;
    }
    case SP_FIREBALL:
        for (int ry=ty-FBALL_RAD; ry<=ty+FBALL_RAD; ry++)
            for (int rx=tx-FBALL_RAD; rx<=tx+FBALL_RAD; rx++) {
                int ddx=rx-tx, ddy=ry-ty;
                if (ddx*ddx+ddy*ddy <= FBALL_RAD*FBALL_RAD+1
                    && rx>=0&&rx<GRID_W&&ry>=0&&ry<GRID_H&&!wall_at(rx,ry))
                    s_hl[ry][rx] = true;
            }
        break;
    case SP_BLINK:
        if (!wall_at(tx,ty) && en_at(tx,ty)<0) s_hl[ty][tx] = true;
        break;
    }
}

// ── Spell casting ─────────────────────────────────────────────────────────────
static bool try_cast(IV2 t) {
    int tx=t.x, ty=t.y;
    if (s_pl.mana < SP_COST[s_pl.spell]) { push_msg("Not enough mana!"); return false; }
    switch (s_pl.spell) {
    case SP_MISSILE: {
        s_pl.mana -= SP_COST[SP_MISSILE];
        int x=s_pl.x, y=s_pl.y;
        int ddx=tx-x, ddy=ty-y;
        int adx=ddx<0?-ddx:ddx, ady=ddy<0?-ddy:ddy;
        int sx=ddx>=0?1:-1, sy=ddy>=0?1:-1, err=adx-ady, hit=-1;
        for (int step=0; step<64; step++) {
            if (x!=s_pl.x||y!=s_pl.y) {
                if (wall_at(x,y)) break;
                spawn_par((float)x+0.3f,(float)y+0.3f,'*',
                          (Color){255,220,80,255},0,0,0.1f+step*0.02f);
                hit=en_at(x,y);
                if (hit>=0) break;
            }
            if (x==tx&&y==ty) break;
            int e2=2*err;
            if (e2>-ady){err-=ady;x+=sx;}
            if (e2< adx){err+=adx;y+=sy;}
        }
        if (hit>=0) {
            int dmg=8+GetRandomValue(0,6);
            push_msg("Missile hits %s for %d!", s_en[hit].name, dmg);
            en_damage(hit,dmg);
            burst(s_en[hit].x,s_en[hit].y,'*',(Color){255,210,60,255},8,5.0f,0.4f);
        } else push_msg("Missile dissipates.");
        return true;
    }
    case SP_FIREBALL: {
        s_pl.mana -= SP_COST[SP_FIREBALL];
        int hits=0;
        for (int i=0; i<s_en_n; i++) {
            if (!s_en[i].alive) continue;
            int ddx=s_en[i].x-tx, ddy=s_en[i].y-ty;
            if (ddx*ddx+ddy*ddy <= FBALL_RAD*FBALL_RAD+1) {
                int dmg=12+GetRandomValue(0,8);
                push_msg("Fireball blasts %s for %d!", s_en[i].name, dmg);
                en_damage(i,dmg); hits++;
            }
        }
        if (!hits) push_msg("Fireball scorches the ground.");
        burst(tx,ty,'*',(Color){255,140,20,255},24,7.0f,0.7f);
        burst(tx,ty,'.',(Color){255,60,0,255},16,4.0f,0.4f);
        return true;
    }
    case SP_BLINK:
        if (wall_at(tx,ty))    { push_msg("Can't blink there!");    return false; }
        if (en_at(tx,ty)>=0)   { push_msg("Blink blocked!");        return false; }
        s_pl.mana -= SP_COST[SP_BLINK];
        burst(s_pl.x,s_pl.y,'?',(Color){140,80,255,255},8,3.5f,0.4f);
        s_pl.x=tx; s_pl.y=ty;
        burst(tx,ty,'*',(Color){180,130,255,255},8,3.5f,0.4f);
        push_msg("You blink to safety.");
        return true;
    }
    return false;
}

static void do_cast(IV2 t) {
    bool acted = try_cast(t);
    s_targeting = false;
    memset(s_hl, 0, sizeof(s_hl));
    if (acted && !s_game_over) { update_camera(); do_enemy_turns(); }
}

// ── Tap-to-move: one step toward target cell ──────────────────────────────────
static void tap_move(IV2 target) {
    int dx = target.x - s_pl.x, dy = target.y - s_pl.y;
    if (!dx && !dy) return;
    int adx=dx<0?-dx:dx, ady=dy<0?-dy:dy;
    int mdx=0, mdy=0;
    if (adx >= ady) mdx = dx>0?1:-1;
    else            mdy = dy>0?1:-1;
    int nx=s_pl.x+mdx, ny=s_pl.y+mdy;
    if (wall_at(nx,ny)) return;
    int ei = en_at(nx,ny);
    if (ei >= 0) {
        int dmg = 4 + GetRandomValue(0,3);
        push_msg("You strike the %s for %d!", s_en[ei].name, dmg);
        en_damage(ei, dmg);
        burst(nx,ny,'!',WHITE,6,4.0f,0.3f);
    } else {
        s_pl.x=nx; s_pl.y=ny;
        if (s_map[ny][nx] == '>') { s_won=true; push_msg("You reach the stairs! Victory!"); }
    }
    update_camera();
    if (!s_game_over && !s_won) do_enemy_turns();
}

// ── Button layout helpers ─────────────────────────────────────────────────────
static int btn_h(void) {
    int h = GetScreenHeight() / 10;
    if (h < 56)  h = 56;
    if (h > 200) h = 200;
    return h;
}
static void spell_rects(Rectangle *out) {
    int sw=GetScreenWidth(), sh=GetScreenHeight(), bh=btn_h();
    int w = sw / NUM_SPELLS;
    int y = sh - bh * 2;
    for (int i=0; i<NUM_SPELLS; i++)
        out[i] = (Rectangle){(float)(i*w), (float)y, (float)(w-1), (float)bh};
}
static Rectangle cancel_rect(void) {
    int sw=GetScreenWidth(), sh=GetScreenHeight(), bh=btn_h();
    return (Rectangle){0, (float)(sh - bh), (float)sw, (float)bh};
}
static Rectangle restart_rect(void) {
    int sw=GetScreenWidth(), sh=GetScreenHeight();
    int bh = btn_h();
    return (Rectangle){(float)(sw/4), (float)(sh/2 + 40), (float)(sw/2), (float)(bh)};
}

// ── Init ──────────────────────────────────────────────────────────────────────
static void RiftWizardInit(void) {
    int sw=GetScreenWidth(), sh=GetScreenHeight();
    int bh = btn_h();
    int btn_zone = bh * 2 + 4;

    // Cell size: fill VP_W_TGT columns; also keep grid ≤ 55% of non-button area
    int avail_h   = sh - btn_zone;
    s_cell        = sw / VP_W_TGT;
    int cell_by_h = (int)(avail_h * 0.55f) / GRID_H;
    if (cell_by_h < s_cell) s_cell = cell_by_h;
    if (s_cell < 10) s_cell = 10;
    if (s_cell > 30) s_cell = 30;

    s_vp_w = sw / s_cell;
    if (s_vp_w > GRID_W) s_vp_w = GRID_W;
    s_ox = (sw - s_vp_w * s_cell) / 2;
    s_oy = s_cell / 2;

    s_par_n=s_msg_n=0;
    s_targeting=s_game_over=s_won=false;
    s_regen_t=0.0f; s_prev_tc=0; s_last_tp=(Vector2){0,0};
    memset(s_hl,0,sizeof(s_hl));

    s_pl.hp=s_pl.max_hp=30; s_pl.mana=s_pl.max_mana=20; s_pl.spell=SP_MISSILE;
    s_pl.x=ROOMS[0].x+ROOMS[0].w/2; s_pl.y=ROOMS[0].y+ROOMS[0].h/2;

    gen_map(); place_enemies(); update_camera();
    push_msg("Tap grid: move/attack. Spell buttons: cast.");
}

// ── Update ────────────────────────────────────────────────────────────────────
static void RiftWizardUpdate(float dt) {
    update_particles(dt);
    if (!s_game_over && !s_won) {
        s_regen_t += dt;
        if (s_regen_t >= REGEN_SEC) {
            s_regen_t -= REGEN_SEC;
            if (s_pl.mana < s_pl.max_mana) s_pl.mana++;
        }
    }

    // ── Gather tap (touch-up or mouse-press) ─────────────────────────────────
    int tc = GetTouchPointCount();
    Vector2 tap = {-1,-1};
    bool tapped  = false;
    bool holding = false;

    if (tc > 0) {
        s_last_tp = GetTouchPosition(0);
        holding = true;
    } else if (s_prev_tc > 0) {
        tap = s_last_tp; tapped = true;   // finger lifted
    }
    if (tc == 0) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            tap = GetMousePosition(); tapped = true;
        } else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            s_last_tp = GetMousePosition(); holding = true;
        }
    }
    s_prev_tc = tc;

    // Live highlight update while pressing on grid
    if (holding && s_targeting) {
        Vector2 hp = tc > 0 ? GetTouchPosition(0) : GetMousePosition();
        IV2 mg = screen_to_dungeon(hp);
        compute_hl(mg);
    }

    // ── Keyboard fallback (desktop) ──────────────────────────────────────────
    if (IsKeyPressed(KEY_ONE))  { s_pl.spell=SP_MISSILE;  s_targeting=true; memset(s_hl,0,sizeof(s_hl)); }
    if (IsKeyPressed(KEY_TWO))  { s_pl.spell=SP_FIREBALL; s_targeting=true; memset(s_hl,0,sizeof(s_hl)); }
    if (IsKeyPressed(KEY_THREE)){ s_pl.spell=SP_BLINK;    s_targeting=true; memset(s_hl,0,sizeof(s_hl)); }
    if ((s_game_over||s_won) && IsKeyPressed(KEY_R)) { RiftWizardInit(); return; }
    if (!s_targeting && !s_game_over && !s_won) {
        int mdx=0, mdy=0;
        if (IsKeyPressed(KEY_W)||IsKeyPressed(KEY_UP))    mdy=-1;
        if (IsKeyPressed(KEY_S)||IsKeyPressed(KEY_DOWN))  mdy= 1;
        if (IsKeyPressed(KEY_A)||IsKeyPressed(KEY_LEFT))  mdx=-1;
        if (IsKeyPressed(KEY_D)||IsKeyPressed(KEY_RIGHT)) mdx= 1;
        if (mdx||mdy) tap_move((IV2){s_pl.x+mdx*GRID_W, s_pl.y+mdy*GRID_H});
    }

    if (!tapped) return;

    // ── Process tap ──────────────────────────────────────────────────────────
    if (s_game_over || s_won) {
        if (CheckCollisionPointRec(tap, restart_rect())) { RiftWizardInit(); return; }
        return;
    }

    Rectangle spr[NUM_SPELLS]; spell_rects(spr);
    Rectangle cr = cancel_rect();

    // Cancel button (bottom row, only meaningful when targeting)
    if (CheckCollisionPointRec(tap, cr)) {
        if (s_targeting) {
            s_targeting=false; memset(s_hl,0,sizeof(s_hl)); push_msg("Cancelled.");
        }
        return;
    }
    // Spell buttons (row above cancel)
    for (int i=0; i<NUM_SPELLS; i++) {
        if (CheckCollisionPointRec(tap, spr[i])) {
            s_pl.spell=i; s_targeting=true; memset(s_hl,0,sizeof(s_hl));
            push_msg("Target for %s?", SP_NAMES[i]);
            return;
        }
    }
    // Tap on grid
    IV2 mg = screen_to_dungeon(tap);
    if (mg.x<0||mg.x>=GRID_W||mg.y<0||mg.y>=GRID_H) return;
    if (s_targeting) do_cast(mg);
    else             tap_move(mg);
}

// ── Draw ──────────────────────────────────────────────────────────────────────
static void draw_vp(int gx, int gy, char ch, Color c) {
    int vx = gx - s_cam_x;
    if (vx < 0 || vx >= s_vp_w || gy < 0 || gy >= GRID_H) return;
    char s[2] = {ch,'\0'};
    DrawText(s, s_ox + vx*s_cell + 1, s_oy + gy*s_cell, s_cell, c);
}

static void RiftWizardDraw(void) {
    ClearBackground((Color){8,8,18,255});

    // Border around viewport
    DrawRectangleLines(s_ox-1, s_oy-1, s_vp_w*s_cell+2, GRID_H*s_cell+2,
                       (Color){40,40,65,255});

    // Tiles
    for (int gy=0; gy<GRID_H; gy++) {
        for (int vx=0; vx<s_vp_w; vx++) {
            int gx = vx + s_cam_x;
            char c = s_map[gy][gx];
            if      (c=='#') draw_vp(gx,gy,'#',(Color){55,55,80,255});
            else if (c=='.') draw_vp(gx,gy,'.',(Color){28,28,48,255});
            else if (c=='>') draw_vp(gx,gy,'>',GOLD);
            if (s_hl[gy][gx]) {
                Color hc = s_pl.spell==SP_FIREBALL
                           ? (Color){200,80,0,80} : (Color){200,200,0,60};
                DrawRectangle(s_ox+vx*s_cell, s_oy+gy*s_cell, s_cell, s_cell, hc);
            }
        }
    }

    // Enemies
    for (int i=0; i<s_en_n; i++) {
        Enemy *e = &s_en[i];
        if (!e->alive) continue;
        int vx = e->x - s_cam_x;
        if (vx<0||vx>=s_vp_w) continue;
        draw_vp(e->x, e->y, e->glyph, e->col);
        int bx=s_ox+vx*s_cell, by=s_oy+e->y*s_cell-2;
        DrawRectangle(bx,by,s_cell,2,DARKGRAY);
        DrawRectangle(bx,by,(e->hp*s_cell)/e->max_hp,2,RED);
    }

    // Player
    draw_vp(s_pl.x, s_pl.y, '@', YELLOW);

    // Targeting cursor
    if (s_targeting) {
        Vector2 cp = GetTouchPointCount()>0 ? GetTouchPosition(0) : GetMousePosition();
        IV2 mg = screen_to_dungeon(cp);
        int vx = mg.x - s_cam_x;
        if (vx>=0 && vx<s_vp_w && mg.y>=0 && mg.y<GRID_H)
            DrawRectangleLines(s_ox+vx*s_cell, s_oy+mg.y*s_cell,
                               s_cell, s_cell, (Color){255,255,100,200});
    }

    // Particles
    for (int i=0; i<s_par_n; i++) {
        Particle *p = &s_par[i];
        int pgx = (int)p->x - s_cam_x;
        if (pgx < -1 || pgx > s_vp_w+1) continue;
        Color c = p->col;
        c.a = (unsigned char)((p->life/p->max_life)*255.0f);
        char s[2] = {p->glyph,'\0'};
        DrawText(s, s_ox+(int)((p->x-(float)s_cam_x)*s_cell),
                    s_oy+(int)(p->y*s_cell), s_cell, c);
    }

    // ── Status strip (between grid and buttons) ───────────────────────────────
    int sw=GetScreenWidth(), sh=GetScreenHeight();
    int bh=btn_h();
    int grid_bot = s_oy + GRID_H*s_cell;
    int btns_top = sh - bh*2;
    int status_h = btns_top - grid_bot;

    DrawRectangle(0, grid_bot, sw, status_h, (Color){12,12,24,255});
    DrawLine(0, grid_bot, sw, grid_bot, (Color){50,50,80,255});

    int fs = status_h / 6;
    if (fs < 11) fs = 11;
    if (fs > 18) fs = 18;
    int lh = fs + 4;
    int pad = 8;

    // HP / MP bars
    int bar_w = sw/2 - fs*3 - pad*3;
    if (bar_w < 20) bar_w = 20;

    DrawText("HP", pad, grid_bot+pad, fs, RED);
    DrawRectangle(pad+fs*3, grid_bot+pad, bar_w, fs, DARKGRAY);
    DrawRectangle(pad+fs*3, grid_bot+pad,
                  s_pl.max_hp ? (s_pl.hp*bar_w)/s_pl.max_hp : 0, fs, RED);
    char tmp[24]; snprintf(tmp,sizeof(tmp),"%d/%d",s_pl.hp,s_pl.max_hp);
    DrawText(tmp, pad+fs*3+bar_w+4, grid_bot+pad, fs, RED);

    DrawText("MP", pad, grid_bot+pad+lh, fs, SKYBLUE);
    DrawRectangle(pad+fs*3, grid_bot+pad+lh, bar_w, fs, DARKGRAY);
    DrawRectangle(pad+fs*3, grid_bot+pad+lh,
                  s_pl.max_mana ? (s_pl.mana*bar_w)/s_pl.max_mana : 0, fs, SKYBLUE);
    snprintf(tmp,sizeof(tmp),"%d/%d",s_pl.mana,s_pl.max_mana);
    DrawText(tmp, pad+fs*3+bar_w+4, grid_bot+pad+lh, fs, SKYBLUE);

    // Messages (right half)
    int log_x = sw/2 + pad;
    for (int i=0; i<s_msg_n; i++) {
        float alpha = 1.0f - (float)(s_msg_n-1-i)*0.25f;
        if (alpha < 0.2f) alpha = 0.2f;
        Color mc = {200,200,200,(unsigned char)(alpha*255.0f)};
        DrawText(s_msg[i], log_x, grid_bot+pad+i*(lh-1), fs-1, mc);
    }

    // Targeting hint above buttons
    if (s_targeting) {
        const char *hint = TextFormat("Tap target for %s", SP_NAMES[s_pl.spell]);
        int hw = MeasureText(hint, fs);
        DrawText(hint, (sw-hw)/2, btns_top-lh-2, fs, YELLOW);
    }

    // ── Spell buttons ─────────────────────────────────────────────────────────
    static const Color SP_COLS[NUM_SPELLS] = {
        {255,230, 80,255}, {255,120, 30,255}, {180,130,255,255}
    };
    Rectangle spr[NUM_SPELLS]; spell_rects(spr);
    for (int i=0; i<NUM_SPELLS; i++) {
        bool sel = s_targeting && s_pl.spell==i;
        Color bg  = sel ? (Color){SP_COLS[i].r/3, SP_COLS[i].g/3, SP_COLS[i].b/3, 230}
                        : (Color){18,18,32,220};
        Color brd = sel ? SP_COLS[i] : (Color){55,55,85,255};
        DrawRectangleRec(spr[i], bg);
        DrawRectangleLinesEx(spr[i], 2, brd);
        Color tc  = sel ? SP_COLS[i]
                        : (Color){SP_COLS[i].r/2, SP_COLS[i].g/2, SP_COLS[i].b/2, 255};
        int btn_fs = bh * 22 / 100;
        if (btn_fs < 11) btn_fs = 11;
        if (btn_fs > 20) btn_fs = 20;
        int cx = (int)(spr[i].x + spr[i].width/2);
        int cy = (int)(spr[i].y + spr[i].height/2);
        int nw = MeasureText(SP_NAMES[i], btn_fs);
        int mw = MeasureText(SP_MP[i], btn_fs-2);
        DrawText(SP_NAMES[i], cx-nw/2, cy-btn_fs,   btn_fs,   tc);
        DrawText(SP_MP[i],    cx-mw/2, cy+2,         btn_fs-2, sel?GOLD:DARKGRAY);
    }

    // ── Cancel button (bottom row) ────────────────────────────────────────────
    Rectangle cr = cancel_rect();
    Color cancel_bg  = s_targeting ? (Color){55,15,15,230} : (Color){18,18,32,180};
    Color cancel_brd = s_targeting ? (Color){180,40,40,255} : (Color){40,40,60,255};
    DrawRectangleRec(cr, cancel_bg);
    DrawRectangleLinesEx(cr, 2, cancel_brd);
    int cfs = bh * 30 / 100;
    if (cfs < 12) cfs = 12;
    if (cfs > 22) cfs = 22;
    const char *clbl = s_targeting ? TextFormat("Cancel %s", SP_NAMES[s_pl.spell])
                                   : "Cancel";
    int cw = MeasureText(clbl, cfs);
    DrawText(clbl, (int)(cr.x+(cr.width-cw)/2),
                   (int)(cr.y+(cr.height-cfs)/2),
                   cfs, s_targeting?RED:DARKGRAY);

    // ── Game-over / win overlay ───────────────────────────────────────────────
    if (s_game_over || s_won) {
        DrawRectangle(0,0,sw,sh,(Color){0,0,0,170});
        int ofs = sw/9; if (ofs<28) ofs=28; if (ofs>60) ofs=60;
        const char *msg = s_won ? "YOU ESCAPED!" : "YOU DIED";
        Color mc = s_won ? GOLD : RED;
        int tw = MeasureText(msg,ofs);
        DrawText(msg,(sw-tw)/2, sh/2-ofs*2, ofs, mc);
        Rectangle rr = restart_rect();
        DrawRectangleRec(rr,(Color){35,35,55,240});
        DrawRectangleLinesEx(rr,2,LIGHTGRAY);
        const char *rl = "RESTART";
        int rw = MeasureText(rl, cfs);
        DrawText(rl,(int)(rr.x+(rr.width-rw)/2),(int)(rr.y+(rr.height-cfs)/2),cfs,WHITE);
    }
}

// ── Deinit / export ───────────────────────────────────────────────────────────
static void RiftWizardDeinit(void) {}

const Prototype RiftWizardProto = {
    .name        = "Rift Wizard",
    .description = "ASCII dungeon: tap to move, spell buttons to cast.",
    .Init        = RiftWizardInit,
    .Update      = RiftWizardUpdate,
    .Draw        = RiftWizardDraw,
    .Deinit      = RiftWizardDeinit,
};
