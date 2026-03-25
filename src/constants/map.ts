/**
 * MAP CONSTANTS
 *
 * All values that define the shape and content of generated maps.
 * Change these to resize the world, adjust dungeon feel, or tune
 * how many enemies appear per floor.
 */

// ── Grid dimensions ──────────────────────────────────────────────────────────

/** Number of tile columns in the gameplay layer. */
export const MAP_W = 32;
/** Number of tile rows in the gameplay layer. */
export const MAP_H = 22;

// ── Dungeon generator options (passed to rot.js Digger) ──────────────────────

export const DUNGEON_OPTIONS = {
  roomWidth:      [4, 9]  as [number, number],
  roomHeight:     [3, 6]  as [number, number],
  corridorLength: [2, 5]  as [number, number],
  dugPercentage:  0.4,
} as const;

// ── ASCII tile sets ───────────────────────────────────────────────────────────

export const FLOOR_CHARS  = ['.', '·', ',', '`'] as const;
export const WALL_CHARS   = ['#', '█', '▓', '▒'] as const;
/** bg_far star/dot chars — used for the sparse starfield dots */
export const BG_FAR_CHARS = ['·', '∙', '✦', '∗', '.'] as const;
/** bg_mid fill chars — used for sparse scatter between pillars/beams */
export const BG_MID_CHARS = ['░', '·', '∘', '°', '▪'] as const;
/** foreground sparkle chars — floating near-plane particles */
export const FG_CHARS     = ['✦', '✧', '∗', '◆', '◇', '·', '∙'] as const;

// ── Enemy spawn tuning ────────────────────────────────────────────────────────

/** Minimum enemies spawned per floor. */
export const ENEMY_COUNT_MIN = 9;
/** Random variance added on top of MIN (actual = MIN + rand(0, VARIANCE)). */
export const ENEMY_COUNT_VARIANCE = 6;
