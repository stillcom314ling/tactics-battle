/**
 * RENDER SYSTEM
 *
 * Syncs ECS actor state to the parallax renderer's gameplay layer.
 *
 * Each game tile is rendered as a 3×3 block of sub-cells.  Actors are drawn
 * using ASCII "mini-sprites" (CellPattern from constants/patterns.ts) centred
 * on their game tile.  When an actor moves, restoreTerrain() replaces all 9
 * sub-cells of the vacated tile with the stored terrain pattern.
 *
 * drawActors()    — re-renders all actors with HP-tint and status-effect colours.
 * restoreTerrain() — resets all 9 sub-cells of a game tile to map appearance.
 */

import { World, EntityId, Position, Renderable, Health, StatusEffect } from '../core/ECS';
import { ParallaxAsciiRenderer } from '../rendering/ParallaxAsciiRenderer';
import { GeneratedMap } from './MapGenerator';
import {
  HP_DIM_THRESHOLD, LOW_HP_COLOR, LOW_HP_MAX_BLEND, STATUS_COLORS,
} from '../constants/rendering';
import { lerpColor } from '../utils/colors';
import { makeActorPattern, CellPattern } from '../constants/patterns';

// ─── HELPERS ─────────────────────────────────────────────────────────────────

/**
 * Write a 3×3 CellPattern into the gameplay layer at the given game-tile
 * coordinates.  gameCol/gameRow are in game-tile space; the render layer uses
 * (gameCol*3, gameRow*3) as the top-left sub-cell.
 */
function setGameCellPattern(
  renderer: ParallaxAsciiRenderer,
  gameCol: number,
  gameRow: number,
  pattern: CellPattern,
): void {
  for (let dy = 0; dy < 3; dy++) {
    for (let dx = 0; dx < 3; dx++) {
      renderer.setCell('gameplay', gameCol * 3 + dx, gameRow * 3 + dy, pattern[dy][dx]);
    }
  }
}

/**
 * Convenience export: draw one actor's 3×3 pattern at its current game-tile.
 * Used by Game.ts after moving an entity so it doesn't need to call drawActors()
 * (which iterates all entities) just to update one tile.
 */
export function setActorPatternAt(
  renderer: ParallaxAsciiRenderer,
  col: number,
  row: number,
  char: string,
  fg: number,
  alpha = 1.0,
): void {
  const pat = makeActorPattern(char, fg);
  if (alpha !== 1.0) {
    for (let dy = 0; dy < 3; dy++) {
      for (let dx = 0; dx < 3; dx++) {
        const cell = pat[dy][dx];
        if (cell) pat[dy][dx] = { ...cell, alpha: (cell.alpha ?? 1) * alpha };
      }
    }
  }
  setGameCellPattern(renderer, col, row, pat);
}

// ─── ACTOR DRAWING ────────────────────────────────────────────────────────────

/**
 * Draw all entities that have Position + Renderable + Faction.
 *
 * Glyph colour priority:
 *   1. Status effect (burning → pale orange, shocked → pale yellow, etc.)
 *   2. Low-HP blend toward LOW_HP_COLOR
 *   3. Base Renderable.fg colour
 *
 * The resolved colour is applied to the centre sub-cell (the primary glyph).
 * Surround sub-cells in the pattern are re-coloured proportionally so the
 * whole sprite reacts to status / HP state while remaining subtle.
 */
export function drawActors(
  world: World,
  renderer: ParallaxAsciiRenderer,
): void {
  for (const id of world.query('position', 'renderable', 'faction')) {
    const pos    = world.getComponent<Position>(id, 'position')!;
    const rend   = world.getComponent<Renderable>(id, 'renderable')!;
    const status = world.getComponent<StatusEffect>(id, 'status');
    const hp     = world.getComponent<Health>(id, 'health');

    let fg = rend.fg;

    if (status && status.effects.size > 0) {
      for (const [eff] of status.effects) {
        const tint = STATUS_COLORS[eff];
        if (tint !== undefined) { fg = tint; break; }
      }
    } else if (hp && hp.max > 0) {
      const ratio = Math.max(0, hp.current / hp.max);
      if (ratio < HP_DIM_THRESHOLD) {
        const t = (1 - ratio / HP_DIM_THRESHOLD) * LOW_HP_MAX_BLEND;
        fg = lerpColor(rend.fg, LOW_HP_COLOR, t);
      }
    }

    // Build the 3×3 pattern using the (possibly tinted) fg colour
    const pattern = makeActorPattern(rend.char, fg);

    // Apply actor alpha to every non-null sub-cell
    const alpha = rend.alpha ?? 1.0;
    if (alpha !== 1.0) {
      for (let dy = 0; dy < 3; dy++) {
        for (let dx = 0; dx < 3; dx++) {
          const cell = pattern[dy][dx];
          if (cell) pattern[dy][dx] = { ...cell, alpha: (cell.alpha ?? 1) * alpha };
        }
      }
    }

    setGameCellPattern(renderer, pos.col, pos.row, pattern);
  }
}

// ─── TERRAIN RESTORE ─────────────────────────────────────────────────────────

/**
 * Reset all 9 sub-cells of a game tile back to their static map appearance.
 * Call after an entity moves off (col, row) or targeting is cleared.
 */
export function restoreTerrain(
  renderer: ParallaxAsciiRenderer,
  map: GeneratedMap,
  col: number,
  row: number,
): void {
  for (let dy = 0; dy < 3; dy++) {
    for (let dx = 0; dx < 3; dx++) {
      const renderRow = row * 3 + dy;
      const renderCol = col * 3 + dx;
      renderer.setCell('gameplay', renderCol, renderRow, map.gameplay[renderRow]?.[renderCol] ?? null);
    }
  }
}
