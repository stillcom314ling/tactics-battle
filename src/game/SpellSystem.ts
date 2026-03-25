/**
 * SPELL SYSTEM
 *
 * Resolves spell casts: hit detection, damage, status effects, interactions.
 * Each spell is an Ability data object — adding a new spell is a single factory function.
 *
 * Spell patterns:
 *   single — one tile at target position
 *   line   — Bresenham line from caster to target
 *   aoe    — circle of aoeRadius around target
 *   self   — caster's tile only
 *
 * Spells use per-level CHARGES instead of per-turn cooldowns.
 * This rewards aggressive forward play — no waiting between rooms.
 * Charges refill when populateWorld() is called for a new level.
 */

import {
  World, EntityId, Ability, Abilities, Position, Health, StatusEffect,
  applyStatus, STATUS_DURATIONS,
} from '../core/ECS';
import { processInteractions } from '../core/InteractionSystem';
import { GeneratedMap } from './MapGenerator';

// ─── STARTER SPELL FACTORIES ─────────────────────────────────────────────────
// Balance note: all spells available simultaneously for testing.
// In production these will be unlocked/slotted progressively.

export function makeFlameBolt(): Ability {
  return {
    id: 'flame_bolt', name: 'Flame Bolt', element: 'fire',
    range: 5, pattern: 'single', damage: 10,
    effects: ['burning'],
    charges: 3, chargesMax: 3,
  };
}

export function makeArcLightning(): Ability {
  return {
    id: 'arc_lightning', name: 'Arc Lightning', element: 'lightning',
    range: 5, pattern: 'line', damage: 7,
    effects: ['shocked'],
    charges: 2, chargesMax: 2,
  };
}

export function makeFrostShard(): Ability {
  return {
    id: 'frost_shard', name: 'Frost Shard', element: 'ice',
    range: 4, pattern: 'single', damage: 6,
    effects: ['wet', 'slowed'],
    charges: 3, chargesMax: 3,
  };
}

export function makePoisonCloud(): Ability {
  return {
    id: 'poison_cloud', name: 'Poison Cloud', element: 'poison',
    range: 4, pattern: 'aoe', aoeRadius: 2, damage: 4,
    effects: ['poisoned'],
    charges: 2, chargesMax: 2,
  };
}

/** Returns the default set of player abilities for a new game / floor reset. */
export function makeDefaultPlayerAbilities(): Ability[] {
  return [makeFlameBolt(), makeArcLightning(), makeFrostShard(), makePoisonCloud()];
}

// ─── TILE GEOMETRY ───────────────────────────────────────────────────────────

/** All tiles hit by a spell given caster + target position. */
export function getSpellTiles(
  casterCol: number, casterRow: number,
  targetCol: number, targetRow: number,
  ability: Ability,
  map: GeneratedMap,
): [number, number][] {
  const inBounds = (c: number, r: number) => c >= 0 && c < map.width && r >= 0 && r < map.height;

  switch (ability.pattern) {
    case 'single':
      return [[targetCol, targetRow]];

    case 'self':
      return [[casterCol, casterRow]];

    case 'aoe': {
      const rad = ability.aoeRadius ?? 1;
      const tiles: [number, number][] = [];
      for (let dy = -rad; dy <= rad; dy++) {
        for (let dx = -rad; dx <= rad; dx++) {
          if (dx * dx + dy * dy <= rad * rad) {
            const c = targetCol + dx, r = targetRow + dy;
            if (inBounds(c, r)) tiles.push([c, r]);
          }
        }
      }
      return tiles;
    }

    case 'line': {
      // Bresenham line from caster (exclusive) to target (inclusive)
      const tiles: [number, number][] = [];
      let x = casterCol, y = casterRow;
      const adx = Math.abs(targetCol - x), ady = Math.abs(targetRow - y);
      const sx = x < targetCol ? 1 : -1;
      const sy = y < targetRow ? 1 : -1;
      let err = adx - ady;
      while (true) {
        if ((x !== casterCol || y !== casterRow) && inBounds(x, y)) {
          tiles.push([x, y]);
        }
        if (x === targetCol && y === targetRow) break;
        const e2 = 2 * err;
        if (e2 > -ady) { err -= ady; x += sx; }
        if (e2 < adx)  { err += adx; y += sy; }
      }
      return tiles;
    }
  }
}

/** All tiles in a circle of `range` around (col, row) for targeting highlight. */
export function getRangeTiles(
  col: number, row: number,
  range: number,
  map: GeneratedMap,
): [number, number][] {
  const tiles: [number, number][] = [];
  for (let dy = -range; dy <= range; dy++) {
    for (let dx = -range; dx <= range; dx++) {
      if (dx === 0 && dy === 0) continue;
      if (Math.sqrt(dx * dx + dy * dy) <= range) {
        const c = col + dx, r = row + dy;
        if (c >= 0 && c < map.width && r >= 0 && r < map.height) {
          tiles.push([c, r]);
        }
      }
    }
  }
  return tiles;
}

// ─── RESOLUTION ──────────────────────────────────────────────────────────────

/** Cast a spell: applies damage, status effects, and runs interaction rules. */
export function resolveSpell(
  world: World,
  caster: EntityId,
  ability: Ability,
  targetCol: number,
  targetRow: number,
  map: GeneratedMap,
  addMessage: (msg: string) => void,
): void {
  const casterPos = world.getComponent<Position>(caster, 'position')!;
  const tiles = getSpellTiles(casterPos.col, casterPos.row, targetCol, targetRow, ability, map);

  let hitCount = 0;

  for (const [tc, tr] of tiles) {
    const targets = world.query('position', 'health', 'faction').filter(id => {
      if (id === caster) return false;
      const p = world.getComponent<Position>(id, 'position')!;
      return p.col === tc && p.row === tr;
    });

    for (const targetId of targets) {
      const health = world.getComponent<Health>(targetId, 'health')!;
      health.current -= ability.damage;
      hitCount++;

      const status = world.getComponent<StatusEffect>(targetId, 'status');
      if (status) {
        for (const eff of ability.effects) {
          applyStatus(status, eff, STATUS_DURATIONS[eff] ?? 3);
        }
      }

      // Run synergy rules (wet+lightning, burning+terrain, etc.)
      processInteractions(world, targetId, {
        trigger:    'attack',
        sourceId:   caster,
        data:       { element: ability.element, damage: ability.damage },
        addMessage,
      });

      if (health.current <= 0) {
        addMessage(`${ability.name} slays the enemy!`);
        world.removeEntity(targetId);
      } else {
        const fx = ability.effects.length > 0 ? ` [${ability.effects.join(', ')}]` : '';
        addMessage(`${ability.name}: ${ability.damage} dmg${fx}`);
      }
    }
  }

  if (hitCount === 0) addMessage(`${ability.name} hits nothing.`);

  // Spend a charge
  ability.charges = Math.max(0, ability.charges - 1);
}

/** Refill all spell charges (call when starting a new level). */
export function refillSpellCharges(world: World): void {
  for (const id of world.query('abilities')) {
    const abs = world.getComponent<Abilities>(id, 'abilities')!;
    for (const a of abs.list) {
      a.charges = a.chargesMax;
    }
  }
}
