/**
 * AI SYSTEM
 *
 * Drives all enemy turns each round.
 *
 * STRATEGY PATTERN (functional)
 * ─────────────────────────────
 * Each enemy type maps to a strategy function:
 *   (enemyId, context) => void
 *
 * Adding a new enemy type:
 *   1. Create src/ai/strategies/MyStrategy.ts exporting executeMyTurn()
 *   2. Add an entry to STRATEGY_REGISTRY below
 *   3. Add the corresponding config in MapGenerator's ENEMY_TYPES array
 *
 * EnemyTurnContext contains all dependencies the strategies need.
 * This makes every strategy fully testable with a mock context object.
 */

import { World, EntityId, Position, Faction, StatusEffect, AI, tickStatuses } from '../core/ECS';
import { GeneratedMap } from '../game/MapGenerator';
import { executeBasicTurn }  from './strategies/BasicStrategy';
import { executeRangedTurn } from './strategies/RangedStrategy';
import { executeBruteTurn }  from './strategies/BruteStrategy';
import { ALERT_RADIUS_SPELL } from '../constants/combat';

// ─── CONTEXT ─────────────────────────────────────────────────────────────────

/**
 * Everything a strategy function needs to act.
 * All game-state access goes through this interface — strategies never import
 * from Game.ts or reach into PixiJS.
 */
export interface EnemyTurnContext {
  world:     World;
  map:       GeneratedMap;
  playerId:  EntityId;
  playerPos: Position;

  /** A* path from (fc,fr) to (tc,tr), avoiding solid tiles and other actors. */
  findPath: (fc: number, fr: number, tc: number, tr: number, moverId: EntityId) => [number, number][];

  /** Move an entity to (col, row) — updates ECS + renderer. */
  moveEntity: (id: EntityId, col: number, row: number) => void;

  /** Attempt melee attack from attackerId against defenderId. */
  resolveMelee: (attackerId: EntityId, defenderId: EntityId) => void;

  /**
   * Attempt a ranged attack. Returns true if the shot connected.
   * @param damage - Base damage before target defense reduction.
   */
  resolveRangedAttack: (attackerId: EntityId, targetId: EntityId, damage: number) => boolean;

  /** Return the EntityId of any actor at (col, row), or null. */
  getActorAt: (col: number, row: number) => EntityId | null;

  /** Alert enemies near (col, row) within Chebyshev `radius`. */
  alertEnemies: (col: number, row: number, radius: number) => void;
}

// ─── STRATEGY TYPE & REGISTRY ─────────────────────────────────────────────────

/** A strategy is a pure function: receives the entity + context, produces side-effects. */
export type EnemyStrategy = (enemyId: EntityId, context: EnemyTurnContext) => void;

/**
 * Maps AI strategy names to their implementation.
 * Swarm enemies reuse the basic rush logic but can be differentiated here later.
 */
export const STRATEGY_REGISTRY: Record<string, EnemyStrategy> = {
  basic:  executeBasicTurn,
  ranged: executeRangedTurn,
  brute:  executeBruteTurn,
  swarm:  executeBasicTurn,  // currently same logic as basic
};

// ─── ALERT SYSTEM ────────────────────────────────────────────────────────────

/**
 * Wake all un-alerted enemies within Chebyshev `radius` of (sourceCol, sourceRow).
 * Returns true if any new enemies were alerted (so Game.ts can log a message).
 */
export function alertEnemiesNear(
  world: World,
  sourceCol: number,
  sourceRow: number,
  radius: number,
): boolean {
  let anyNew = false;
  for (const id of world.query('position', 'faction', 'ai')) {
    if (world.getComponent<Faction>(id, 'faction')?.team !== 'enemy') continue;
    const ai  = world.getComponent<AI>(id, 'ai')!;
    if (ai.alerted) continue;
    const pos = world.getComponent<Position>(id, 'position')!;
    const dist = Math.max(Math.abs(pos.col - sourceCol), Math.abs(pos.row - sourceRow));
    if (dist <= radius) { ai.alerted = true; anyNew = true; }
  }
  return anyNew;
}

// ─── MAIN ENEMY PHASE ────────────────────────────────────────────────────────

/**
 * Process every enemy's turn in sequence.
 * Stunned enemies skip their turn.
 * Called once after the player ends their turn.
 *
 * Also ticks status-effect durations at end of round.
 *
 * @returns true if the player is still alive after all enemy turns.
 */
export function processEnemyTurns(
  world: World,
  playerId: EntityId,
  context: EnemyTurnContext,
): boolean {
  if (!world.hasComponent(playerId, 'health')) return false;

  const enemies = world
    .query('position', 'faction', 'health', 'combat')
    .filter(id => world.getComponent<Faction>(id, 'faction')?.team === 'enemy');

  for (const enemyId of enemies) {
    // Re-check player alive after each enemy (a prior kill could cascade)
    if (!world.hasComponent(playerId, 'health')) break;

    const status = world.getComponent<StatusEffect>(enemyId, 'status');
    if (status?.effects.has('stunned')) continue; // stunned: skip turn

    const ai       = world.getComponent<AI>(enemyId, 'ai');
    const strategy = STRATEGY_REGISTRY[ai?.strategy ?? 'basic'] ?? executeBasicTurn;
    strategy(enemyId, context);
  }

  // Tick all status durations at the end of the round
  for (const id of world.query('status')) {
    tickStatuses(world.getComponent<StatusEffect>(id, 'status')!);
  }

  return world.hasComponent(playerId, 'health');
}
