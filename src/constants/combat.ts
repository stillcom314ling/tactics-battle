/**
 * COMBAT CONSTANTS
 *
 * Damage values, durations, ranges, and player starting stats.
 * Centralised here so balance tuning is a single-file edit.
 */

// ── Status effect durations (turns) ─────────────────────────────────────────
// Mirrors STATUS_DURATIONS in ECS.ts — ECS.ts imports from here so there is
// one canonical source.

export const STATUS_DURATION_BURNING  = 3;
export const STATUS_DURATION_POISONED = 4;
export const STATUS_DURATION_SLOWED   = 2;
export const STATUS_DURATION_STUNNED  = 1;
export const STATUS_DURATION_SHOCKED  = 1;
export const STATUS_DURATION_WET      = 3;

// ── Passive damage from status effects (per turn_start tick) ─────────────────

export const BURNING_DAMAGE_PER_TURN  = 3;
export const POISON_DAMAGE_PER_TURN   = 2;
export const FIRE_TERRAIN_DAMAGE      = 5;
/** Burning duration applied when standing on on_fire terrain. */
export const FIRE_TERRAIN_BURN_TURNS  = 3;

// ── Enemy melee randomness ────────────────────────────────────────────────────

/** Random bonus added to melee damage (actual += rand(0, MELEE_VARIANCE - 1)). */
export const MELEE_VARIANCE = 4;

// ── Alert system ──────────────────────────────────────────────────────────────

/** Chebyshev radius alerted when the player casts a spell. */
export const ALERT_RADIUS_SPELL  = 5;
/** Chebyshev radius alerted when an enemy lands a melee hit on the player. */
export const ALERT_RADIUS_MELEE  = 4;

// ── Ranged enemy ─────────────────────────────────────────────────────────────

/** Euclidean range at which the Archer attempts a ranged shot. */
export const ARCHER_SHOOT_RANGE  = 5;
/** Damage dealt by an Archer ranged attack (before defense). */
export const ARCHER_SHOT_DAMAGE  = 5;
/** Chebyshev distance at which an un-alerted Archer tries to back away. */
export const ARCHER_BACK_OFF_DIST = 2;

// ── Player starting stats ─────────────────────────────────────────────────────

export const PLAYER_HP_MAX        = 100;
export const PLAYER_MOVE_RANGE    = 5;
export const PLAYER_ATTACK_POWER  = 15;
export const PLAYER_DEFENSE       = 3;
export const PLAYER_ATTACK_RANGE  = 1;

// ── Turn timing ───────────────────────────────────────────────────────────────

/** Milliseconds between end-of-player-turn and start of enemy processing. */
export const ENEMY_TURN_DELAY_MS = 140;
