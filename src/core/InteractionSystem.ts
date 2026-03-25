/**
 * INTERACTION SYSTEM
 *
 * Where emergent gameplay lives. Rules are simple predicate → effect pairs.
 * Add a new entry to INTERACTION_RULES to get a new emergent interaction.
 *
 * Current rules:
 *   - 'burning'  on turn_start      → 3 damage/turn
 *   - 'poisoned' on turn_start      → 2 damage/turn
 *   - burning entity on flammable tile → tile becomes on_fire
 *   - 'wet' + lightning attack      → double damage + stun
 *   - standing on on_fire tile      → 5 damage + apply burning
 *
 * Damage values live in constants/combat.ts for easy tuning.
 */

import { EntityId, World, StatusEffect, Terrain, Position, Health, applyStatus } from './ECS';
import {
  BURNING_DAMAGE_PER_TURN,
  POISON_DAMAGE_PER_TURN,
  FIRE_TERRAIN_DAMAGE,
  FIRE_TERRAIN_BURN_TURNS,
} from '../constants/combat';

export interface InteractionRule {
  name: string;
  description: string;
  predicate: (world: World, entityId: EntityId, context: InteractionContext) => boolean;
  effect:    (world: World, entityId: EntityId, context: InteractionContext) => void;
}

export interface InteractionContext {
  /** What triggered this check: 'turn_start' | 'attack' | 'move' | 'ability' */
  trigger: string;
  /** Source entity (e.g. attacker). */
  sourceId?: EntityId;
  /** Arbitrary extra data (e.g. { element, damage }). */
  data?: Record<string, unknown>;
  /** Log a combat message to the HUD. */
  addMessage?: (msg: string) => void;
}

// ─── HELPERS ─────────────────────────────────────────────────────────────────

/** Return the terrain entity occupying (col, row), or undefined. */
function terrainAt(world: World, col: number, row: number): EntityId | undefined {
  return world.query('terrain', 'position').find(tid => {
    const p = world.getComponent<Position>(tid, 'position')!;
    return p.col === col && p.row === row;
  });
}

// ─── RULE REGISTRY ────────────────────────────────────────────────────────────

export const INTERACTION_RULES: InteractionRule[] = [
  {
    name: 'burning_entity_damage',
    description: `Burning entities take ${BURNING_DAMAGE_PER_TURN} damage per turn`,
    predicate: (_world, entityId, ctx) => {
      if (ctx.trigger !== 'turn_start') return false;
      const status = _world.getComponent<StatusEffect>(entityId, 'status');
      return status?.effects.has('burning') ?? false;
    },
    effect: (world, entityId, ctx) => {
      const health = world.getComponent<Health>(entityId, 'health');
      if (health) {
        health.current -= BURNING_DAMAGE_PER_TURN;
        ctx.addMessage?.(`🔥 Burning deals ${BURNING_DAMAGE_PER_TURN} damage!`);
      }
    },
  },

  {
    name: 'poisoned_entity_damage',
    description: `Poisoned entities take ${POISON_DAMAGE_PER_TURN} damage per turn`,
    predicate: (_world, entityId, ctx) => {
      if (ctx.trigger !== 'turn_start') return false;
      const status = _world.getComponent<StatusEffect>(entityId, 'status');
      return status?.effects.has('poisoned') ?? false;
    },
    effect: (world, entityId, ctx) => {
      const health = world.getComponent<Health>(entityId, 'health');
      if (health) {
        health.current -= POISON_DAMAGE_PER_TURN;
        ctx.addMessage?.(`☠ Poison deals ${POISON_DAMAGE_PER_TURN} damage!`);
      }
    },
  },

  {
    name: 'fire_spreads_to_flammable',
    description: 'Burning entity standing on flammable terrain ignites the tile',
    predicate: (world, entityId) => {
      const status = world.getComponent<StatusEffect>(entityId, 'status');
      const pos    = world.getComponent<Position>(entityId, 'position');
      if (!status?.effects.has('burning') || !pos) return false;
      const tid = terrainAt(world, pos.col, pos.row);
      if (!tid) return false;
      return world.getComponent<Terrain>(tid, 'terrain')!.properties.has('flammable');
    },
    effect: (world, entityId) => {
      const pos = world.getComponent<Position>(entityId, 'position')!;
      const tid = terrainAt(world, pos.col, pos.row);
      if (!tid) return;
      const terrain = world.getComponent<Terrain>(tid, 'terrain')!;
      terrain.properties.delete('flammable');
      terrain.properties.add('on_fire');
    },
  },

  {
    name: 'wet_plus_lightning',
    description: 'Wet entities take double damage from lightning and get stunned',
    predicate: (_world, entityId, ctx) => {
      if (ctx.trigger !== 'attack' || ctx.data?.['element'] !== 'lightning') return false;
      const status = _world.getComponent<StatusEffect>(entityId, 'status');
      return status?.effects.has('wet') ?? false;
    },
    effect: (world, entityId, ctx) => {
      const health = world.getComponent<Health>(entityId, 'health');
      const status = world.getComponent<StatusEffect>(entityId, 'status');
      if (health && ctx.data?.['damage']) {
        health.current -= ctx.data['damage'] as number;
        ctx.addMessage?.(`⚡ Wet+Lightning: bonus damage!`);
      }
      if (status) {
        applyStatus(status, 'stunned', 1);
        status.effects.delete('wet');
        ctx.addMessage?.(`⚡ Electrocuted — stunned!`);
      }
    },
  },

  {
    name: 'fire_terrain_damage',
    description: `Standing on burning terrain deals ${FIRE_TERRAIN_DAMAGE} damage and applies burning`,
    predicate: (world, entityId, ctx) => {
      if (ctx.trigger !== 'turn_start') return false;
      const pos = world.getComponent<Position>(entityId, 'position');
      if (!pos) return false;
      const tid = terrainAt(world, pos.col, pos.row);
      if (!tid) return false;
      return world.getComponent<Terrain>(tid, 'terrain')!.properties.has('on_fire');
    },
    effect: (world, entityId, ctx) => {
      const health = world.getComponent<Health>(entityId, 'health');
      if (health) {
        health.current -= FIRE_TERRAIN_DAMAGE;
        ctx.addMessage?.(`🔥 Burning floor deals ${FIRE_TERRAIN_DAMAGE} damage!`);
      }
      const status = world.getComponent<StatusEffect>(entityId, 'status');
      if (status) applyStatus(status, 'burning', FIRE_TERRAIN_BURN_TURNS);
    },
  },
];

/**
 * Run all interaction rules against an entity in the given context.
 * Call once per entity per trigger event (turn_start, attack, etc.).
 */
export function processInteractions(
  world: World,
  entityId: EntityId,
  context: InteractionContext,
): void {
  for (const rule of INTERACTION_RULES) {
    if (rule.predicate(world, entityId, context)) {
      rule.effect(world, entityId, context);
    }
  }
}
