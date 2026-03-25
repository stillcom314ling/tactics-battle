/**
 * AI Strategy Tests
 *
 * Tests each strategy function (basic, ranged, brute) and the
 * processEnemyTurns dispatcher using a mock EnemyTurnContext.
 * No renderer or PixiJS needed.
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';
import { executeBasicTurn }  from '../ai/strategies/BasicStrategy';
import { executeRangedTurn } from '../ai/strategies/RangedStrategy';
import { executeBruteTurn }  from '../ai/strategies/BruteStrategy';
import { processEnemyTurns, alertEnemiesNear } from '../ai/AISystem';
import { World, Position, Health, Faction, StatusEffect, AI, Combat } from '../core/ECS';
import type { EnemyTurnContext } from '../ai/AISystem';
import type { GeneratedMap } from '../game/MapGenerator';

// ─── HELPERS ─────────────────────────────────────────────────────────────────

function makeMap(w = 10, h = 10): GeneratedMap {
  const walkable = Array.from({ length: h }, () => new Array(w).fill(true));
  return { width: w, height: h, walkable, gameplay: [], bgFar: [], bgMid: [], foreground: [] } as unknown as GeneratedMap;
}

function spawnEnemy(world: World, col: number, row: number, strategy: AI['strategy'] = 'basic') {
  const id = world.createEntity();
  world.addComponent(id, { type: 'position',  col, row, layer: 'gameplay' } as Position);
  world.addComponent(id, { type: 'health',    current: 20, max: 20 } as Health);
  world.addComponent(id, { type: 'faction',   team: 'enemy' } as Faction);
  world.addComponent(id, { type: 'status',    effects: new Map() } as StatusEffect);
  world.addComponent(id, { type: 'combat',    attackPower: 5, defense: 0, attackRange: 1 } as Combat);
  world.addComponent(id, { type: 'ai',        strategy, alerted: false, alertRange: 5, moveCounter: 0 } as AI);
  return id;
}

function spawnPlayer(world: World, col: number, row: number) {
  const id = world.createEntity();
  world.addComponent(id, { type: 'position', col, row, layer: 'gameplay' } as Position);
  world.addComponent(id, { type: 'health',   current: 100, max: 100 } as Health);
  world.addComponent(id, { type: 'faction',  team: 'player' } as Faction);
  world.addComponent(id, { type: 'status',   effects: new Map() } as StatusEffect);
  world.addComponent(id, { type: 'combat',   attackPower: 10, defense: 0, attackRange: 1 } as Combat);
  return id;
}

function makeContext(world: World, playerId: number, map: GeneratedMap): EnemyTurnContext {
  const playerPos = world.getComponent<Position>(playerId, 'position')!;
  return {
    world,
    map,
    playerId,
    playerPos,
    findPath:            vi.fn().mockReturnValue([]),
    moveEntity:          vi.fn(),
    resolveMelee:        vi.fn(),
    resolveRangedAttack: vi.fn().mockReturnValue(true),
    getActorAt:          vi.fn().mockReturnValue(null),
    alertEnemies:        vi.fn(),
  };
}

// ─── BASIC STRATEGY ───────────────────────────────────────────────────────────

describe('BasicStrategy', () => {
  let world: World;
  let map: GeneratedMap;
  beforeEach(() => { world = new World(); map = makeMap(); });

  it('attacks when adjacent (Chebyshev ≤ 1)', () => {
    const player  = spawnPlayer(world, 5, 5);
    const enemy   = spawnEnemy(world, 5, 6); // adjacent
    const ctx     = makeContext(world, player, map);
    executeBasicTurn(enemy, ctx);
    expect(ctx.resolveMelee).toHaveBeenCalledWith(enemy, player);
  });

  it('moves toward player when not adjacent', () => {
    const player = spawnPlayer(world, 5, 5);
    const enemy  = spawnEnemy(world, 0, 0);
    const ctx    = makeContext(world, player, map);
    // Return a two-step path so the strategy picks path[1]
    (ctx.findPath as any).mockReturnValue([[0, 0], [1, 0]]);
    executeBasicTurn(enemy, ctx);
    expect(ctx.moveEntity).toHaveBeenCalledWith(enemy, 1, 0);
  });

  it('does not move when slowed and not adjacent', () => {
    const player = spawnPlayer(world, 5, 5);
    const enemy  = spawnEnemy(world, 0, 0);
    world.getComponent<StatusEffect>(enemy, 'status')!.effects.set('slowed', 2);
    const ctx = makeContext(world, player, map);
    executeBasicTurn(enemy, ctx);
    expect(ctx.moveEntity).not.toHaveBeenCalled();
  });
});

// ─── BRUTE STRATEGY ───────────────────────────────────────────────────────────

describe('BruteStrategy', () => {
  let world: World;
  let map: GeneratedMap;
  beforeEach(() => { world = new World(); map = makeMap(); });

  it('skips on odd moveCounter ticks', () => {
    const player = spawnPlayer(world, 5, 6);
    const enemy  = spawnEnemy(world, 5, 5, 'brute');
    const ctx    = makeContext(world, player, map);
    // First call → moveCounter becomes 1 (odd) → skip
    executeBruteTurn(enemy, ctx);
    expect(ctx.resolveMelee).not.toHaveBeenCalled();
  });

  it('acts on even moveCounter ticks', () => {
    const player = spawnPlayer(world, 5, 6);
    const enemy  = spawnEnemy(world, 5, 5, 'brute');
    const ctx    = makeContext(world, player, map);
    executeBruteTurn(enemy, ctx); // tick 1 — skip
    executeBruteTurn(enemy, ctx); // tick 2 — act (adjacent → melee)
    expect(ctx.resolveMelee).toHaveBeenCalledWith(enemy, player);
  });
});

// ─── RANGED STRATEGY ─────────────────────────────────────────────────────────

describe('RangedStrategy', () => {
  let world: World;
  let map: GeneratedMap;
  beforeEach(() => { world = new World(); map = makeMap(); });

  it('attempts ranged attack when in range', () => {
    const player = spawnPlayer(world, 3, 0);
    const enemy  = spawnEnemy(world, 0, 0, 'ranged');
    const ctx    = makeContext(world, player, map);
    // Euclidean dist = 3 ≤ ARCHER_SHOOT_RANGE(5)
    executeRangedTurn(enemy, ctx);
    expect(ctx.resolveRangedAttack).toHaveBeenCalled();
  });
});

// ─── PROCESS ENEMY TURNS ─────────────────────────────────────────────────────

describe('processEnemyTurns', () => {
  let world: World;
  let map: GeneratedMap;
  beforeEach(() => { world = new World(); map = makeMap(); });

  it('returns true when player is still alive', () => {
    const player = spawnPlayer(world, 5, 5);
    const ctx    = makeContext(world, player, map);
    const alive  = processEnemyTurns(world, player, ctx);
    expect(alive).toBe(true);
  });

  it('returns false when player has no health component', () => {
    const player = spawnPlayer(world, 5, 5);
    world.removeComponent(player, 'health');
    const ctx   = makeContext(world, player, map);
    const alive = processEnemyTurns(world, player, ctx);
    expect(alive).toBe(false);
  });

  it('skips stunned enemies', () => {
    const player = spawnPlayer(world, 5, 6);
    const enemy  = spawnEnemy(world, 5, 5, 'basic');
    world.getComponent<StatusEffect>(enemy, 'status')!.effects.set('stunned', 1);
    const ctx = makeContext(world, player, map);
    processEnemyTurns(world, player, ctx);
    expect(ctx.resolveMelee).not.toHaveBeenCalled();
  });

  it('ticks status durations for all entities at end of round', () => {
    const player = spawnPlayer(world, 5, 5);
    world.getComponent<StatusEffect>(player, 'status')!.effects.set('wet', 3);
    const ctx = makeContext(world, player, map);
    processEnemyTurns(world, player, ctx);
    expect(world.getComponent<StatusEffect>(player, 'status')!.effects.get('wet')).toBe(2);
  });
});

// ─── ALERT ENEMIES ────────────────────────────────────────────────────────────

describe('alertEnemiesNear', () => {
  it('marks enemies within radius as alerted', () => {
    const world  = new World();
    const enemy  = spawnEnemy(world, 3, 3);
    const anyNew = alertEnemiesNear(world, 0, 0, 6);
    expect(anyNew).toBe(true);
    expect(world.getComponent<AI>(enemy, 'ai')!.alerted).toBe(true);
  });

  it('does not alert enemies outside radius', () => {
    const world  = new World();
    const enemy  = spawnEnemy(world, 9, 9);
    alertEnemiesNear(world, 0, 0, 3);
    expect(world.getComponent<AI>(enemy, 'ai')!.alerted).toBe(false);
  });

  it('returns false when no new enemies are alerted', () => {
    const world = new World();
    const anyNew = alertEnemiesNear(world, 0, 0, 10);
    expect(anyNew).toBe(false);
  });
});
