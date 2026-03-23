/**
 * INTERACTION SYSTEM
 * 
 * This is where emergent gameplay lives. Define rules like:
 *   - "burning" + "standing on oil" = tile catches fire
 *   - "wet" + "lightning attack" = double damage + stun
 *   - "frozen" + "hit" = shatter (bonus damage)
 * 
 * Rules are simple predicate -> effect pairs.
 * Add more rules to get more emergent interactions.
 * 
 * Claude Code: this is the file to extend when adding new mechanics.
 */

import { EntityId, World, StatusEffect, Terrain, Position, Health } from './ECS';

export interface InteractionRule {
  name: string;
  description: string;
  /** Check if this rule applies */
  predicate: (world: World, entityId: EntityId, context: InteractionContext) => boolean;
  /** Apply the effect */
  effect: (world: World, entityId: EntityId, context: InteractionContext) => void;
}

export interface InteractionContext {
  /** What triggered this check: 'turn_start', 'attack', 'move', 'ability' */
  trigger: string;
  /** Source entity (e.g., attacker) */
  sourceId?: EntityId;
  /** Additional data */
  data?: Record<string, unknown>;
}

/**
 * Registry of all interaction rules. Add new rules here for emergent mechanics.
 */
export const INTERACTION_RULES: InteractionRule[] = [
  {
    name: 'fire_spreads_to_oil',
    description: 'Burning entity standing on flammable terrain ignites it',
    predicate: (world, entityId) => {
      const status = world.getComponent<StatusEffect>(entityId, 'status');
      const pos = world.getComponent<Position>(entityId, 'position');
      if (!status?.effects.has('burning') || !pos) return false;

      // Check terrain at this position
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
          console.log(`[interaction] fire spread to terrain at ${pos.col},${pos.row}`);
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
        // Double the damage
        health.current -= ctx.data['damage'] as number;
        console.log(`[interaction] wet+lightning: bonus damage to entity ${entityId}`);
      }
      if (status) {
        status.effects.add('stunned');
        status.effects.delete('wet'); // water evaporates
        console.log(`[interaction] wet+lightning: entity ${entityId} stunned`);
      }
    },
  },

  {
    name: 'fire_terrain_damage',
    description: 'Standing on burning terrain deals damage each turn',
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
    effect: (world, entityId) => {
      const health = world.getComponent<Health>(entityId, 'health');
      if (health) {
        health.current -= 5;
        console.log(`[interaction] fire terrain deals 5 damage to entity ${entityId}`);
      }
      const status = world.getComponent<StatusEffect>(entityId, 'status');
      if (status) {
        status.effects.add('burning');
      }
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
