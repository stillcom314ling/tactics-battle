/**
 * RENDER SYSTEM
 *
 * Syncs ECS actor state to the parallax renderer's gameplay layer.
 *
 * drawActors()    — re-renders all actors with HP-tint and status-effect colours.
 * restoreTerrain() — resets a single tile to its map appearance (called after
 *                    an actor moves off it or targeting is cleared).
 *
 * Previously drawActors() was a private method in Game.ts.
 * The stub renderEntities() function (which was unused) has been replaced by
 * this richer implementation that mirrors what Game.ts was actually doing.
 */

import { World, EntityId, Position, Renderable, Health, StatusEffect } from '../core/ECS';
import { ParallaxAsciiRenderer } from '../rendering/ParallaxAsciiRenderer';
import { GeneratedMap } from './MapGenerator';
import {
  HP_DIM_THRESHOLD, LOW_HP_COLOR, LOW_HP_MAX_BLEND, STATUS_COLORS,
} from '../constants/rendering';
import { lerpColor } from '../utils/colors';

// ─── ACTOR DRAWING ────────────────────────────────────────────────────────────

/**
 * Draw all entities that have Position + Renderable + Faction.
 *
 * Glyph colour priority:
 *   1. Status effect (burning → orange, shocked → yellow, etc.)
 *   2. Low-HP blend toward LOW_HP_COLOR
 *   3. Base Renderable.fg colour
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
      // First matching status effect wins
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

    renderer.setCell('gameplay', pos.col, pos.row, {
      char:  rend.char,
      fg,
      alpha: rend.alpha ?? 1.0,
    });
  }
}

// ─── TERRAIN RESTORE ─────────────────────────────────────────────────────────

/**
 * Reset a single gameplay tile back to its static map appearance.
 * Call after an entity moves off (col, row) or targeting is cleared.
 */
export function restoreTerrain(
  renderer: ParallaxAsciiRenderer,
  map: GeneratedMap,
  col: number,
  row: number,
): void {
  renderer.setCell('gameplay', col, row, map.gameplay[row]?.[col] ?? null);
}
