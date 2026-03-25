/**
 * TILE PATTERNS — 3×3 SUB-CELL SPRITES
 *
 * Each game tile is rendered as a 3×3 block of ASCII sub-cells (8px each,
 * total 24px per tile).
 *
 * COLOUR CONVENTION — dark walls, neon actors
 * ────────────────────────────────────────────
 * Terrain  → very dark, near-black; recedes into background
 * Actors   → vivid neon primary glyph; dim neon detail chars around it
 *
 * TERRAIN
 *   Walls  — four styles using box-drawing / block chars; all very dark
 *   Floors — nearly empty; sparse faint texture marks
 *
 * ACTORS
 *   Centre [1][1] : primary glyph at fontSize=16 (large, vivid neon)
 *   Surrounds     : small 8px detail chars at 20–35% alpha of the neon hue
 */

import { AsciiCell } from '../rendering/ParallaxAsciiRenderer';

// ─── Types ───────────────────────────────────────────────────────────────────

export type SubCell = AsciiCell | null;
/** A 3-row × 3-col pattern of sub-cells. [row][col]. */
export type CellPattern = [
  [SubCell, SubCell, SubCell],
  [SubCell, SubCell, SubCell],
  [SubCell, SubCell, SubCell],
];

// ─── Helpers ─────────────────────────────────────────────────────────────────

function c(char: string, fg: number, alpha = 1.0): SubCell {
  return { char, fg, alpha };
}

/** Dim a neon colour to ~35% brightness for surround detail chars. */
function dim(fg: number, factor = 0.30): number {
  const r = Math.round(((fg >> 16) & 0xff) * factor);
  const g = Math.round(((fg >>  8) & 0xff) * factor);
  const b = Math.round(( fg        & 0xff) * factor);
  return (r << 16) | (g << 8) | b;
}

const _ = null;

// ─── WALL PATTERNS ───────────────────────────────────────────────────────────
// Dark structural chars; pick one style randomly per wall tile.

const WALL_STYLE_A = (fg: number): CellPattern => [
  [c('╔', fg, 0.55), c('═', fg, 0.50), c('╗', fg, 0.55)],
  [c('║', fg, 0.50), c('█', fg, 0.70), c('║', fg, 0.50)],
  [c('╚', fg, 0.55), c('═', fg, 0.50), c('╝', fg, 0.55)],
];

const WALL_STYLE_B = (fg: number): CellPattern => [
  [c('▓', fg, 0.60), c('▓', fg, 0.55), c('▓', fg, 0.60)],
  [c('▓', fg, 0.55), c('█', fg, 0.75), c('▓', fg, 0.55)],
  [c('▓', fg, 0.60), c('▓', fg, 0.55), c('▓', fg, 0.60)],
];

const WALL_STYLE_C = (fg: number): CellPattern => [
  [c('#',  fg, 0.45), c('─', fg, 0.55), c('#',  fg, 0.45)],
  [c('│',  fg, 0.55), c('▒', fg, 0.65), c('│',  fg, 0.55)],
  [c('#',  fg, 0.45), c('─', fg, 0.55), c('#',  fg, 0.45)],
];

const WALL_STYLE_D = (fg: number): CellPattern => [
  [c('·', fg, 0.35), c('#', fg, 0.55), c('·', fg, 0.35)],
  [c('#', fg, 0.55), c('▓', fg, 0.70), c('#', fg, 0.55)],
  [c('·', fg, 0.35), c('#', fg, 0.55), c('·', fg, 0.35)],
];

export const WALL_STYLES = [WALL_STYLE_A, WALL_STYLE_B, WALL_STYLE_C, WALL_STYLE_D] as const;

export function makeWallPattern(fg: number, styleIndex: number): CellPattern {
  return WALL_STYLES[styleIndex % WALL_STYLES.length](fg);
}

// ─── FLOOR PATTERNS ──────────────────────────────────────────────────────────
// Nearly empty; very faint texture only.

const FLOOR_STYLE_A = (fg: number): CellPattern => [
  [_, _, _],
  [_, c('·', fg, 0.25), _],
  [_, _, _],
];

const FLOOR_STYLE_B = (_fg: number): CellPattern => [
  [_, _, _],
  [_, _, _],
  [_, _, _],
];

const FLOOR_STYLE_C = (fg: number): CellPattern => [
  [c('·', fg, 0.18), _, _],
  [_, _, _],
  [_, _, c('·', fg, 0.18)],
];

const FLOOR_STYLE_D = (fg: number): CellPattern => [
  [_, _, c(',', fg, 0.15)],
  [_, _, _],
  [_, _, _],
];

export const FLOOR_STYLES = [
  FLOOR_STYLE_A, FLOOR_STYLE_A, FLOOR_STYLE_A,
  FLOOR_STYLE_B, FLOOR_STYLE_B,
  FLOOR_STYLE_C,
  FLOOR_STYLE_D,
] as const;

export function makeFloorPattern(fg: number, styleIndex: number): CellPattern {
  return FLOOR_STYLES[styleIndex % FLOOR_STYLES.length](fg);
}

// ─── ACTOR PATTERNS ──────────────────────────────────────────────────────────
//
// Centre [1][1]: primary glyph at full 8px sub-cell size.
// Surrounding 8 sub-cells: dim neon detail chars that frame the centre char.
//
//   @  player    — bracket frame
//   g  goblin    — caret ears, tilde skirt
//   a  archer    — arrow motif pointing right
//   B  brute     — heavy hash/box border
//   m  mage      — star-corner aura
//   z  swarmer   — minimal dot frame

/** Player — bracket frame: ─│@│─ */
export function makePlayerPattern(fg: number): CellPattern {
  const d = dim(fg, 0.40);
  return [
    [c('─', d, 0.50), c('─', d, 0.35), c('─', d, 0.50)],
    [c('│', d, 0.50), c('@', fg, 1.0),  c('│', d, 0.50)],
    [c('─', d, 0.40), c('─', d, 0.30), c('─', d, 0.40)],
  ];
}

/** Goblin — ^─^ / >g< / ~~~ */
export function makeGoblinPattern(fg: number): CellPattern {
  const d = dim(fg, 0.40);
  return [
    [c('^', d, 0.55), c('─', d, 0.30), c('^', d, 0.55)],
    [c('>', d, 0.45), c('g', fg, 1.0),  c('<', d, 0.45)],
    [c('~', d, 0.45), c('~', d, 0.35), c('~', d, 0.45)],
  ];
}

/** Archer — ──► / ─a► / ─── */
export function makeArcherPattern(fg: number): CellPattern {
  const d = dim(fg, 0.40);
  return [
    [_,               c('─', d, 0.35), c('─', d, 0.45)],
    [c('─', d, 0.30), c('a', fg, 1.0),  c('►', d, 0.65)],
    [_,               c('─', d, 0.30), _              ],
  ];
}

/** Brute — #═# / ║B║ / #═# */
export function makeBrutePattern(fg: number): CellPattern {
  const d = dim(fg, 0.45);
  return [
    [c('#', d, 0.65), c('═', d, 0.55), c('#', d, 0.65)],
    [c('║', d, 0.55), c('B', fg, 1.0),  c('║', d, 0.55)],
    [c('#', d, 0.65), c('═', d, 0.55), c('#', d, 0.65)],
  ];
}

/** Mage — *·* / ·m· / *·* */
export function makeMagePattern(fg: number): CellPattern {
  const d = dim(fg, 0.45);
  return [
    [c('*', d, 0.70), c('·', d, 0.30), c('*', d, 0.70)],
    [c('·', d, 0.30), c('m', fg, 1.0),  c('·', d, 0.30)],
    [c('*', d, 0.70), c('·', d, 0.30), c('*', d, 0.70)],
  ];
}

/** Swarmer — ··· / ·z· / ··· */
export function makeSwarmerPattern(fg: number): CellPattern {
  const d = dim(fg, 0.35);
  return [
    [c('·', d, 0.40), c('·', d, 0.30), c('·', d, 0.40)],
    [c('·', d, 0.35), c('z', fg, 1.0),  c('·', d, 0.35)],
    [c('·', d, 0.40), c('·', d, 0.30), c('·', d, 0.40)],
  ];
}

// ─── ACTOR PATTERN DISPATCHER ────────────────────────────────────────────────

export function makeActorPattern(char: string, fg: number): CellPattern {
  switch (char) {
    case '@': return makePlayerPattern(fg);
    case 'g': return makeGoblinPattern(fg);
    case 'a': return makeArcherPattern(fg);
    case 'B': return makeBrutePattern(fg);
    case 'm': return makeMagePattern(fg);
    case 'z': return makeSwarmerPattern(fg);
    default:
      return [
        [_, _, _],
        [_, c(char, fg, 1.0), _],
        [_, _, _],
      ];
  }
}
