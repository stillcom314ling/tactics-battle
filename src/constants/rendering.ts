/**
 * RENDERING CONSTANTS
 *
 * Layer depth values, cell sizes, color palettes, and camera physics.
 *
 * PARALLAX DEPTH PRIMER
 * ---------------------
 * depth < 1  → layer moves SLOWER than camera  (feels far away / background)
 * depth = 1  → layer moves 1:1 with camera     (the gameplay layer)
 * depth > 1  → layer moves FASTER than camera  (feels close / foreground)
 */

// ── Cell / grid ───────────────────────────────────────────────────────────────

/** Pixel size of one ASCII cell on the gameplay layer. */
export const CELL_SIZE = 24;

// ── Parallax layer depths ─────────────────────────────────────────────────────

export const DEPTH_BG_FAR    = 0.2;
export const DEPTH_BG_MID    = 0.5;
export const DEPTH_GAMEPLAY  = 1.0;
export const DEPTH_FOREGROUND = 1.3;

// ── Layer size multipliers (relative to MAP_W / MAP_H) ───────────────────────

/** Background layers are larger than the gameplay grid to fill parallax gaps. */
export const BG_LAYER_SCALE = 1.4;
/** Foreground layer is slightly larger to allow foreground bleed. */
export const FG_LAYER_SCALE = 1.2;

// ── Layer alpha / tint (atmospheric depth simulation) ────────────────────────

export const BG_FAR_ALPHA = 0.4;
export const BG_FAR_TINT  = 0x8888aa;
export const BG_MID_ALPHA = 0.5;
export const BG_MID_TINT  = 0x9999bb;
export const GAMEPLAY_ALPHA    = 1.0;
export const FOREGROUND_ALPHA  = 0.3;

// ── Background color ──────────────────────────────────────────────────────────

/** PixiJS app backgroundColor — very dark blue-black. */
export const APP_BG_COLOR = 0x0a0a0c;

// ── Tile color palettes ───────────────────────────────────────────────────────

export const WALL_COLORS   = [0x445566, 0x334455, 0x3a4a5a, 0x2d3d4d] as const;
export const FLOOR_COLORS  = [0x556677, 0x667788, 0x5a6a7a] as const;
export const BG_FAR_COLORS = [0x1a1a2e, 0x16213e, 0x0f1626] as const;
export const BG_MID_COLORS = [0x222244, 0x1a1a3a, 0x2a2a44] as const;
export const FG_COLORS     = [0x8899aa, 0x99aabb, 0x778899] as const;

// ── Element colours (used for targeting highlights and action menu) ───────────

export const ELEMENT_COLORS: Record<string, number> = {
  fire:      0xff5500,
  lightning: 0xffee00,
  ice:       0x44ccff,
  poison:    0x88ff44,
  arcane:    0xcc44ff,
  none:      0xffffff,
} as const;

// ── Status effect glyph tint colours ─────────────────────────────────────────

export const STATUS_COLORS: Record<string, number> = {
  burning:  0xff5500,
  shocked:  0xffff00,
  slowed:   0x88ccff,
  poisoned: 0x66ff44,
  wet:      0x44aaff,
  stunned:  0xffaaff,
} as const;

// ── HP-based color thresholds ─────────────────────────────────────────────────

/** HP ratio below which the actor glyph starts blending toward LOW_HP_COLOR. */
export const HP_DIM_THRESHOLD = 0.6;
/** Color blended into actor glyph when HP is low. */
export const LOW_HP_COLOR = 0x993333;
/** Max blend factor toward LOW_HP_COLOR at 0 HP. */
export const LOW_HP_MAX_BLEND = 0.55;

// ── Hit flash ────────────────────────────────────────────────────────────────

/** Number of frames to show the red screen-flash when the player is hit. */
export const HIT_FLASH_FRAMES = 4;

// ── Camera physics ────────────────────────────────────────────────────────────

/** Inertia decay per frame (1 = no decay, 0 = instant stop). */
export const CAMERA_FRICTION = 0.92;
/** Velocity magnitude below which camera movement snaps to zero. */
export const CAMERA_VELOCITY_EPSILON = 0.1;
/** Pointer travel (px) before a press is considered a drag rather than a tap. */
export const CAMERA_DRAG_THRESHOLD = 6;

// ── Renderer font ─────────────────────────────────────────────────────────────

export const ASCII_FONT_FAMILY = '"Courier New", "Consolas", "Monaco", monospace';
