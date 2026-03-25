/**
 * TILE PATTERNS — 3×3 SUB-CELL SPRITES
 *
 * Each game tile is rendered as a 3×3 block of ASCII sub-cells.
 * A CellPattern is a [row][col] array of { char, fg, alpha } descriptors.
 * null means "leave empty" (no character rendered at that sub-cell).
 *
 * Colour convention — watercolour pastel on near-black background:
 *   - Primary glyph (centre or main char): full colour
 *   - Decorative surround chars:           50–70% alpha, same or dimmer hue
 *   - Empty sub-cells:                     null
 *
 * TERRAIN
 *   Walls  — box-drawing / block chars, several styles chosen randomly
 *   Floors — nearly empty; sparse corner marks or a faint centre dot
 *
 * ACTORS
 *   Each actor has a unique ASCII silhouette that evokes their archetype.
 *   The primary char (the ECS renderable.char) sits at position [1][1]
 *   (centre of the 3×3) and the surroundings add flavour.
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

const _  = null; // empty sub-cell

// ─── WALL PATTERNS ───────────────────────────────────────────────────────────
// Several styles; pick one randomly per wall tile for organic variety.

const WALL_STYLE_A = (fg: number): CellPattern => [
  [c('╔', fg, 0.7), c('═', fg, 0.7), c('╗', fg, 0.7)],
  [c('║', fg, 0.7), c('█', fg, 0.9), c('║', fg, 0.7)],
  [c('╚', fg, 0.7), c('═', fg, 0.7), c('╝', fg, 0.7)],
];

const WALL_STYLE_B = (fg: number): CellPattern => [
  [c('▓', fg, 0.8), c('▓', fg, 0.7), c('▓', fg, 0.8)],
  [c('▓', fg, 0.7), c('█', fg, 0.9), c('▓', fg, 0.7)],
  [c('▓', fg, 0.8), c('▓', fg, 0.7), c('▓', fg, 0.8)],
];

const WALL_STYLE_C = (fg: number): CellPattern => [
  [c('#',  fg, 0.6), c('═', fg, 0.7), c('#',  fg, 0.6)],
  [c('║',  fg, 0.7), c('▒', fg, 0.8), c('║',  fg, 0.7)],
  [c('#',  fg, 0.6), c('═', fg, 0.7), c('#',  fg, 0.6)],
];

const WALL_STYLE_D = (fg: number): CellPattern => [
  [c('·',  fg, 0.4), c('#',  fg, 0.7), c('·',  fg, 0.4)],
  [c('#',  fg, 0.7), c('▓', fg, 0.85),c('#',  fg, 0.7)],
  [c('·',  fg, 0.4), c('#',  fg, 0.7), c('·',  fg, 0.4)],
];

export const WALL_STYLES = [WALL_STYLE_A, WALL_STYLE_B, WALL_STYLE_C, WALL_STYLE_D] as const;

export function makeWallPattern(fg: number, styleIndex: number): CellPattern {
  return WALL_STYLES[styleIndex % WALL_STYLES.length](fg);
}

// ─── FLOOR PATTERNS ──────────────────────────────────────────────────────────
// Mostly empty; the dungeon should feel spacious.

const FLOOR_STYLE_A = (fg: number): CellPattern => [
  [_, _, _],
  [_, c('·', fg, 0.35), _],
  [_, _, _],
];

const FLOOR_STYLE_B = (_fg: number): CellPattern => [
  [_, _, _],
  [_, _, _],
  [_, _, _],
];

const FLOOR_STYLE_C = (fg: number): CellPattern => [
  [c('·', fg, 0.25), _, _],
  [_, _, _],
  [_, _, c('·', fg, 0.25)],
];

const FLOOR_STYLE_D = (fg: number): CellPattern => [
  [_, _, c(',', fg, 0.20)],
  [_, _, _],
  [_, _, _],
];

export const FLOOR_STYLES = [
  FLOOR_STYLE_A, FLOOR_STYLE_A, FLOOR_STYLE_A, // weighted toward centre dot
  FLOOR_STYLE_B, FLOOR_STYLE_B,                 // weighted toward empty
  FLOOR_STYLE_C,
  FLOOR_STYLE_D,
] as const;

export function makeFloorPattern(fg: number, styleIndex: number): CellPattern {
  return FLOOR_STYLES[styleIndex % FLOOR_STYLES.length](fg);
}

// ─── ACTOR PATTERNS ──────────────────────────────────────────────────────────
// Primary glyph always at [1][1]. Surrounds suggest the archetype.

/**
 * Player — adventurer silhouette: crossed blades above, boots below.
 *
 *   \ ^ /
 *   ( @ )
 *   / v \
 */
export function makePlayerPattern(fg: number): CellPattern {
  const dim = 0xaaccbb; // slightly dimmer surround
  return [
    [c('\\', dim, 0.55), c('^', dim, 0.55), c('/', dim, 0.55)],
    [c('(',  dim, 0.55), c('@', fg,  1.0 ), c(')', dim, 0.55)],
    [c('/',  dim, 0.45), c('v', dim, 0.45), c('\\', dim, 0.45)],
  ];
}

/**
 * Goblin — hunched, fanged: caret ears, curved mouth.
 *
 *   ^ v ^
 *   ( g )
 *   ~ ~ ~
 */
export function makeGoblinPattern(fg: number): CellPattern {
  const dim = Math.max(0, fg - 0x222222);
  return [
    [c('^', dim, 0.50), c('v', dim, 0.45), c('^', dim, 0.50)],
    [c('(', dim, 0.55), c('g', fg,  1.0 ), c(')', dim, 0.55)],
    [c('~', dim, 0.40), c('~', dim, 0.35), c('~', dim, 0.40)],
  ];
}

/**
 * Archer — ranged stance: arrow nocked to the right.
 *
 *       - >
 *   [   a ]
 *       -
 */
export function makeArcherPattern(fg: number): CellPattern {
  const dim = Math.max(0, fg - 0x222222);
  return [
    [_,             c('-', dim, 0.45), c('>', dim, 0.60)],
    [c('[', dim, 0.50), c('a', fg,  1.0 ), c(']', dim, 0.50)],
    [_,             c('-', dim, 0.40), _             ],
  ];
}

/**
 * Brute — massive, armoured block.
 *
 *   # # #
 *   # B #
 *   # # #
 */
export function makeBrutePattern(fg: number): CellPattern {
  const dim = Math.max(0, fg - 0x111111);
  return [
    [c('#', dim, 0.65), c('#', dim, 0.65), c('#', dim, 0.65)],
    [c('#', dim, 0.65), c('B', fg,  1.0 ), c('#', dim, 0.65)],
    [c('#', dim, 0.65), c('#', dim, 0.65), c('#', dim, 0.65)],
  ];
}

/**
 * Mage — magical aura: star corners, vertical bars.
 *
 *   *   *
 *   | m |
 *   *   *
 */
export function makeMagePattern(fg: number): CellPattern {
  const dim = Math.max(0, fg - 0x111111);
  return [
    [c('*', dim, 0.60), _,              c('*', dim, 0.60)],
    [c('|', dim, 0.50), c('m', fg, 1.0), c('|', dim, 0.50)],
    [c('*', dim, 0.60), _,              c('*', dim, 0.60)],
  ];
}

/**
 * Swarmer — small, skittering: dots and colons.
 *
 *     .
 *   : z :
 *     .
 */
export function makeSwarmerPattern(fg: number): CellPattern {
  const dim = Math.max(0, fg - 0x333333);
  return [
    [_,              c('.', dim, 0.45), _             ],
    [c(':', dim, 0.45), c('z', fg,  1.0 ), c(':', dim, 0.45)],
    [_,              c('.', dim, 0.45), _             ],
  ];
}

// ─── ACTOR PATTERN DISPATCHER ────────────────────────────────────────────────

/**
 * Given an actor's ECS char glyph and base colour, return the appropriate
 * 3×3 pattern. Falls back to a simple single-char centred pattern for
 * unknown chars.
 */
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
