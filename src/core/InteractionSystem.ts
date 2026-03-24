/**
 * INTERACTION SYSTEM
 *
 * This is where emergent gameplay lives. Define rules like:
 *   - "burning" + "standing on oil" = tile catches fire
 *   - "wet" + "lightning attack" = double damage + stun
 *   - "burning" entity = takes 3 damage per turn
 *   - "poisoned" entity = takes 2 damage per turn
 *
 * Rules are simple predicate -> effect pairs.
 * Add more rules to get more emergent interactions.
 */

import { EntityId, World, StatusEffect, Terrain, Position, Health, applyStatus } from './ECS';

export interface InteractionRule {
  name: string;
  description: string;
  predicate: (world: World, entityId: EntityId, context: InteractionContext) => boolean;
  effect: (world: World, entityId: EntityId, context: InteractionContext) => void;
}

export interface InteractionContext {
  /** What triggered this check: 'turn_start', 'attack', 'move', 'ability' */
  trigger: string;
  /** Source entity (e.g., attacker) */
  sourceId?: EntityId;
  /** Additional data */
  data?: Record<string, unknown>;
  /** Log a combat message */
  addMessage?: (msg: string) => void;
}

/**
 * Registry of all interaction rules.
 */
export const INTERACTION_RULES: InteractionRule[] = [
  {
    name: 'burning_entity_damage',
    description: 'Burning entities take 3 damage per turn',
    predicate: (world, entityId, ctx) => {
      if (ctx.trigger !== 'turn_start') return false;
      const status = world.getComponent<StatusEffect>(entityId, 'status');
      return status?.effects.has('burning') ?? false;
    },
    effect: (world, entityId, ctx) => {
      const health = world.getComponent<Health>(entityId, 'health');
      if (health) {
        health.current -= 3;
        ctx.addMessage?.(`🔥 Burning deals 3 damage!`);
      }
    },
  },

  {
    name: 'poisoned_entity_damage',
    description: 'Poisoned entities take 2 damage per turn',
    predicate: (world, entityId, ctx) => {
      if (ctx.trigger !== 'turn_start') return false;
      const status = world.getComponent<StatusEffect>(entityId, 'status');
      return status?.effects.has('poisoned') ?? false;
    },
    effect: (world, entityId, ctx) => {
      const health = world.getComponent<Health>(entityId, 'health');
      if (health) {
        health.current -= 2;
        ctx.addMessage?.(`☠ Poison deals 2 damage!`);
      }
    },
  },

  {
    name: 'fire_spreads_to_oil',
    description: 'Burning entity standing on flammable terrain ignites it',
    predicate: (world, entityId) => {
      const status = world.getComponent<StatusEffect>(entityId, 'status');
      const pos = world.getComponent<Position>(entityId, 'position');
      if (!status?.effects.has('burning') || !pos) return false;

      const terrainEntities = world.query('terrain', 'position');
      for (const tid of terrainEntities) {
        const tpos = world.getComponent<Position>(tid, 'position')!;
        const terrain = world.getComponent<Terrain>(tid, 'terrain')!;
        if (tpos.col === pos.col && tpos.row === pos.row && terrain.properties.has('flammable')) {
          return true;
        }
      }
      return false;
    },
    effect: (world, entityId) => {
      const pos = world.getComponent<Position>(entityId, 'position')!;
      const terrainEntities = world.query('terrain', 'position');
      for (const tid of terrainEntities) {
        const tpos = world.getComponent<Position>(tid, 'position')!;
        const terrain = world.getComponent<Terrain>(tid, 'terrain')!;
        if (tpos.col === pos.col && tpos.row === pos.row && terrain.properties.has('flammable')) {
          terrain.properties.delete('flammable');
          terrain.properties.add('on_fire');
        }
      }
    },
  },

  {
    name: 'wet_plus_lightning',
    description: 'Wet entities take double damage from lightning and get stunned',
    predicate: (world, entityId, ctx) => {
      if (ctx.trigger !== 'attack' || ctx.data?.['element'] !== 'lightning') return false;
      const status = world.getComponent<StatusEffect>(entityId, 'status');
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
    description: 'Standing on burning terrain deals 5 damage and applies burning',
    predicate: (world, entityId, ctx) => {
      if (ctx.trigger !== 'turn_start') return false;
      const pos = world.getComponent<Position>(entityId, 'position');
      if (!pos) return false;

      const terrainEntities = world.query('terrain', 'position');
      for (const tid of terrainEntities) {
        const tpos = world.getComponent<Position>(tid, 'position')!;
        const terrain = world.getComponent<Terrain>(tid, 'terrain')!;
        if (tpos.col === pos.col && tpos.row === pos.row && terrain.properties.has('on_fire')) {
          return true;
        }
      }
      return false;
    },
    effect: (world, entityId, ctx) => {
      const health = world.getComponent<Health>(entityId, 'health');
      if (health) {
        health.current -= 5;
        ctx.addMessage?.(`🔥 Burning floor deals 5 damage!`);
      }
      const status = world.getComponent<StatusEffect>(entityId, 'status');
      if (status) applyStatus(status, 'burning', 3);
    },
  },
];

/**
 * Run all interaction rules against an entity.
 */
export function processInteractions(
  world: World,
  entityId: EntityId,
  context: InteractionContext
) {
  for (const rule of INTERACTION_RULES) {
    if (rule.predicate(world, entityId, context)) {
      rule.effect(world, entityId, context);
    }
  }
}
