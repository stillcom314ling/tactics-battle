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

/**
 * Logical size of one game tile in pixels.
 * Used for camera math, screenToTile conversions, and UI positioning.
 */
export const CELL_SIZE = 24;

/**
 * Physical render cell size for the gameplay layer.
 * Each game tile is drawn as a 3×3 block of sub-cells, so the render cell
 * is 1/3 of CELL_SIZE (3 × 8 = 24 pixels per game tile on screen).
 */
export const RENDER_CELL_SIZE = 8;

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

// ── Tile color palettes — dark walls, neon actors ────────────────────────────
// Walls and floors recede into darkness; actors and effects pop with vivid neon.

/** Wall sub-cell colours: very dark blue-grey, structural and dim. */
export const WALL_COLORS   = [0x1e2a38, 0x18222e, 0x1a2433, 0x1c2636] as const;
/** Floor sub-cell colours: near-black, barely visible texture. */
export const FLOOR_COLORS  = [0x0d1520, 0x0f1825, 0x111b29] as const;
export const BG_FAR_COLORS = [0x1a1a2e, 0x16213e, 0x0f1626] as const;
export const BG_MID_COLORS = [0x222244, 0x1a1a3a, 0x2a2a44] as const;
export const FG_COLORS     = [0x2a3a4a, 0x2e3e50, 0x253545] as const;

// ── Element colours — vivid neon (targeting highlights and action menu) ───────

export const ELEMENT_COLORS: Record<string, number> = {
  fire:      0xff5500,
  lightning: 0xffee00,
  ice:       0x44ccff,
  poison:    0x88ff44,
  arcane:    0xcc44ff,
  none:      0xaabbcc,
} as const;

// ── Status effect glyph tint colours — neon ──────────────────────────────────

export const STATUS_COLORS: Record<string, number> = {
  burning:  0xff5500,
  shocked:  0xffff00,
  slowed:   0x44aaff,
  poisoned: 0x66ff44,
  wet:      0x00ccff,
  stunned:  0xff44ff,
} as const;

// ── HP-based color thresholds ─────────────────────────────────────────────────

/** HP ratio below which the actor glyph starts blending toward LOW_HP_COLOR. */
export const HP_DIM_THRESHOLD = 0.6;
/** Color blended into actor glyph when HP is low — vivid red. */
export const LOW_HP_COLOR = 0xff2244;
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
