/* Orbfall — orb-drag roguelike prototype.
 * Puzzle & Dragons movement + cascade grammar, Slay-the-Spire run structure.
 * Resolver (resolve.c) and damage pipeline (content.c) are pure and
 * unit-tested; this file owns feel, ceremony, and the run. */

#include "orbfall.h"

#include "raylib.h"
#include "raygui.h"
#include "resolve.h"
#include "content.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* --------------------------------------------------------------- constants */

#define MOVE_TIME_BASE   5.0f
#define SWAP_LERP_RATE  22.0f    /* displaced orbs reach slot in <90ms */
#define POP_STEP_BASE    0.20f   /* cascade beat, debug-tunable       */
#define POP_STEP_FLOOR   0.12f
#define POP_STEP_ACCEL   0.90f
#define FALL_GRAVITY    38.0f    /* cells per s^2, in cell units      */
#define JACKPOT_HOLD     0.85f

#define MAP_FLOORS   8
#define MAP_WIDTH    3

#define PARTICLE_MAX 320
#define FLOATER_MAX   40
#define GHOST_MAX     32
#define FRAME_RING   240

/* ----------------------------------------------------------------- palette */

static const Color C_BG       = {  16,  19,  31, 255 };
static const Color C_PANEL    = {  27,  32,  50, 255 };
static const Color C_PANEL_HI = {  40,  47,  72, 255 };
static const Color C_TEXT     = { 233, 237, 249, 255 };
static const Color C_DIM      = { 132, 141, 165, 255 };
static const Color C_GOOD     = {  92, 220, 132, 255 };
static const Color C_BAD      = { 255,  92,  92, 255 };
static const Color C_GOLD     = { 255, 206,  82, 255 };

static const Color ORB_COL[RZ_COLOR_COUNT] = {
    { 255,  92,  58, 255 },  /* fire   */
    {  56, 170, 255, 255 },  /* water  */
    {  80, 210,  90, 255 },  /* wood   */
    { 255, 213,  65, 255 },  /* light  */
    { 172, 106, 255, 255 },  /* dark   */
    { 255, 100, 160, 255 },  /* heart  */
    { 110, 115, 128, 255 },  /* jammer */
};

/* ------------------------------------------------------------------- types */

typedef enum {
    GS_TITLE, GS_PICK_LEADER, GS_MAP, GS_BATTLE,
    GS_REWARD, GS_EVENT, GS_REST, GS_DEFEAT, GS_VICTORY,
} GameState;

typedef enum {
    BP_INTRO, BP_IDLE, BP_DRAG, BP_POP, BP_JACKPOT, BP_FALL,
    BP_ATTACK, BP_ENEMY, BP_WIN, BP_LOSE,
} BattlePhase;

typedef enum { NODE_NONE, NODE_FIGHT, NODE_ELITE, NODE_EVENT,
               NODE_REST, NODE_BOSS } NodeType;

typedef struct {
    NodeType type;
    int      pick;      /* enemy / event index, seeded at map gen */
    int      pick2;     /* second enemy, -1 = single              */
    bool     done;
} MapNode;

typedef struct {
    const EnemyDef *def;
    float  scale;
    long   hp, hp_max;
    int    intent_idx;
    int    delay;
    bool   enraged;
    float  hurt_t;
    float  lunge_t;
} Enemy;

typedef struct { Vector2 pos, vel; float r, t, life; Color col; } Particle;
typedef struct { Vector2 pos; float t, life; bool big; Color col;
                 char text[20]; } Floater;
typedef struct { Vector2 pos; float t; uint8_t color; bool enhanced; } Ghost;

/* ------------------------------------------------------------ module state */

static GameState   s_state;
static BattlePhase s_bp;

/* run */
static uint64_t s_run_seed;
static RzRng    s_rng;
static int      s_leader_idx;
static UnitDef  s_units[OFC_TEAM_SIZE];      /* mutable copies for upgrades */
static RelicId  s_relics[OFC_MAX_RELICS];
static int      s_relic_count;
static OfcRules s_rules;
static RzRules  s_rz_rules;
static long     s_hp, s_hp_max;
static int      s_skill_cd[OFC_TEAM_SIZE];
static MapNode  s_map[MAP_FLOORS][MAP_WIDTH];
static int      s_floor, s_col;              /* -1 = run start */
static long     s_best_burst;
static int      s_best_combo;

/* battle */
static RzBoard s_board;
static Enemy   s_foes[2];
static int     s_foe_count;
static Vector2 s_off[RZ_ROWS][RZ_COLS];      /* visual offset from slot */
static float   s_fall_vel[RZ_ROWS][RZ_COLS];
static bool    s_falling[RZ_ROWS][RZ_COLS];
static float   s_intro_t;

static bool    s_dragging;
static int     s_held_r, s_held_c;
static RzOrb   s_held_orb;
static Vector2 s_finger;
static bool    s_timer_started;
static float   s_move_timer, s_move_time_max;
static bool    s_moved_any;
static float   s_time_burst;
static int     s_timecut_turns;
static float   s_timecut_secs;

static RzMatch s_turn_matches[OFC_MAX_TURN_MATCHES];
static int     s_turn_count;
static RzMatch s_step_matches[RZ_MAX_MATCHES];
static int     s_step_count, s_step_idx;
static float   s_step_timer, s_step_dur;
static int     s_combo;
static bool    s_cleared_board;
static float   s_jackpot_t;

static OfcAttack s_attack;
static int       s_strike_idx;
static float     s_strike_t;
static float     s_shield_skill;    /* % from actives, this turn      */
static float     s_shield_cross;    /* % from heart cross, this turn  */
static int       s_enemy_idx;
static float     s_enemy_t;
static bool      s_enemy_applied;
static float     s_banner_t;
static char      s_banner[48];

/* menus */
static int   s_reward_ids[3];
static int   s_event_id;
static bool  s_show_relics;

/* juice */
static Particle s_parts[PARTICLE_MAX];
static Floater  s_floaters[FLOATER_MAX];
static Ghost    s_ghosts[GHOST_MAX];
static float    s_shake;
static float    s_flash;

/* audio */
static bool  s_audio_ready;
static Sound s_snd_pop, s_snd_pop_alias[6];
static int   s_pop_alias_idx;
static Sound s_snd_tick, s_snd_chord, s_snd_thud, s_snd_chime, s_snd_land;

/* input */
static int     s_prev_tc;
static bool    s_pressed, s_released;
static Vector2 s_press_pos;

/* debug */
static bool  s_dbg_open;
static float s_dbg_step_base = POP_STEP_BASE;
static float s_dbg_move_time = MOVE_TIME_BASE;
static float s_dbg_juice     = 1.0f;
static float s_frame_ring[FRAME_RING];
static int   s_frame_idx;

/* ------------------------------------------------------------------ audio */

static Wave make_wave(int frames)
{
    Wave w = { 0 };
    w.frameCount = (unsigned int)frames;
    w.sampleRate = 22050;
    w.sampleSize = 32;
    w.channels   = 1;
    w.data       = MemAlloc((unsigned int)frames * sizeof(float));
    return w;
}

static Sound make_blip(float freq, float secs)
{
    int n = (int)(22050 * secs);
    Wave w = make_wave(n);
    float *d = (float *)w.data;
    for (int i = 0; i < n; i++) {
        float t = (float)i / 22050.0f;
        float env = expf(-t * 14.0f);
        d[i] = sinf(2.0f * PI * freq * t) * env * 0.5f;
    }
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

static Sound make_chord(void)
{
    int n = (int)(22050 * 0.8f);
    Wave w = make_wave(n);
    float *d = (float *)w.data;
    const float f[3] = { 523.25f, 659.25f, 783.99f };
    for (int i = 0; i < n; i++) {
        float t = (float)i / 22050.0f;
        float env = expf(-t * 3.5f);
        float v = 0;
        for (int k = 0; k < 3; k++) v += sinf(2.0f * PI * f[k] * t);
        d[i] = v * env * 0.22f;
    }
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

static Sound make_thud(void)
{
    int n = (int)(22050 * 0.20f);
    Wave w = make_wave(n);
    float *d = (float *)w.data;
    for (int i = 0; i < n; i++) {
        float t = (float)i / 22050.0f;
        float freq = 170.0f - 110.0f * (t / 0.20f);
        float env = expf(-t * 16.0f);
        d[i] = sinf(2.0f * PI * freq * t) * env * 0.8f;
    }
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

static Sound make_tickw(float freq, float secs)
{
    int n = (int)(22050 * secs);
    Wave w = make_wave(n);
    float *d = (float *)w.data;
    for (int i = 0; i < n; i++) {
        float t = (float)i / 22050.0f;
        float env = 1.0f - t / secs;
        d[i] = sinf(2.0f * PI * freq * t) * env * env * 0.25f;
    }
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

static Sound make_chime(void)
{
    int n = (int)(22050 * 0.35f);
    Wave w = make_wave(n);
    float *d = (float *)w.data;
    for (int i = 0; i < n; i++) {
        float t = (float)i / 22050.0f;
        float env = expf(-t * 8.0f);
        d[i] = (sinf(2.0f * PI * 880.0f * t) +
                0.6f * sinf(2.0f * PI * 1318.5f * t)) * env * 0.28f;
    }
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

static void audio_init(void)
{
    if (!IsAudioDeviceReady()) InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        TraceLog(LOG_WARNING, "orbfall: audio device unavailable");
        return;
    }
    s_snd_pop   = make_blip(523.25f, 0.22f);
    for (int i = 0; i < 6; i++) s_snd_pop_alias[i] = LoadSoundAlias(s_snd_pop);
    s_snd_tick  = make_tickw(1150.0f, 0.03f);
    s_snd_chord = make_chord();
    s_snd_thud  = make_thud();
    s_snd_chime = make_chime();
    s_snd_land  = make_tickw(300.0f, 0.05f);
    s_audio_ready = true;
}

static void audio_deinit(void)
{
    if (!s_audio_ready) return;
    for (int i = 0; i < 6; i++) UnloadSoundAlias(s_snd_pop_alias[i]);
    UnloadSound(s_snd_pop);
    UnloadSound(s_snd_tick);
    UnloadSound(s_snd_chord);
    UnloadSound(s_snd_thud);
    UnloadSound(s_snd_chime);
    UnloadSound(s_snd_land);
    s_audio_ready = false;
}

/* combo pitch: one semitone per combo step */
static void play_pop(int combo)
{
    if (!s_audio_ready) return;
    Sound *s = &s_snd_pop_alias[s_pop_alias_idx];
    s_pop_alias_idx = (s_pop_alias_idx + 1) % 6;
    float pitch = powf(2.0f, (float)(combo - 1) / 12.0f);
    if (pitch > 3.0f) pitch = 3.0f;
    SetSoundPitch(*s, pitch);
    PlaySound(*s);
}

static void play(Sound s) { if (s_audio_ready) PlaySound(s); }

/* ------------------------------------------------------------------ layout */

static float sw(void) { return (float)GetScreenWidth(); }
static float sh(void) { return (float)GetScreenHeight(); }

static float cell_sz(void)  { return sw() / (float)RZ_COLS; }
static float board_top(void){ return sh() - cell_sz() * RZ_ROWS; }

static Vector2 cell_center(int r, int c)
{
    float cs = cell_sz();
    return (Vector2){ cs * ((float)c + 0.5f),
                      board_top() + cs * ((float)r + 0.5f) };
}

static void cell_at(Vector2 p, int *out_r, int *out_c)
{
    float cs = cell_sz();
    int c = (int)(p.x / cs);
    int r = (int)((p.y - board_top()) / cs);
    if (c < 0) c = 0;
    if (c >= RZ_COLS) c = RZ_COLS - 1;
    if (r < 0) r = 0;
    if (r >= RZ_ROWS) r = RZ_ROWS - 1;
    *out_r = r; *out_c = c;
}

static Rectangle portrait_rect(int i)
{
    float w = sw() / 4.6f;
    float gap = (sw() - 4.0f * w) / 5.0f;
    float h = w * 0.78f;
    return (Rectangle){ gap + (float)i * (w + gap),
                        board_top() - h - sh() * 0.012f, w, h };
}

static Rectangle card_rect(int i, int n)
{
    float w = sw() * 0.86f;
    float h = sh() * 0.115f;
    float total = (float)n * (h + sh() * 0.02f);
    float y0 = sh() * 0.56f - total * 0.5f;
    return (Rectangle){ (sw() - w) * 0.5f,
                        y0 + (float)i * (h + sh() * 0.02f), w, h };
}

static Vector2 map_node_pos(int f, int c, int count)
{
    float x0 = sw() * 0.5f - (float)(count - 1) * sw() * 0.16f;
    float y = sh() * 0.86f - (float)f * (sh() * 0.72f / (MAP_FLOORS - 1));
    return (Vector2){ x0 + (float)c * sw() * 0.32f, y };
}

/* ------------------------------------------------------------------- input */

static Vector2 input_pos(void)
{
    if (GetTouchPointCount() > 0) return GetTouchPosition(0);
    return GetMousePosition();
}

static void input_poll(void)
{
    int tc = GetTouchPointCount();
    s_pressed  = (tc > 0 && s_prev_tc == 0) ||
                 (tc == 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
    s_released = (tc == 0 && s_prev_tc > 0) ||
                 (tc == 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT));
    if (s_pressed) s_press_pos = input_pos();
    s_prev_tc = tc;
}

static bool tapped(Rectangle r)
{
    return s_released &&
           CheckCollisionPointRec(s_press_pos, r) &&
           CheckCollisionPointRec(s_finger, r);
}

/* ------------------------------------------------------------------- juice */

static void spawn_particles(Vector2 pos, Color col, int n)
{
    n = (int)((float)n * s_dbg_juice);
    for (int i = 0; i < PARTICLE_MAX && n > 0; i++) {
        Particle *p = &s_parts[i];
        if (p->t < p->life) continue;
        float a = (float)GetRandomValue(0, 628) / 100.0f;
        float v = (float)GetRandomValue(30, 100) / 100.0f * cell_sz() * 3.2f;
        p->pos = pos;
        p->vel = (Vector2){ cosf(a) * v, sinf(a) * v - cell_sz() * 1.4f };
        p->r = cell_sz() * (float)GetRandomValue(4, 10) / 100.0f;
        p->t = 0; p->life = 0.55f;
        p->col = col;
        n--;
    }
}

static void spawn_floater(Vector2 pos, const char *text, Color col, bool big)
{
    for (int i = 0; i < FLOATER_MAX; i++) {
        Floater *f = &s_floaters[i];
        if (f->t < f->life) continue;
        f->pos = pos; f->t = 0; f->life = big ? 1.0f : 0.8f;
        f->big = big; f->col = col;
        snprintf(f->text, sizeof(f->text), "%s", text);
        return;
    }
}

static void spawn_ghost(Vector2 pos, uint8_t color, bool enhanced)
{
    for (int i = 0; i < GHOST_MAX; i++) {
        if (s_ghosts[i].t > 0.0f) continue;
        s_ghosts[i] = (Ghost){ .pos = pos, .t = 0.28f,
                               .color = color, .enhanced = enhanced };
        return;
    }
}

static void juice_update(float dt)
{
    for (int i = 0; i < PARTICLE_MAX; i++) {
        Particle *p = &s_parts[i];
        if (p->t >= p->life) continue;
        p->t += dt;
        p->vel.y += cell_sz() * 9.0f * dt;
        p->pos.x += p->vel.x * dt;
        p->pos.y += p->vel.y * dt;
    }
    for (int i = 0; i < FLOATER_MAX; i++) {
        Floater *f = &s_floaters[i];
        if (f->t >= f->life) continue;
        f->t += dt;
        f->pos.y -= sh() * (f->big ? 0.045f : 0.06f) * dt;
    }
    for (int i = 0; i < GHOST_MAX; i++)
        if (s_ghosts[i].t > 0.0f) s_ghosts[i].t -= dt;
    s_shake *= expf(-7.0f * dt);
    s_flash *= expf(-6.0f * dt);
    if (s_banner_t > 0.0f) s_banner_t -= dt;
}

static void set_banner(const char *text, float secs)
{
    snprintf(s_banner, sizeof(s_banner), "%s", text);
    s_banner_t = secs;
}

/* ---------------------------------------------------------------- run setup */

static void rebuild_rules(void)
{
    long old_max = s_hp_max;
    ofc_build_rules(s_relics, s_relic_count, &OFC_LEADERS[s_leader_idx],
                    0.0f, &s_rules, &s_rz_rules);
    long base = 0;
    for (int i = 0; i < OFC_TEAM_SIZE; i++) base += s_units[i].hp;
    s_hp_max = (long)((double)base * (double)s_rules.hp_mult);
    if (old_max > 0 && s_hp_max > old_max) s_hp += s_hp_max - old_max;
    if (s_hp > s_hp_max) s_hp = s_hp_max;
}

static void gen_map(void)
{
    memset(s_map, 0, sizeof(s_map));
    for (int f = 0; f < MAP_FLOORS; f++) {
        if (f == MAP_FLOORS - 1) {
            s_map[f][1].type = NODE_BOSS;
            continue;
        }
        if (f == MAP_FLOORS - 2) {
            s_map[f][0].type = NODE_REST;
            s_map[f][1].type = NODE_REST;
            continue;
        }
        int n = 2 + rz_rand_range(&s_rng, 2);
        for (int c = 0; c < n; c++) {
            NodeType t = NODE_FIGHT;
            if (f >= 1 && f <= 4 && rz_randf(&s_rng) < 0.30f) t = NODE_EVENT;
            s_map[f][c].type = t;
            s_map[f][c].pick = rz_rand_range(&s_rng, t == NODE_EVENT ? 4
                                                     : OFC_ENEMY_COUNT);
            s_map[f][c].pick2 = -1;
            if (t == NODE_FIGHT && f >= 2 && rz_randf(&s_rng) < 0.45f)
                s_map[f][c].pick2 = rz_rand_range(&s_rng, OFC_ENEMY_COUNT);
        }
    }
    /* exactly one elite, floor 3 or 4 */
    int ef = 3 + rz_rand_range(&s_rng, 2);
    for (int c = 0; c < MAP_WIDTH; c++) {
        if (s_map[ef][c].type == NODE_FIGHT) {
            s_map[ef][c].type = NODE_ELITE;
            s_map[ef][c].pick = rz_rand_range(&s_rng, OFC_ELITE_COUNT);
            s_map[ef][c].pick2 = -1;
            break;
        }
    }
}

static int floor_node_count(int f)
{
    int n = 0;
    for (int c = 0; c < MAP_WIDTH; c++)
        if (s_map[f][c].type != NODE_NONE) n++;
    return n;
}

static bool node_reachable(int f, int c)
{
    if (s_map[f][c].type == NODE_NONE) return false;
    if (f != s_floor + 1) return false;
    if (s_floor < 0) return true;
    /* adjacency by screen position: nearest columns reachable */
    int cur_n = floor_node_count(s_floor);
    int nxt_n = floor_node_count(f);
    float cur_x = map_node_pos(s_floor, s_col, cur_n).x;
    float nxt_x = map_node_pos(f, c, nxt_n).x;
    return fabsf(cur_x - nxt_x) <= sw() * 0.33f;
}

static void start_run(int leader_idx)
{
    s_leader_idx = leader_idx;
    const LeaderDef *ld = &OFC_LEADERS[leader_idx];
    s_units[0] = ld->unit;
    for (int i = 0; i < 3; i++) s_units[i + 1] = OFC_UNITS[ld->subs[i]];
    s_relic_count = 0;
    s_hp = s_hp_max = 0;
    rebuild_rules();
    s_hp = s_hp_max;
    s_floor = -1; s_col = 0;
    s_best_burst = 0; s_best_combo = 0;
    s_timecut_turns = 0;
    gen_map();
    s_state = GS_MAP;
}

static void gain_relic(RelicId id)
{
    if (s_relic_count < OFC_MAX_RELICS) s_relics[s_relic_count++] = id;
    rebuild_rules();
    play(s_snd_chime);
}

static bool relic_is_unique(RelicId id)
{
    return id == RELIC_GUARD_BREAK || id == RELIC_JAMMER_SEAL ||
           id == RELIC_STONE_SKY   || id == RELIC_HEART_WARD;
}

static bool has_relic(RelicId id)
{
    for (int i = 0; i < s_relic_count; i++)
        if (s_relics[i] == id) return true;
    return false;
}

static RelicId roll_relic(bool shape_only)
{
    for (int tries = 0; tries < 64; tries++) {
        RelicId id = (RelicId)rz_rand_range(&s_rng, RELIC_COUNT);
        if (shape_only && OFC_RELICS[id].utility) continue;
        if (relic_is_unique(id) && has_relic(id)) continue;
        return id;
    }
    return RELIC_TIME_DILATOR;
}

static void roll_rewards(bool elite)
{
    for (int i = 0; i < 3; i++) {
        RelicId id;
        bool dup;
        int guard = 0;
        do {
            id = roll_relic(elite);
            dup = false;
            for (int j = 0; j < i; j++)
                if (s_reward_ids[j] == (int)id) dup = true;
        } while (dup && ++guard < 32);
        s_reward_ids[i] = (int)id;
    }
}

/* ------------------------------------------------------------- board ops */

static void reset_offsets(void)
{
    memset(s_off, 0, sizeof(s_off));
    memset(s_fall_vel, 0, sizeof(s_fall_vel));
    memset(s_falling, 0, sizeof(s_falling));
}

static void board_convert(uint8_t from, uint8_t to)
{
    for (int r = 0; r < RZ_ROWS; r++)
        for (int c = 0; c < RZ_COLS; c++)
            if (s_board.cell[r][c].color == from) {
                s_board.cell[r][c].color = to;
                spawn_particles(cell_center(r, c), ORB_COL[to], 4);
            }
}

static void board_enhance(uint8_t color)
{
    for (int r = 0; r < RZ_ROWS; r++)
        for (int c = 0; c < RZ_COLS; c++)
            if (s_board.cell[r][c].color == color)
                s_board.cell[r][c].enhanced = true;
}

static void board_refresh(void)
{
    rz_fill_no_match(&s_board, &s_rng, &s_rz_rules);
    reset_offsets();
    for (int r = 0; r < RZ_ROWS; r++)
        for (int c = 0; c < RZ_COLS; c++) {
            s_off[r][c].y = -cell_sz() * (float)(RZ_ROWS - r + 1);
            s_falling[r][c] = true;
        }
}

static void board_random_cells(int n, void (*fn)(int r, int c))
{
    for (int k = 0; k < n * 8 && n > 0; k++) {
        int r = rz_rand_range(&s_rng, RZ_ROWS);
        int c = rz_rand_range(&s_rng, RZ_COLS);
        if (s_board.cell[r][c].color >= RZ_COLOR_COUNT) continue;
        fn(r, c);
        n--;
    }
}

static void cell_to_jammer(int r, int c)
{
    if (s_board.cell[r][c].color == RZ_JAMMER) return;
    s_board.cell[r][c] = (RzOrb){ .color = RZ_JAMMER };
    spawn_particles(cell_center(r, c), ORB_COL[RZ_JAMMER], 5);
}

static void cell_seal(int r, int c)
{
    s_board.cell[r][c].sealed = true;
    spawn_particles(cell_center(r, c), C_DIM, 3);
}

static void board_unseal_all(void)
{
    for (int r = 0; r < RZ_ROWS; r++)
        for (int c = 0; c < RZ_COLS; c++)
            s_board.cell[r][c].sealed = false;
}

/* ------------------------------------------------------------ battle setup */

static void spawn_foe(int slot, const EnemyDef *def, float scale)
{
    Enemy *e = &s_foes[slot];
    e->def = def;
    e->scale = scale;
    e->hp_max = (long)((double)def->hp * (double)scale);
    if (e->hp_max < 1) e->hp_max = def->hp; /* tiny-HP puzzle enemies */
    if (def->hp < 100) e->hp_max = def->hp;
    e->hp = e->hp_max;
    e->intent_idx = 0;
    e->delay = 0;
    e->enraged = false;
    e->hurt_t = 0;
    e->lunge_t = 0;
}

static void start_battle(const EnemyDef *a, const EnemyDef *b, float scale)
{
    spawn_foe(0, a, scale);
    s_foe_count = 1;
    if (b) { spawn_foe(1, b, scale); s_foe_count = 2; }

    rz_fill_no_match(&s_board, &s_rng, &s_rz_rules);
    reset_offsets();
    s_dragging = false;
    s_turn_count = 0; s_combo = 0;
    s_shield_skill = s_shield_cross = 0;
    s_time_burst = 0;
    s_cleared_board = false;
    for (int i = 0; i < OFC_TEAM_SIZE; i++) {
        s_skill_cd[i] = s_units[i].skill.cooldown - s_rules.skill_boost;
        if (s_skill_cd[i] < 0) s_skill_cd[i] = 0;
    }
    s_intro_t = 0.6f;
    s_bp = BP_INTRO;
    s_state = GS_BATTLE;

    if (a->is_boss && !s_rules.hazard_immune) {
        board_random_cells(4, cell_seal);
        set_banner("PREEMPTIVE: orbs sealed!", 1.6f);
    }
}

static void enter_node(int f, int c)
{
    s_floor = f; s_col = c;
    MapNode *n = &s_map[f][c];
    n->done = true;
    float scale = 1.0f + 0.22f * (float)f;

    switch (n->type) {
    case NODE_FIGHT:
        start_battle(&OFC_ENEMIES[n->pick],
                     n->pick2 >= 0 ? &OFC_ENEMIES[n->pick2] : NULL, scale);
        break;
    case NODE_ELITE:
        start_battle(&OFC_ELITES[n->pick], NULL, 1.0f);
        break;
    case NODE_BOSS:
        start_battle(&OFC_BOSS, NULL, 1.0f);
        break;
    case NODE_EVENT:
        s_event_id = n->pick;
        s_state = GS_EVENT;
        break;
    case NODE_REST:
        s_state = GS_REST;
        break;
    default: break;
    }
}

/* --------------------------------------------------------------- mid-drag */

/* Predicted stats if the player released right now — powers the live leader
 * condition readout, so the condition is checkable while moving. */
static void preview_stats(OfcTurnStats *out)
{
    RzBoard b = s_board;
    if (s_dragging) b.cell[s_held_r][s_held_c] = s_held_orb;
    RzMatch m[RZ_MAX_MATCHES];
    int n = rz_find_matches(&b, m, RZ_MAX_MATCHES);
    ofc_turn_stats(m, n, out);
}

/* ---------------------------------------------------------------- cascade */

static float move_time_now(void)
{
    float t = s_dbg_move_time + s_rules.time_extend + s_time_burst;
    if (s_timecut_turns > 0) t -= s_timecut_secs;
    if (t < 1.5f) t = 1.5f;
    return t;
}

static void begin_pop_phase(void)
{
    s_step_count = rz_find_matches(&s_board, s_step_matches, RZ_MAX_MATCHES);
    s_step_idx = 0;
    if (s_step_count > 0) {
        s_step_timer = 0.05f;
        s_bp = BP_POP;
        return;
    }
    /* cascade over */
    if (s_combo > 0) {
        s_bp = BP_ATTACK;
        s_strike_idx = -1;
        s_strike_t = 0.35f;
        ofc_compute_attack(s_turn_matches, s_turn_count,
                           (const UnitDef *[]){ &s_units[0], &s_units[1],
                                                &s_units[2], &s_units[3] },
                           &OFC_LEADERS[s_leader_idx], &s_rules,
                           (float)s_hp / (float)s_hp_max,
                           s_foes[0].hp > 0 ? s_foes[0].def->color
                                            : s_foes[s_foe_count-1].def->color,
                           s_foes[0].hp > 0 ? s_foes[0].def->def
                                            : s_foes[s_foe_count-1].def->def,
                           &s_attack);
        s_shield_cross = s_attack.shield;
        if (s_combo > s_best_combo) s_best_combo = s_combo;
        /* heal up front */
        if (s_attack.heal > 0) {
            s_hp += s_attack.heal;
            if (s_hp > s_hp_max) s_hp = s_hp_max;
            char b[24], t[32];
            ofc_fmt_num(s_attack.heal, b);
            snprintf(t, sizeof(t), "+%s", b);
            spawn_floater((Vector2){ sw() * 0.5f, board_top() - sh() * 0.09f },
                          t, C_GOOD, false);
            play(s_snd_chime);
        }
    } else {
        /* no matches: the move still passes the turn */
        s_bp = BP_ENEMY;
        s_enemy_idx = 0; s_enemy_t = 0; s_enemy_applied = false;
    }
}

static void pop_current_match(void)
{
    RzMatch *m = &s_step_matches[s_step_idx];
    s_combo++;
    if (s_turn_count < OFC_MAX_TURN_MATCHES)
        s_turn_matches[s_turn_count++] = *m;

    Vector2 mid = { 0, 0 };
    int cnt = 0;
    for (int r = 0; r < RZ_ROWS; r++)
        for (int c = 0; c < RZ_COLS; c++)
            if (m->cells & rz_bit(r, c)) {
                Vector2 p = cell_center(r, c);
                spawn_ghost(p, m->color, s_board.cell[r][c].enhanced);
                spawn_particles(p, ORB_COL[m->color], 4);
                mid.x += p.x; mid.y += p.y; cnt++;
            }
    if (cnt > 0) { mid.x /= (float)cnt; mid.y /= (float)cnt; }

    rz_remove(&s_board, m->cells);
    play_pop(s_combo);
    if (s_combo >= 4) s_shake = fminf(s_shake + cell_sz() * 0.04f,
                                      cell_sz() * 0.22f);

    /* shape callouts, part of the ceremony */
    if (m->is_box)        spawn_floater(mid, "VOID PIERCE", C_GOLD, false);
    else if (m->is_cross) spawn_floater(mid, "CROSS", C_GOLD, false);
    else if (m->is_l)     spawn_floater(mid, "L!", C_GOLD, false);
    else if (m->is_row)   spawn_floater(mid, "ROW", C_GOLD, false);

    if (m->is_l && s_rules.l_unseals) board_unseal_all();
}

static void begin_fall_phase(void)
{
    uint8_t fall[RZ_ROWS][RZ_COLS], entry[RZ_ROWS][RZ_COLS];
    rz_gravity(&s_board, fall);
    rz_skyfall(&s_board, &s_rng, &s_rz_rules, entry);
    for (int r = 0; r < RZ_ROWS; r++)
        for (int c = 0; c < RZ_COLS; c++) {
            int d = fall[r][c] + entry[r][c];
            if (d > 0) {
                s_off[r][c].y = -cell_sz() * (float)d;
                s_fall_vel[r][c] = 0;
                s_falling[r][c] = true;
            }
        }
    s_bp = BP_FALL;
}

static void release_held(void)
{
    s_board.cell[s_held_r][s_held_c] = s_held_orb;
    s_dragging = false;

    if (!s_moved_any) { s_bp = BP_IDLE; return; }   /* a tap is not a move */

    /* the move is spent: actives charge one step */
    for (int i = 0; i < OFC_TEAM_SIZE; i++)
        if (s_skill_cd[i] > 0) s_skill_cd[i]--;

    s_time_burst = 0;
    s_turn_count = 0;
    s_combo = 0;
    s_cleared_board = false;
    s_step_dur = s_dbg_step_base;
    begin_pop_phase();
}

/* ----------------------------------------------------------- attack phase */

static Enemy *primary_target(void)
{
    for (int i = 0; i < s_foe_count; i++)
        if (s_foes[i].hp > 0) return &s_foes[i];
    return &s_foes[0];
}

static Vector2 foe_pos(int i)
{
    float x = s_foe_count == 1 ? sw() * 0.5f
                               : sw() * (i == 0 ? 0.30f : 0.70f);
    return (Vector2){ x, sh() * 0.225f };
}

static void hit_enemy(Enemy *e, long dmg, Vector2 at)
{
    char b[24];
    if (e->hp <= 0) return;

    if (e->def->combo_shield > 0 && s_combo < e->def->combo_shield) {
        spawn_floater(at, "SHIELDED", C_DIM, false);
        return;
    }
    if (e->def->void_over > 0 && dmg >= e->def->void_over &&
        !s_rules.vdp_pierce) {
        spawn_floater(at, "VOID", C_DIM, false);
        return;
    }

    long before = e->hp;
    e->hp -= dmg;
    if (e->hp <= 0 && e->def->resolve &&
        (float)before / (float)e->hp_max >= 0.30f) {
        e->hp = 1;
        spawn_floater((Vector2){ at.x, at.y - sh() * 0.03f },
                      "RESOLVE", C_BAD, false);
    }
    if (e->hp < 0) e->hp = 0;
    e->hurt_t = 0.22f;
    if (e->def->enrage_below > 0 && !e->enraged &&
        (float)e->hp / (float)e->hp_max < e->def->enrage_below) {
        e->enraged = true;
        spawn_floater((Vector2){ at.x, at.y - sh() * 0.05f },
                      "ENRAGED!", C_BAD, true);
    }

    ofc_fmt_num(dmg, b);
    spawn_floater(at, b, dmg > 0 ? C_TEXT : C_DIM, dmg >= 50000);
    play(s_snd_thud);
}

static void do_strike(int u)
{
    if (s_attack.dmg[u] <= 0) return;
    if (s_attack.mass[u]) {
        for (int i = 0; i < s_foe_count; i++)
            if (s_foes[i].hp > 0)
                hit_enemy(&s_foes[i], s_attack.dmg[u], foe_pos(i));
    } else {
        Enemy *t = primary_target();
        int ti = (int)(t - s_foes);
        hit_enemy(t, s_attack.dmg[u], foe_pos(ti));
    }
}

static bool all_foes_dead(void)
{
    for (int i = 0; i < s_foe_count; i++)
        if (s_foes[i].hp > 0) return false;
    return true;
}

/* ------------------------------------------------------------ enemy phase */

static void enemy_apply_intent(Enemy *e)
{
    const Intent *in = &e->def->pattern[e->intent_idx];
    float atk_mult = e->enraged ? e->def->enrage_mult : 1.0f;
    char b[24], t[32];

    switch (in->kind) {
    case INT_ATTACK:
    case INT_BIG: {
        long dmg = (long)((double)e->def->atk * (double)e->scale
                          * (double)in->value / 100.0 * (double)atk_mult);
        float red = 1.0f - (1.0f - s_shield_skill) * (1.0f - s_shield_cross);
        dmg = (long)((double)dmg * (double)(1.0f - red));
        if (dmg < 0) dmg = 0;
        s_hp -= dmg;
        ofc_fmt_num(dmg, b);
        snprintf(t, sizeof(t), "-%s", b);
        spawn_floater((Vector2){ sw() * 0.5f, board_top() - sh() * 0.11f },
                      t, C_BAD, in->kind == INT_BIG);
        s_shake = fminf(s_shake + cell_sz() * 0.06f, cell_sz() * 0.25f);
        play(s_snd_thud);
        break;
    }
    case INT_MULTI: {
        long total = 0;
        for (int i = 0; i < in->count; i++) {
            long dmg = (long)((double)in->value * (double)e->scale
                              * (double)atk_mult);
            float red = 1.0f - (1.0f - s_shield_skill) * (1.0f - s_shield_cross);
            dmg = (long)((double)dmg * (double)(1.0f - red));
            total += dmg;
        }
        s_hp -= total;
        ofc_fmt_num(total, b);
        snprintf(t, sizeof(t), "-%s x%d", b, in->count);
        spawn_floater((Vector2){ sw() * 0.5f, board_top() - sh() * 0.11f },
                      t, C_BAD, false);
        play(s_snd_thud);
        break;
    }
    case INT_JAMMERS:
        if (s_rules.hazard_immune)
            spawn_floater((Vector2){ sw() * 0.5f, sh() * 0.4f },
                          "IMMUNE", C_GOOD, false);
        else
            board_random_cells(in->count, cell_to_jammer);
        break;
    case INT_SEAL:
        if (s_rules.hazard_immune)
            spawn_floater((Vector2){ sw() * 0.5f, sh() * 0.4f },
                          "IMMUNE", C_GOOD, false);
        else
            board_random_cells(in->count, cell_seal);
        break;
    case INT_TIMECUT:
        s_timecut_turns = in->count + 1;
        s_timecut_secs = (float)in->value;
        set_banner("Move time cut!", 1.2f);
        break;
    case INT_HEAL: {
        long amt = e->hp_max * in->value / 100;
        e->hp += amt;
        if (e->hp > e->hp_max) e->hp = e->hp_max;
        ofc_fmt_num(amt, b);
        snprintf(t, sizeof(t), "+%s", b);
        int ei = (int)(e - s_foes);
        spawn_floater(foe_pos(ei), t, C_GOOD, false);
        break;
    }
    }
    e->intent_idx = (e->intent_idx + 1) % e->def->pattern_len;
}

/* ---------------------------------------------------------------- skills */

static void use_skill(int slot)
{
    if (s_skill_cd[slot] > 0 || s_bp != BP_IDLE) return;
    const SkillDef *sk = &s_units[slot].skill;
    char msg[48];
    snprintf(msg, sizeof(msg), "%s!", sk->name);
    set_banner(msg, 1.0f);
    play(s_snd_chime);

    switch (sk->kind) {
    case SK_CONVERT:    board_convert(sk->from_color, sk->to_color); break;
    case SK_ENHANCE:    board_enhance(sk->to_color); break;
    case SK_HEARTMAKE:  board_convert(sk->from_color, RZ_HEART); break;
    case SK_REFRESH:    board_refresh(); break;
    case SK_UNSEAL:     board_unseal_all(); break;
    case SK_DELAY:
        for (int i = 0; i < s_foe_count; i++)
            if (s_foes[i].hp > 0) s_foes[i].delay += sk->power;
        break;
    case SK_SHIELD:     s_shield_skill = (float)sk->power / 100.0f; break;
    case SK_TIME_BURST: s_time_burst = (float)sk->power; break;
    case SK_GRAVITY: {
        Enemy *t = primary_target();
        long dmg = t->hp * sk->power / 100;
        t->hp -= dmg;
        t->hurt_t = 0.25f;
        char b[24];
        ofc_fmt_num(dmg, b);
        spawn_floater(foe_pos((int)(t - s_foes)), b, C_TEXT, true);
        play(s_snd_thud);
        break;
    }
    case SK_NUKE_FIXED: {
        /* fixed damage: ignores DEF and voids (the high-DEF counter) */
        Enemy *t = primary_target();
        long before = t->hp;
        t->hp -= sk->power;
        if (t->hp <= 0 && t->def->resolve &&
            (float)before / (float)t->hp_max >= 0.30f) t->hp = 1;
        if (t->hp < 0) t->hp = 0;
        t->hurt_t = 0.25f;
        char b[24];
        ofc_fmt_num(sk->power, b);
        spawn_floater(foe_pos((int)(t - s_foes)), b, C_TEXT, false);
        play(s_snd_thud);
        if (all_foes_dead()) { s_bp = BP_WIN; s_strike_t = 0.8f; }
        break;
    }
    }
    s_skill_cd[slot] = sk->cooldown;
}

/* ----------------------------------------------------------- battle update */

static void battle_update(float dt)
{
    Vector2 ip = input_pos();
    s_finger = ip;

    for (int i = 0; i < s_foe_count; i++) {
        if (s_foes[i].hurt_t > 0) s_foes[i].hurt_t -= dt;
        if (s_foes[i].lunge_t > 0) s_foes[i].lunge_t -= dt;
    }

    /* orb settle animation */
    for (int r = 0; r < RZ_ROWS; r++)
        for (int c = 0; c < RZ_COLS; c++) {
            if (s_falling[r][c]) {
                s_fall_vel[r][c] += FALL_GRAVITY * cell_sz() * dt;
                s_off[r][c].y += s_fall_vel[r][c] * dt;
                if (s_off[r][c].y >= 0) {
                    s_off[r][c].y = 0;
                    s_fall_vel[r][c] = 0;
                    s_falling[r][c] = false;
                    if (s_bp == BP_FALL) play(s_snd_land);
                }
            } else {
                float k = fminf(SWAP_LERP_RATE * dt, 1.0f);
                s_off[r][c].x -= s_off[r][c].x * k;
                s_off[r][c].y -= s_off[r][c].y * k;
            }
        }

    switch (s_bp) {
    case BP_INTRO:
        s_intro_t -= dt;
        if (s_intro_t <= 0) s_bp = BP_IDLE;
        break;

    case BP_IDLE: {
        /* skill taps */
        for (int i = 0; i < OFC_TEAM_SIZE; i++)
            if (tapped(portrait_rect(i))) { use_skill(i); return; }

        /* orb pickup */
        if (s_pressed && s_press_pos.y >= board_top()) {
            int r, c;
            cell_at(s_press_pos, &r, &c);
            if (!rz_cell_empty(&s_board, r, c) &&
                !s_board.cell[r][c].sealed) {
                s_held_orb = s_board.cell[r][c];
                s_held_r = r; s_held_c = c;
                s_board.cell[r][c] = (RzOrb){ .color = RZ_COLOR_COUNT };
                s_dragging = true;
                s_timer_started = false;
                s_moved_any = false;
                s_move_time_max = move_time_now();
                s_move_timer = s_move_time_max;
                s_bp = BP_DRAG;
                play(s_snd_tick);
            }
        }
        break;
    }

    case BP_DRAG: {
        if (!s_dragging) { s_bp = BP_IDLE; break; }
        int nr, nc;
        cell_at(ip, &nr, &nc);
        if ((nr != s_held_r || nc != s_held_c) &&
            !s_board.cell[nr][nc].sealed &&
            !rz_cell_empty(&s_board, nr, nc)) {
            /* orb passed through swaps behind the held one */
            Vector2 from = cell_center(nr, nc);
            Vector2 to = cell_center(s_held_r, s_held_c);
            s_board.cell[s_held_r][s_held_c] = s_board.cell[nr][nc];
            s_off[s_held_r][s_held_c] =
                (Vector2){ from.x - to.x, from.y - to.y };
            s_board.cell[nr][nc] = (RzOrb){ .color = RZ_COLOR_COUNT };
            s_held_r = nr; s_held_c = nc;
            if (!s_timer_started) s_timer_started = true;
            s_moved_any = true;
            play(s_snd_tick);
        }
        if (s_timer_started) {
            s_move_timer -= dt;
            if (s_move_timer <= 0) { release_held(); break; }
        }
        if (s_released) release_held();
        break;
    }

    case BP_POP:
        s_step_timer -= dt;
        if (s_step_timer <= 0) {
            if (s_step_idx < s_step_count) {
                pop_current_match();
                s_step_idx++;
                s_step_dur = fmaxf(POP_STEP_FLOOR, s_step_dur * POP_STEP_ACCEL);
                s_step_timer = s_step_dur;
            } else {
                /* all matches this wave popped; board-clear check */
                bool empty = true;
                for (int r = 0; r < RZ_ROWS && empty; r++)
                    for (int c = 0; c < RZ_COLS && empty; c++)
                        if (!rz_cell_empty(&s_board, r, c)) empty = false;
                if (empty && !s_cleared_board) {
                    s_cleared_board = true;
                    s_jackpot_t = JACKPOT_HOLD;
                    s_flash = 1.0f;
                    s_shake = cell_sz() * 0.3f;
                    play(s_snd_chord);
                    s_bp = BP_JACKPOT;
                } else {
                    begin_fall_phase();
                }
            }
        }
        break;

    case BP_JACKPOT:
        s_jackpot_t -= dt;
        if (s_jackpot_t <= 0) begin_fall_phase();
        break;

    case BP_FALL: {
        bool done = true;
        for (int r = 0; r < RZ_ROWS && done; r++)
            for (int c = 0; c < RZ_COLS && done; c++)
                if (s_falling[r][c]) done = false;
        if (done) begin_pop_phase();
        break;
    }

    case BP_ATTACK:
        s_strike_t -= dt;
        if (s_strike_t <= 0) {
            s_strike_idx++;
            if (s_strike_idx < OFC_TEAM_SIZE) {
                do_strike(s_strike_idx);
                s_strike_t = 0.22f;
            } else {
                if (s_attack.total > s_best_burst)
                    s_best_burst = s_attack.total;
                if (all_foes_dead()) {
                    s_bp = BP_WIN;
                    s_strike_t = 0.9f;
                } else {
                    s_bp = BP_ENEMY;
                    s_enemy_idx = 0; s_enemy_t = 0; s_enemy_applied = false;
                }
            }
        }
        break;

    case BP_ENEMY: {
        /* find an acting foe */
        while (s_enemy_idx < s_foe_count && s_foes[s_enemy_idx].hp <= 0)
            s_enemy_idx++;
        if (s_enemy_idx >= s_foe_count) {
            /* turn wraps: shields and timers expire */
            s_shield_skill = s_shield_cross = 0;
            if (s_timecut_turns > 0) s_timecut_turns--;
            if (s_hp <= 0) { s_bp = BP_LOSE; s_strike_t = 0.9f; }
            else s_bp = BP_IDLE;
            break;
        }
        Enemy *e = &s_foes[s_enemy_idx];
        if (e->delay > 0 && !s_enemy_applied) {
            e->delay--;
            spawn_floater(foe_pos(s_enemy_idx), "DELAYED", C_DIM, false);
            s_enemy_applied = true;
            s_enemy_t = 0.35f;
        }
        if (!s_enemy_applied) {
            if (s_enemy_t == 0) e->lunge_t = 0.35f;
            s_enemy_t += dt;
            if (s_enemy_t >= 0.30f) {
                enemy_apply_intent(e);
                s_enemy_applied = true;
                s_enemy_t = 0.45f;
            }
        } else {
            s_enemy_t -= dt;
            if (s_enemy_t <= 0) {
                s_enemy_idx++;
                s_enemy_applied = false;
                s_enemy_t = 0;
            }
        }
        break;
    }

    case BP_WIN:
        s_strike_t -= dt;
        if (s_strike_t <= 0) {
            if (s_foes[0].def->is_boss) {
                s_state = GS_VICTORY;
            } else {
                roll_rewards(s_foes[0].def->is_elite);
                s_state = GS_REWARD;
            }
        }
        break;

    case BP_LOSE:
        s_strike_t -= dt;
        if (s_strike_t <= 0) s_state = GS_DEFEAT;
        break;
    }
}

/* ------------------------------------------------------------ orb drawing */

static void draw_orb_shape(Vector2 p, float r, uint8_t color, float alpha)
{
    Color body = Fade(ORB_COL[color], alpha);
    Color glyph = Fade((Color){ 255, 255, 255, 255 }, 0.85f * alpha);
    DrawCircleV(p, r, body);
    DrawCircleV((Vector2){ p.x - r * 0.30f, p.y - r * 0.32f }, r * 0.26f,
                Fade(WHITE, 0.25f * alpha));

    /* colorblind-safe glyphs: every color has a distinct inner shape */
    float g = r * 0.42f;
    switch (color) {
    case RZ_FIRE:
        DrawTriangle((Vector2){ p.x, p.y - g },
                     (Vector2){ p.x - g * 0.9f, p.y + g * 0.7f },
                     (Vector2){ p.x + g * 0.9f, p.y + g * 0.7f }, glyph);
        break;
    case RZ_WATER:
        DrawRing(p, g * 0.55f, g, 0, 360, 24, glyph);
        break;
    case RZ_WOOD:
        DrawPoly(p, 6, g, 0, glyph);
        break;
    case RZ_LIGHT:
        DrawPoly(p, 4, g, 45, glyph);
        break;
    case RZ_DARK:
        DrawTriangle((Vector2){ p.x, p.y + g },
                     (Vector2){ p.x + g * 0.9f, p.y - g * 0.7f },
                     (Vector2){ p.x - g * 0.9f, p.y - g * 0.7f }, glyph);
        break;
    case RZ_HEART: {
        float s = g * 0.62f;
        DrawCircleV((Vector2){ p.x - s * 0.55f, p.y - s * 0.35f }, s * 0.62f, glyph);
        DrawCircleV((Vector2){ p.x + s * 0.55f, p.y - s * 0.35f }, s * 0.62f, glyph);
        DrawTriangle((Vector2){ p.x - s * 1.12f, p.y - s * 0.12f },
                     (Vector2){ p.x, p.y + s * 1.05f },
                     (Vector2){ p.x + s * 1.12f, p.y - s * 0.12f }, glyph);
        break;
    }
    case RZ_JAMMER: {
        float t = g * 0.28f;
        DrawLineEx((Vector2){ p.x - g, p.y - g },
                   (Vector2){ p.x + g, p.y + g }, t, glyph);
        DrawLineEx((Vector2){ p.x - g, p.y + g },
                   (Vector2){ p.x + g, p.y - g }, t, glyph);
        break;
    }
    default: break;
    }
}

static void draw_orb(Vector2 p, float r, const RzOrb *o, float alpha)
{
    draw_orb_shape(p, r, o->color, alpha);
    if (o->enhanced) {
        DrawCircleLinesV(p, r + 2.0f, Fade(C_GOLD, alpha));
        DrawCircleV((Vector2){ p.x + r * 0.55f, p.y - r * 0.60f },
                    r * 0.14f, Fade(WHITE, alpha));
    }
    if (o->sealed) {
        float w = r * 1.6f;
        DrawRectanglePro((Rectangle){ p.x, p.y, w, r * 0.42f },
                         (Vector2){ w * 0.5f, r * 0.21f }, 35.0f,
                         Fade((Color){ 200, 205, 215, 255 }, 0.75f * alpha));
        DrawRectanglePro((Rectangle){ p.x, p.y, w, r * 0.42f },
                         (Vector2){ w * 0.5f, r * 0.21f }, -35.0f,
                         Fade((Color){ 200, 205, 215, 255 }, 0.75f * alpha));
    }
}

/* -------------------------------------------------------------- UI pieces */

static void draw_text_centered(const char *t, float cx, float y, int size,
                               Color col)
{
    int w = MeasureText(t, size);
    DrawText(t, (int)(cx - (float)w * 0.5f), (int)y, size, col);
}

static void draw_hp_bar(Rectangle r, float ratio, Color col, Color back)
{
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    DrawRectangleRounded(r, 0.6f, 6, back);
    Rectangle f = r;
    f.width *= ratio;
    if (f.width > 2)
        DrawRectangleRounded(f, 0.6f, 6, col);
}

static void draw_frame_stats(void)
{
    /* 1% low: avg of the worst 3 of the last 240 frames */
    float worst[3] = { 0, 0, 0 };
    for (int i = 0; i < FRAME_RING; i++) {
        float v = s_frame_ring[i];
        if (v > worst[0]) { worst[2] = worst[1]; worst[1] = worst[0]; worst[0] = v; }
        else if (v > worst[1]) { worst[2] = worst[1]; worst[1] = v; }
        else if (v > worst[2]) worst[2] = v;
    }
    float low = (worst[0] + worst[1] + worst[2]) / 3.0f;
    char b[64];
    snprintf(b, sizeof(b), "%d fps  1%% low %.1f ms", GetFPS(), low * 1000.0f);
    DrawText(b, 10, (int)(sh() * 0.5f), (int)(sw() / 34.0f),
             low > 0.018f ? C_BAD : C_DIM);
}

/* ------------------------------------------------------------ battle draw */

static void draw_foe(int i)
{
    Enemy *e = &s_foes[i];
    Vector2 p = foe_pos(i);
    float base = s_foe_count == 1 ? sw() * 0.17f : sw() * 0.13f;
    if (e->hp <= 0) {
        DrawCircleV(p, base * 0.5f, Fade(C_DIM, 0.25f));
        return;
    }
    float lunge = e->lunge_t > 0 ? sinf(e->lunge_t / 0.35f * PI) : 0;
    p.y += lunge * sh() * 0.02f;

    Color body = ORB_COL[e->def->color];
    if (e->hurt_t > 0) body = WHITE;
    if (e->enraged) body = (Color){ 255, 70, 70, 255 };

    /* flat vector body: rounded slab + eyes; horns for elite, crown for boss */
    Rectangle rb = { p.x - base, p.y - base * 0.8f, base * 2, base * 1.6f };
    DrawRectangleRounded(rb, 0.45f, 10, Fade(body, 0.92f));
    DrawRectangleRounded((Rectangle){ rb.x + base * 0.16f, rb.y + base * 0.16f,
                                      rb.width - base * 0.32f, base * 0.5f },
                         0.8f, 8, Fade(BLACK, 0.18f));
    float ey = p.y - base * 0.25f;
    DrawCircleV((Vector2){ p.x - base * 0.38f, ey }, base * 0.13f, C_BG);
    DrawCircleV((Vector2){ p.x + base * 0.38f, ey }, base * 0.13f, C_BG);
    if (e->def->is_elite || e->def->is_boss) {
        DrawTriangle((Vector2){ rb.x + base * 0.2f, rb.y },
                     (Vector2){ rb.x + base * 0.55f, rb.y - base * 0.55f },
                     (Vector2){ rb.x + base * 0.9f, rb.y }, Fade(body, 0.9f));
        DrawTriangle((Vector2){ rb.x + rb.width - base * 0.9f, rb.y },
                     (Vector2){ rb.x + rb.width - base * 0.55f,
                                rb.y - base * 0.55f },
                     (Vector2){ rb.x + rb.width - base * 0.2f, rb.y },
                     Fade(body, 0.9f));
    }
    if (e->def->is_boss)
        DrawTriangle((Vector2){ p.x - base * 0.25f, rb.y },
                     (Vector2){ p.x, rb.y - base * 0.7f },
                     (Vector2){ p.x + base * 0.25f, rb.y }, C_GOLD);

    /* name + hp with legible round numbers */
    int fs = (int)(sw() / 26.0f);
    draw_text_centered(e->def->name, p.x, p.y + base * 0.95f, fs, C_TEXT);
    char hb[24], mb[24], line[64];
    ofc_fmt_num(e->hp, hb);
    ofc_fmt_num(e->hp_max, mb);
    snprintf(line, sizeof(line), "%s / %s", hb, mb);
    Rectangle bar = { p.x - base, p.y + base * 0.95f + (float)fs * 1.3f,
                      base * 2, sh() * 0.011f };
    draw_hp_bar(bar, (float)e->hp / (float)e->hp_max, C_BAD, C_PANEL);
    draw_text_centered(line, p.x, bar.y + bar.height + 4,
                       (int)(sw() / 32.0f), C_DIM);

    /* gate tags */
    float tagy = bar.y + bar.height + (float)fs * 1.4f;
    char tags[80] = "";
    if (e->def->def > 0) {
        char db[24]; ofc_fmt_num(e->def->def, db);
        snprintf(tags, sizeof(tags), "DEF %s  ", db);
    }
    if (e->def->combo_shield > 0)
        snprintf(tags + strlen(tags), sizeof(tags) - strlen(tags),
                 "%d+ COMBOS  ", e->def->combo_shield);
    if (e->def->void_over > 0) {
        char vb[24]; ofc_fmt_num(e->def->void_over, vb);
        snprintf(tags + strlen(tags), sizeof(tags) - strlen(tags),
                 "VOID >%s", vb);
    }
    if (tags[0])
        draw_text_centered(tags, p.x, tagy, (int)(sw() / 34.0f), C_GOLD);

    /* intent */
    const Intent *in = &e->def->pattern[e->intent_idx];
    char intent[64];
    float amult = (e->enraged ? e->def->enrage_mult : 1.0f) * e->scale;
    switch (in->kind) {
    case INT_ATTACK: case INT_BIG: {
        char ab[24];
        ofc_fmt_num((long)((double)e->def->atk * (double)in->value / 100.0
                           * (double)amult), ab);
        snprintf(intent, sizeof(intent), in->kind == INT_BIG
                 ? "! attacks for %s" : "attacks for %s", ab);
        break;
    }
    case INT_MULTI: {
        char ab[24];
        ofc_fmt_num((long)((double)in->value * (double)amult), ab);
        snprintf(intent, sizeof(intent), "%d hits of %s", in->count, ab);
        break;
    }
    case INT_JAMMERS: snprintf(intent, sizeof(intent),
                               "throws %d jammers", in->count); break;
    case INT_SEAL:    snprintf(intent, sizeof(intent),
                               "seals %d orbs", in->count); break;
    case INT_TIMECUT: snprintf(intent, sizeof(intent),
                               "cuts move time -%ds", in->value); break;
    case INT_HEAL:    snprintf(intent, sizeof(intent),
                               "heals %d%%", in->value); break;
    default: intent[0] = 0; break;
    }
    if (e->delay > 0)
        snprintf(intent, sizeof(intent), "DELAYED %d", e->delay);
    long threat = 0;
    if (in->kind == INT_ATTACK || in->kind == INT_BIG)
        threat = (long)((double)e->def->atk * (double)in->value / 100.0
                        * (double)amult);
    if (in->kind == INT_MULTI)
        threat = (long)((double)in->value * (double)in->count * (double)amult);
    Color icol = threat >= s_hp * 8 / 10 && threat > 0 ? C_BAD : C_TEXT;
    draw_text_centered(intent, p.x, tagy + (float)fs * 1.1f,
                       (int)(sw() / 30.0f), icol);
}

static void draw_board(void)
{
    float cs = cell_sz();
    float bt = board_top();
    Vector2 shake = { 0, 0 };
    if (s_shake > 0.5f) {
        shake.x = (float)GetRandomValue(-100, 100) / 100.0f * s_shake;
        shake.y = (float)GetRandomValue(-100, 100) / 100.0f * s_shake;
    }

    DrawRectangle(0, (int)bt, (int)sw(), (int)(cs * RZ_ROWS), C_PANEL);
    for (int r = 0; r < RZ_ROWS; r++)
        for (int c = 0; c < RZ_COLS; c++) {
            Rectangle cr = { cs * (float)c + 2 + shake.x,
                             bt + cs * (float)r + 2 + shake.y,
                             cs - 4, cs - 4 };
            DrawRectangleRounded(cr, 0.22f, 6,
                                 ((r + c) & 1) ? C_PANEL_HI : C_PANEL);
        }

    float orb_r = cs * 0.44f;
    for (int r = 0; r < RZ_ROWS; r++)
        for (int c = 0; c < RZ_COLS; c++) {
            if (rz_cell_empty(&s_board, r, c)) continue;
            Vector2 p = cell_center(r, c);
            p.x += s_off[r][c].x + shake.x;
            p.y += s_off[r][c].y + shake.y;
            if (p.y < bt - cs * 0.5f) continue;   /* still above the board */
            draw_orb(p, orb_r, &s_board.cell[r][c], 1.0f);
        }

    /* pop ghosts: matched orbs swelling and fading */
    for (int i = 0; i < GHOST_MAX; i++) {
        Ghost *g = &s_ghosts[i];
        if (g->t <= 0) continue;
        float k = 1.0f - g->t / 0.28f;
        draw_orb_shape((Vector2){ g->pos.x + shake.x, g->pos.y + shake.y },
                       orb_r * (1.0f + 0.5f * k), g->color, 1.0f - k);
    }

    /* held orb: offset above the finger so it is never occluded */
    if (s_dragging) {
        Vector2 slot = cell_center(s_held_r, s_held_c);
        draw_orb_shape(slot, orb_r * 0.92f, s_held_orb.color, 0.30f);
        DrawCircleLinesV(slot, orb_r * 0.98f,
                         Fade(ORB_COL[s_held_orb.color], 0.6f));

        Vector2 hp = { s_finger.x, s_finger.y - cs * 1.2f };
        DrawCircleV((Vector2){ hp.x + 4, hp.y + 6 }, orb_r * 1.15f,
                    Fade(BLACK, 0.30f));
        draw_orb(hp, orb_r * 1.15f, &s_held_orb, 1.0f);

        /* draining ring, paused until the first swap */
        float frac = s_timer_started ? s_move_timer / s_move_time_max : 1.0f;
        Color rc = frac > 0.35f ? C_GOOD : C_BAD;
        DrawRing(hp, orb_r * 1.15f + 5, orb_r * 1.15f + 11,
                 -90, -90 + frac * 360.0f, 40, Fade(rc, 0.9f));
    }
}

static void draw_portraits(void)
{
    int fs = (int)(sw() / 34.0f);
    for (int i = 0; i < OFC_TEAM_SIZE; i++) {
        Rectangle r = portrait_rect(i);
        const UnitDef *u = &s_units[i];
        bool ready = s_skill_cd[i] == 0;
        DrawRectangleRounded(r, 0.2f, 6, C_PANEL);
        DrawRectangleRounded((Rectangle){ r.x, r.y, r.width, 5 }, 1.0f, 4,
                             ORB_COL[u->color]);
        char init[3] = { u->name[0], u->name[1], 0 };
        draw_text_centered(init, r.x + r.width * 0.5f,
                           r.y + r.height * 0.16f,
                           (int)(r.height * 0.42f),
                           Fade(ORB_COL[u->color], 0.95f));
        if (ready) {
            DrawRectangleRoundedLinesEx(r, 0.2f, 6, 2.0f, C_GOLD);
            draw_text_centered("READY", r.x + r.width * 0.5f,
                               r.y + r.height * 0.66f, fs, C_GOLD);
        } else {
            char cd[8];
            snprintf(cd, sizeof(cd), "%d", s_skill_cd[i]);
            draw_text_centered(cd, r.x + r.width * 0.5f,
                               r.y + r.height * 0.62f, fs, C_DIM);
        }
    }
}

static void draw_battle(void)
{
    /* enemy zone */
    for (int i = 0; i < s_foe_count; i++) draw_foe(i);

    /* leader condition, live during drag and resolution */
    OfcTurnStats st;
    if (s_bp == BP_DRAG || s_bp == BP_IDLE) preview_stats(&st);
    else ofc_turn_stats(s_turn_matches, s_turn_count, &st);
    float lm = 1.0f;
    const char *status = ofc_leader_status(&OFC_LEADERS[s_leader_idx], &st,
                                           (float)s_hp / (float)(s_hp_max
                                            ? s_hp_max : 1), &lm);
    int fs = (int)(sw() / 27.0f);
    float sy = sh() * 0.475f;
    draw_text_centered(OFC_LEADERS[s_leader_idx].name,
                       sw() * 0.5f, sy - (float)fs * 1.25f,
                       (int)(sw() / 33.0f), C_DIM);
    draw_text_centered(status, sw() * 0.5f, sy, fs,
                       lm > 1.0f ? C_GOOD : C_DIM);

    /* combo + climbing damage during the cascade */
    if (s_combo > 0 &&
        (s_bp == BP_POP || s_bp == BP_JACKPOT || s_bp == BP_FALL ||
         s_bp == BP_ATTACK)) {
        char cb[24];
        snprintf(cb, sizeof(cb), "%d COMBO", s_combo);
        float pulse = 1.0f + 0.10f * sinf((float)GetTime() * 9.0f);
        int cfs = (int)(sw() / 10.0f * (s_combo >= 6 ? pulse : 1.0f));
        draw_text_centered(cb, sw() * 0.5f, sh() * 0.36f, cfs,
                           s_combo >= 6 ? C_GOLD : C_TEXT);

        OfcAttack prev;
        ofc_compute_attack(s_turn_matches, s_turn_count,
                           (const UnitDef *[]){ &s_units[0], &s_units[1],
                                                &s_units[2], &s_units[3] },
                           &OFC_LEADERS[s_leader_idx], &s_rules,
                           (float)s_hp / (float)s_hp_max,
                           primary_target()->def->color,
                           primary_target()->def->def, &prev);
        char db[24], line[40];
        ofc_fmt_num(prev.total, db);
        snprintf(line, sizeof(line), "%s dmg", db);
        draw_text_centered(line, sw() * 0.5f, sh() * 0.36f + (float)cfs * 1.15f,
                           (int)(sw() / 16.0f), C_TEXT);
    }

    /* player HP bar */
    Rectangle hpr = { sw() * 0.05f, board_top() - sh() * 0.015f - 6,
                      sw() * 0.9f, sh() * 0.012f };
    hpr.y = portrait_rect(0).y - sh() * 0.030f;
    float ratio = (float)s_hp / (float)(s_hp_max ? s_hp_max : 1);
    draw_hp_bar(hpr, ratio, ratio > 0.35f ? C_GOOD : C_BAD, C_PANEL);
    char hb[24], mb[24], line[64];
    ofc_fmt_num(s_hp > 0 ? s_hp : 0, hb);
    ofc_fmt_num(s_hp_max, mb);
    snprintf(line, sizeof(line), "%s / %s", hb, mb);
    DrawText(line, (int)hpr.x, (int)(hpr.y - sw() / 30.0f - 4),
             (int)(sw() / 30.0f), C_TEXT);
    if (s_shield_skill > 0 || s_shield_cross > 0) {
        float red = 1.0f - (1.0f - s_shield_skill) * (1.0f - s_shield_cross);
        char sb[24];
        snprintf(sb, sizeof(sb), "SHIELD %.0f%%", red * 100.0f);
        int w = MeasureText(sb, (int)(sw() / 30.0f));
        DrawText(sb, (int)(hpr.x + hpr.width - (float)w),
                 (int)(hpr.y - sw() / 30.0f - 4), (int)(sw() / 30.0f), C_GOOD);
    }

    draw_portraits();
    draw_board();

    /* jackpot banner */
    if (s_bp == BP_JACKPOT) {
        DrawRectangle(0, 0, (int)sw(), (int)sh(), Fade(WHITE, 0.25f * s_flash));
        float t = 1.0f - s_jackpot_t / JACKPOT_HOLD;
        int jfs = (int)(sw() / 6.5f * (1.0f + 0.12f * sinf(t * PI)));
        draw_text_centered("ALL CLEAR!", sw() * 0.5f, sh() * 0.30f, jfs, C_GOLD);
    }
    if (s_flash > 0.02f && s_bp != BP_JACKPOT)
        DrawRectangle(0, 0, (int)sw(), (int)sh(), Fade(WHITE, 0.12f * s_flash));
}

/* ----------------------------------------------------------- menu screens */

static void draw_run_header(const char *title)
{
    int fs = (int)(sw() / 16.0f);
    draw_text_centered(title, sw() * 0.5f, sh() * 0.055f, fs, C_TEXT);
    char b[48];
    snprintf(b, sizeof(b), "floor %d   relics %d   seed %u",
             s_floor + 1, s_relic_count, (unsigned)(s_run_seed & 0xFFFF));
    draw_text_centered(b, sw() * 0.5f, sh() * 0.055f + (float)fs * 1.2f,
                       (int)(sw() / 34.0f), C_DIM);
}

static void title_update(void)
{
    if (s_released) s_state = GS_PICK_LEADER;
}

static void title_draw(void)
{
    float t = (float)GetTime();
    for (int i = 0; i < 6; i++) {
        float x = sw() * (0.5f + 0.38f * cosf(t * 0.4f + (float)i * 1.047f));
        float y = sh() * (0.30f + 0.10f * sinf(t * 0.7f + (float)i * 1.047f));
        draw_orb_shape((Vector2){ x, y }, sw() * 0.045f, (uint8_t)i, 0.9f);
    }
    draw_text_centered("ORBFALL", sw() * 0.5f, sh() * 0.40f,
                       (int)(sw() / 5.5f), C_TEXT);
    draw_text_centered("drag orbs · chain cascades · build the run",
                       sw() * 0.5f, sh() * 0.40f + sw() / 5.0f,
                       (int)(sw() / 26.0f), C_DIM);
    if (fmodf(t, 1.2f) < 0.8f)
        draw_text_centered("TAP TO BEGIN", sw() * 0.5f, sh() * 0.72f,
                           (int)(sw() / 18.0f), C_GOLD);
}

static void pick_update(void)
{
    for (int i = 0; i < OFC_LEADER_COUNT; i++)
        if (tapped(card_rect(i, OFC_LEADER_COUNT))) {
            s_run_seed = (uint64_t)GetRandomValue(1, 0x7FFFFFFF);
            s_rng.s = s_run_seed;
            start_run(i);
            return;
        }
}

static void pick_draw(void)
{
    draw_text_centered("CHOOSE YOUR LEADER", sw() * 0.5f, sh() * 0.06f,
                       (int)(sw() / 14.0f), C_TEXT);
    draw_text_centered("the condition is the build",
                       sw() * 0.5f, sh() * 0.06f + sw() / 13.0f,
                       (int)(sw() / 28.0f), C_DIM);
    for (int i = 0; i < OFC_LEADER_COUNT; i++) {
        Rectangle r = card_rect(i, OFC_LEADER_COUNT);
        const LeaderDef *ld = &OFC_LEADERS[i];
        DrawRectangleRounded(r, 0.18f, 8, C_PANEL);
        DrawRectangleRounded((Rectangle){ r.x, r.y, 7, r.height }, 1.0f, 4,
                             ORB_COL[ld->unit.color]);
        int fs1 = (int)(r.height * 0.26f);
        int fs2 = (int)(r.height * 0.185f);
        DrawText(ld->name, (int)(r.x + r.width * 0.045f),
                 (int)(r.y + r.height * 0.14f), fs1, C_TEXT);
        DrawText(ld->desc, (int)(r.x + r.width * 0.045f),
                 (int)(r.y + r.height * 0.50f), fs2, C_DIM);
    }
}

static void map_update(void)
{
    for (int f = 0; f < MAP_FLOORS; f++) {
        int n = floor_node_count(f);
        for (int c = 0; c < n; c++) {
            if (!node_reachable(f, c)) continue;
            Vector2 p = map_node_pos(f, c, n);
            Rectangle r = { p.x - sw() * 0.075f, p.y - sw() * 0.075f,
                            sw() * 0.15f, sw() * 0.15f };
            if (tapped(r)) { enter_node(f, c); return; }
        }
    }
}

static void map_draw(void)
{
    draw_run_header("THE DESCENT");

    /* edges first */
    for (int f = 0; f < MAP_FLOORS - 1; f++) {
        int n = floor_node_count(f), n2 = floor_node_count(f + 1);
        for (int c = 0; c < n; c++)
            for (int c2 = 0; c2 < n2; c2++) {
                Vector2 a = map_node_pos(f, c, n);
                Vector2 b = map_node_pos(f + 1, c2, n2);
                if (fabsf(a.x - b.x) > sw() * 0.33f) continue;
                DrawLineEx(a, b, 2.0f, Fade(C_DIM, 0.35f));
            }
    }

    float t = (float)GetTime();
    for (int f = 0; f < MAP_FLOORS; f++) {
        int n = floor_node_count(f);
        for (int c = 0; c < n; c++) {
            MapNode *nd = &s_map[f][c];
            Vector2 p = map_node_pos(f, c, n);
            float r = sw() * 0.052f;
            bool reach = node_reachable(f, c);
            bool here = (f == s_floor && c == s_col);

            Color fill = C_PANEL_HI;
            if (nd->done) fill = C_PANEL;
            if (reach) fill = C_PANEL_HI;
            float rr = r * (reach ? 1.0f + 0.07f * sinf(t * 5.0f) : 1.0f);
            DrawCircleV(p, rr + 4, Fade(C_BG, 0.6f));
            DrawCircleV(p, rr, fill);
            if (reach) DrawCircleLinesV(p, rr + 3, C_GOLD);
            if (here)  DrawCircleLinesV(p, rr + 3, C_GOOD);

            Color icol = nd->done ? C_DIM : C_TEXT;
            switch (nd->type) {
            case NODE_FIGHT:
                DrawLineEx((Vector2){ p.x - r * 0.4f, p.y + r * 0.4f },
                           (Vector2){ p.x + r * 0.4f, p.y - r * 0.4f },
                           3, icol);
                DrawLineEx((Vector2){ p.x - r * 0.4f, p.y - r * 0.4f },
                           (Vector2){ p.x + r * 0.4f, p.y + r * 0.4f },
                           3, icol);
                break;
            case NODE_ELITE:
                DrawPoly(p, 3, r * 0.55f, -90, C_BAD);
                break;
            case NODE_EVENT:
                draw_text_centered("?", p.x, p.y - r * 0.55f,
                                   (int)(r * 1.2f), icol);
                break;
            case NODE_REST:
                DrawTriangle((Vector2){ p.x, p.y - r * 0.5f },
                             (Vector2){ p.x - r * 0.5f, p.y + r * 0.4f },
                             (Vector2){ p.x + r * 0.5f, p.y + r * 0.4f },
                             (Color){ 255, 150, 70, 255 });
                break;
            case NODE_BOSS:
                DrawTriangle((Vector2){ p.x - r * 0.55f, p.y + r * 0.3f },
                             (Vector2){ p.x - r * 0.55f, p.y - r * 0.35f },
                             (Vector2){ p.x - r * 0.15f, p.y + r * 0.05f },
                             C_GOLD);
                DrawTriangle((Vector2){ p.x - r * 0.15f, p.y + r * 0.05f },
                             (Vector2){ p.x, p.y - r * 0.5f },
                             (Vector2){ p.x + r * 0.15f, p.y + r * 0.05f },
                             C_GOLD);
                DrawTriangle((Vector2){ p.x + r * 0.15f, p.y + r * 0.05f },
                             (Vector2){ p.x + r * 0.55f, p.y - r * 0.35f },
                             (Vector2){ p.x + r * 0.55f, p.y + r * 0.3f },
                             C_GOLD);
                DrawRectangle((int)(p.x - r * 0.55f), (int)(p.y + r * 0.05f),
                              (int)(r * 1.1f), (int)(r * 0.3f), C_GOLD);
                break;
            default: break;
            }
        }
    }
    draw_text_centered("tap a glowing node", sw() * 0.5f, sh() * 0.93f,
                       (int)(sw() / 28.0f), C_DIM);
}

static void reward_update(void)
{
    for (int i = 0; i < 3; i++)
        if (tapped(card_rect(i, 4))) {
            gain_relic((RelicId)s_reward_ids[i]);
            s_state = GS_MAP;
            return;
        }
    if (tapped(card_rect(3, 4))) {  /* skip = small heal */
        s_hp += s_hp_max / 10;
        if (s_hp > s_hp_max) s_hp = s_hp_max;
        s_state = GS_MAP;
    }
}

static void draw_relic_card(Rectangle r, RelicId id)
{
    const RelicDef *rd = &OFC_RELICS[id];
    DrawRectangleRounded(r, 0.18f, 8, C_PANEL);
    DrawRectangleRounded((Rectangle){ r.x, r.y, 7, r.height }, 1.0f, 4,
                         rd->utility ? C_DIM : C_GOLD);
    DrawText(rd->name, (int)(r.x + r.width * 0.045f),
             (int)(r.y + r.height * 0.14f), (int)(r.height * 0.26f), C_TEXT);
    DrawText(rd->desc, (int)(r.x + r.width * 0.045f),
             (int)(r.y + r.height * 0.50f), (int)(r.height * 0.175f), C_DIM);
}

static void reward_draw(void)
{
    draw_run_header("VICTORY — CHOOSE A RELIC");
    for (int i = 0; i < 3; i++)
        draw_relic_card(card_rect(i, 4), (RelicId)s_reward_ids[i]);
    Rectangle r = card_rect(3, 4);
    DrawRectangleRounded(r, 0.18f, 8, C_PANEL);
    draw_text_centered("skip — recover 10% HP", r.x + r.width * 0.5f,
                       r.y + r.height * 0.38f, (int)(r.height * 0.24f), C_DIM);
}

/* events: every option is a tradeoff or a build push */
typedef struct { const char *title, *body, *a, *b; } EventDef;
static const EventDef s_events[4] = {
    { "Sealed Shrine",
      "An altar hums behind cracked wards. Prying it open will cost you.",
      "Take the relic inside (-15% max HP)", "Rest at the shrine (heal 30%)" },
    { "The Orb Font",
      "A spring of raw orb-light. Drinking attunes your skyfall.",
      "Attune it (gain Attuned Sky)", "Bottle it (gain Enhanced Sky)" },
    { "Blood Pact",
      "A voice offers power for blood. It does not haggle.",
      "Sign (+30% ATK, -20% max HP)", "Refuse (heal 20%)" },
    { "Wandering Trainer",
      "A retired dragon-tamer sizes up your team.",
      "Spar (+15% ATK to all units)", "Listen (+20% max HP)" },
};

static void event_apply(int id, bool a)
{
    if (a) {
        switch (id) {
        case 0:
            gain_relic(roll_relic(true));
            s_hp_max = s_hp_max * 85 / 100;
            if (s_hp > s_hp_max) s_hp = s_hp_max;
            break;
        case 1: gain_relic(RELIC_ATTUNED_SKY); break;
        case 2:
            for (int i = 0; i < OFC_TEAM_SIZE; i++)
                s_units[i].atk = s_units[i].atk * 130 / 100;
            s_hp_max = s_hp_max * 80 / 100;
            if (s_hp > s_hp_max) s_hp = s_hp_max;
            break;
        case 3:
            for (int i = 0; i < OFC_TEAM_SIZE; i++)
                s_units[i].atk = s_units[i].atk * 115 / 100;
            break;
        }
    } else {
        switch (id) {
        case 0: s_hp += s_hp_max * 30 / 100; break;
        case 1: gain_relic(RELIC_ENHANCED_SKY); break;
        case 2: s_hp += s_hp_max * 20 / 100; break;
        case 3:
            for (int i = 0; i < OFC_TEAM_SIZE; i++)
                s_units[i].hp = s_units[i].hp * 120 / 100;
            rebuild_rules();
            break;
        }
    }
    if (s_hp > s_hp_max) s_hp = s_hp_max;
    s_state = GS_MAP;
}

static void event_update(void)
{
    if (tapped(card_rect(1, 3))) { event_apply(s_event_id, true); return; }
    if (tapped(card_rect(2, 3))) { event_apply(s_event_id, false); return; }
}

static void event_draw(void)
{
    const EventDef *e = &s_events[s_event_id];
    draw_run_header(e->title);

    Rectangle rb = card_rect(0, 3);
    DrawRectangleRounded(rb, 0.18f, 8, Fade(C_PANEL, 0.6f));
    DrawText(e->body, (int)(rb.x + rb.width * 0.045f),
             (int)(rb.y + rb.height * 0.2f), (int)(rb.height * 0.19f), C_DIM);

    Rectangle ra = card_rect(1, 3);
    DrawRectangleRounded(ra, 0.18f, 8, C_PANEL);
    DrawRectangleRounded((Rectangle){ ra.x, ra.y, 7, ra.height }, 1.0f, 4, C_GOLD);
    draw_text_centered(e->a, ra.x + ra.width * 0.5f, ra.y + ra.height * 0.38f,
                       (int)(ra.height * 0.21f), C_TEXT);
    Rectangle rc = card_rect(2, 3);
    DrawRectangleRounded(rc, 0.18f, 8, C_PANEL);
    draw_text_centered(e->b, rc.x + rc.width * 0.5f, rc.y + rc.height * 0.38f,
                       (int)(rc.height * 0.21f), C_TEXT);
}

static void rest_update(void)
{
    if (tapped(card_rect(1, 3))) {
        s_hp += s_hp_max * 35 / 100;
        if (s_hp > s_hp_max) s_hp = s_hp_max;
        s_state = GS_MAP;
    }
    if (tapped(card_rect(2, 3))) {
        for (int i = 0; i < OFC_TEAM_SIZE; i++)
            s_units[i].atk = s_units[i].atk * 115 / 100;
        s_state = GS_MAP;
    }
}

static void rest_draw(void)
{
    draw_run_header("CAMPFIRE");
    Rectangle ra = card_rect(1, 3);
    DrawRectangleRounded(ra, 0.18f, 8, C_PANEL);
    draw_text_centered("Rest — heal 35% HP", ra.x + ra.width * 0.5f,
                       ra.y + ra.height * 0.38f, (int)(ra.height * 0.22f),
                       C_GOOD);
    Rectangle rc = card_rect(2, 3);
    DrawRectangleRounded(rc, 0.18f, 8, C_PANEL);
    draw_text_centered("Train — +15% ATK to all units", rc.x + rc.width * 0.5f,
                       rc.y + rc.height * 0.38f, (int)(rc.height * 0.22f),
                       C_GOLD);
}

static void end_draw(bool won)
{
    draw_text_centered(won ? "THE DESCENT IS YOURS" : "THE RUN ENDS HERE",
                       sw() * 0.5f, sh() * 0.28f, (int)(sw() / 11.0f),
                       won ? C_GOLD : C_BAD);
    char b1[24], line[64];
    ofc_fmt_num(s_best_burst, b1);
    snprintf(line, sizeof(line), "best burst  %s", b1);
    draw_text_centered(line, sw() * 0.5f, sh() * 0.42f,
                       (int)(sw() / 18.0f), C_TEXT);
    snprintf(line, sizeof(line), "best combo  %d", s_best_combo);
    draw_text_centered(line, sw() * 0.5f, sh() * 0.42f + sw() / 14.0f,
                       (int)(sw() / 18.0f), C_TEXT);
    snprintf(line, sizeof(line), "floors cleared  %d / %d",
             s_floor + 1, MAP_FLOORS);
    draw_text_centered(line, sw() * 0.5f, sh() * 0.42f + sw() / 7.0f,
                       (int)(sw() / 18.0f), C_TEXT);
    draw_text_centered("tap to return", sw() * 0.5f, sh() * 0.75f,
                       (int)(sw() / 22.0f), C_DIM);
}

/* ----------------------------------------------------------- relic drawer */

static Rectangle relic_bar_rect(void)
{
    return (Rectangle){ 0, 0, sw(), sh() * 0.035f };
}

static void relic_overlay_draw(void)
{
    DrawRectangle(0, 0, (int)sw(), (int)sh(), Fade(C_BG, 0.92f));
    draw_text_centered("RELICS", sw() * 0.5f, sh() * 0.05f,
                       (int)(sw() / 14.0f), C_TEXT);
    int fs = (int)(sw() / 30.0f);
    float y = sh() * 0.14f;
    for (int i = 0; i < s_relic_count; i++) {
        const RelicDef *rd = &OFC_RELICS[s_relics[i]];
        DrawCircleV((Vector2){ sw() * 0.07f, y + (float)fs * 0.5f },
                    (float)fs * 0.45f, rd->utility ? C_DIM : C_GOLD);
        DrawText(rd->name, (int)(sw() * 0.12f), (int)y, fs, C_TEXT);
        DrawText(rd->desc, (int)(sw() * 0.12f), (int)(y + (float)fs * 1.1f),
                 (int)((float)fs * 0.8f), C_DIM);
        y += (float)fs * 2.6f;
    }
    if (s_relic_count == 0)
        draw_text_centered("none yet — win fights", sw() * 0.5f, sh() * 0.3f,
                           fs, C_DIM);
    draw_text_centered("tap to close", sw() * 0.5f, sh() * 0.92f, fs, C_DIM);
}

/* ---------------------------------------------------------- debug overlay */

static Rectangle dbg_btn_rect(void)
{
    return (Rectangle){ 6, sh() * 0.55f, sw() * 0.11f, sh() * 0.030f };
}

static void debug_draw(void)
{
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)(sw() / 36.0f));
    Rectangle b = dbg_btn_rect();
    if (GuiButton(b, s_dbg_open ? "close" : "dbg")) s_dbg_open = !s_dbg_open;
    if (!s_dbg_open) return;

    draw_frame_stats();
    float x = 10, w = sw() * 0.55f, h = sh() * 0.025f;
    float y = sh() * 0.585f;
    GuiSlider((Rectangle){ x + sw() * 0.18f, y, w * 0.6f, h },
              "step", TextFormat("%.2fs", s_dbg_step_base),
              &s_dbg_step_base, 0.10f, 0.40f);
    y += h * 1.6f;
    GuiSlider((Rectangle){ x + sw() * 0.18f, y, w * 0.6f, h },
              "time", TextFormat("%.1fs", s_dbg_move_time),
              &s_dbg_move_time, 3.0f, 10.0f);
    y += h * 1.6f;
    GuiSlider((Rectangle){ x + sw() * 0.18f, y, w * 0.6f, h },
              "juice", TextFormat("%.1fx", s_dbg_juice),
              &s_dbg_juice, 0.0f, 2.0f);
    y += h * 1.6f;
    if (s_state == GS_BATTLE) {
        if (GuiButton((Rectangle){ x, y, sw() * 0.26f, h * 1.3f }, "reroll board"))
            board_refresh();
        if (GuiButton((Rectangle){ x + sw() * 0.28f, y, sw() * 0.26f, h * 1.3f },
                      "win fight")) {
            for (int i = 0; i < s_foe_count; i++) s_foes[i].hp = 0;
            if (s_bp == BP_IDLE || s_bp == BP_DRAG) {
                s_bp = BP_WIN; s_strike_t = 0.2f;
            }
        }
    }
}

/* ---------------------------------------------------------- proto callbacks */

static void orbfall_reset(void)
{
    s_state = GS_TITLE;
    s_bp = BP_IDLE;
    s_dragging = false;
    s_relic_count = 0;
    s_show_relics = false;
    s_dbg_open = false;
    s_prev_tc = 0;
    s_shake = s_flash = 0;
    s_banner_t = 0;
    memset(s_parts, 0, sizeof(s_parts));
    memset(s_floaters, 0, sizeof(s_floaters));
    memset(s_ghosts, 0, sizeof(s_ghosts));
    for (int i = 0; i < FLOATER_MAX; i++) s_floaters[i].life = -1;
    for (int i = 0; i < PARTICLE_MAX; i++) s_parts[i].life = -1;
    reset_offsets();
}

static void OrbfallInit(void)
{
    orbfall_reset();
    audio_init();
}

static void OrbfallUpdate(float dt)
{
    if (dt > 0.1f) dt = 0.1f;
    s_frame_ring[s_frame_idx] = dt;
    s_frame_idx = (s_frame_idx + 1) % FRAME_RING;

    s_finger = input_pos();
    input_poll();
    juice_update(dt);

    if (s_show_relics) {
        if (s_released) s_show_relics = false;
        return;
    }
    /* relic drawer opens from the top bar on run screens */
    /* (right edge excluded: the launcher's back button lives there) */
    if ((s_state == GS_MAP || s_state == GS_BATTLE) &&
        s_released && s_press_pos.y < relic_bar_rect().height &&
        s_press_pos.x < sw() * 0.80f &&
        s_finger.y < relic_bar_rect().height * 2) {
        s_show_relics = true;
        return;
    }

    switch (s_state) {
    case GS_TITLE:       title_update(); break;
    case GS_PICK_LEADER: pick_update(); break;
    case GS_MAP:         map_update(); break;
    case GS_BATTLE:      battle_update(dt); break;
    case GS_REWARD:      reward_update(); break;
    case GS_EVENT:       event_update(); break;
    case GS_REST:        rest_update(); break;
    case GS_DEFEAT:
    case GS_VICTORY:     if (s_released) s_state = GS_TITLE; break;
    }
}

static void draw_particles_floaters(void)
{
    for (int i = 0; i < PARTICLE_MAX; i++) {
        Particle *p = &s_parts[i];
        if (p->t >= p->life || p->life <= 0) continue;
        float a = 1.0f - p->t / p->life;
        DrawCircleV(p->pos, p->r * a, Fade(p->col, a));
    }
    for (int i = 0; i < FLOATER_MAX; i++) {
        Floater *f = &s_floaters[i];
        if (f->t >= f->life || f->life <= 0) continue;
        float a = 1.0f - f->t / f->life;
        int fs = (int)(sw() / (f->big ? 12.0f : 22.0f));
        draw_text_centered(f->text, f->pos.x, f->pos.y, fs, Fade(f->col, a));
    }
}

static void draw_relic_bar(void)
{
    Rectangle r = relic_bar_rect();
    DrawRectangleRec(r, Fade(C_PANEL, 0.85f));
    float x = sw() * 0.02f;
    float cy = r.y + r.height * 0.5f;
    for (int i = 0; i < s_relic_count && i < OFC_MAX_RELICS; i++) {
        const RelicDef *rd = &OFC_RELICS[s_relics[i]];
        DrawCircleV((Vector2){ x, cy }, r.height * 0.32f,
                    rd->utility ? C_DIM : C_GOLD);
        x += r.height * 0.85f;
    }
    const char *lbl = s_relic_count > 0 ? "relics (tap)" : "no relics yet";
    int fs = (int)(r.height * 0.6f);
    DrawText(lbl, (int)(sw() - (float)MeasureText(lbl, fs) - sw() * 0.02f),
             (int)(r.y + r.height * 0.2f), fs, C_DIM);
}

static void OrbfallDraw(void)
{
    ClearBackground(C_BG);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)(sw() / 36.0f));

    switch (s_state) {
    case GS_TITLE:       title_draw(); break;
    case GS_PICK_LEADER: pick_draw(); break;
    case GS_MAP:         map_draw(); draw_relic_bar(); break;
    case GS_BATTLE:      draw_battle(); draw_relic_bar(); break;
    case GS_REWARD:      reward_draw(); break;
    case GS_EVENT:       event_draw(); break;
    case GS_REST:        rest_draw(); break;
    case GS_DEFEAT:      end_draw(false); break;
    case GS_VICTORY:     end_draw(true); break;
    }

    draw_particles_floaters();

    if (s_banner_t > 0) {
        float a = s_banner_t > 0.3f ? 1.0f : s_banner_t / 0.3f;
        draw_text_centered(s_banner, sw() * 0.5f, sh() * 0.52f,
                           (int)(sw() / 15.0f), Fade(C_GOLD, a));
    }

    if (s_show_relics) relic_overlay_draw();
    debug_draw();
}

static void OrbfallDeinit(void)
{
    audio_deinit();
}

/* observability hooks for the headless smoke test (tests/) */
int OrbfallDbgState(void) { return (int)s_state; }
int OrbfallDbgPhase(void) { return (int)s_bp; }

const Prototype OrbfallProto = {
    .name        = "Orbfall",
    .description = "Orb-drag roguelike: P&D cascades, StS map, relic builds.",
    .Init        = OrbfallInit,
    .Update      = OrbfallUpdate,
    .Draw        = OrbfallDraw,
    .Deinit      = OrbfallDeinit,
};
