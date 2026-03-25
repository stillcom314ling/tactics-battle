/**
 * CombatSystem Tests
 *
 * Tests for: resolveMelee, resolveRangedAttack, hasLineOfSight, getActorAt.
 * All tested with mock callbacks — no PixiJS dependency.
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';
import { resolveMelee, resolveRangedAttack, hasLineOfSight, getActorAt } from '../combat/CombatSystem';
import type { CombatCallbacks } from '../combat/CombatSystem';
import { World, Position, Health, Combat, Faction } from '../core/ECS';
import type { GeneratedMap } from '../game/MapGenerator';

// ─── FIXTURES ────────────────────────────────────────────────────────────────

function makeCallbacks(): CombatCallbacks {
  return {
    addMessage:   vi.fn(),
    onPlayerHit:  vi.fn(),
    onPlayerDied: vi.fn(),
    alertEnemies: vi.fn(),
  };
}

function makeMap(width = 10, height = 10, blockedTiles: [number, number][] = []): GeneratedMap {
  const walkable = Array.from({ length: height }, (_, r) =>
    Array.from({ length: width }, (_, c) =>
      !blockedTiles.some(([bc, br]) => bc === c && br === r)
    )
  );
  return { width, height, walkable, gameplay: [], bgFar: [], bgMid: [], foreground: [] } as unknown as GeneratedMap;
}

function spawnCombatant(
  world: World,
  col: number, row: number,
  hp: number,
  attackPower: number,
  defense: number,
  team: 'player' | 'enemy',
) {
  const id = world.createEntity();
  world.addComponent(id, { type: 'position', col, row, layer: 'gameplay' } as Position);
  world.addComponent(id, { type: 'health',  current: hp, max: hp } as Health);
  world.addComponent(id, { type: 'combat',  attackPower, defense, attackRange: 1 } as Combat);
  world.addComponent(id, { type: 'faction', team } as Faction);
  return id;
}

// ─── GET ACTOR AT ─────────────────────────────────────────────────────────────

describe('getActorAt', () => {
  let world: World;
  beforeEach(() => { world = new World(); });

  it('returns entity at given tile', () => {
    const id = spawnCombatant(world, 3, 4, 10, 5, 0, 'enemy');
    expect(getActorAt(world, 3, 4)).toBe(id);
  });

  it('returns null when tile is empty', () => {
    expect(getActorAt(world, 5, 5)).toBeNull();
  });

  it('filters by team', () => {
    const player = spawnCombatant(world, 2, 2, 10, 5, 0, 'player');
    const enemy  = spawnCombatant(world, 3, 3, 10, 5, 0, 'enemy');
    expect(getActorAt(world, 2, 2, 'enemy')).toBeNull();
    expect(getActorAt(world, 3, 3, 'player')).toBeNull();
    expect(getActorAt(world, 2, 2, 'player')).toBe(player);
    expect(getActorAt(world, 3, 3, 'enemy')).toBe(enemy);
  });
});

// ─── HAS LINE OF SIGHT ────────────────────────────────────────────────────────

describe('hasLineOfSight', () => {
  it('returns true for a clear horizontal path', () => {
    const map = makeMap();
    const a: Position = { type: 'position', col: 0, row: 0, layer: 'gameplay' };
    const b: Position = { type: 'position', col: 4, row: 0, layer: 'gameplay' };
    expect(hasLineOfSight(a, b, map)).toBe(true);
  });

  it('returns false when a wall blocks the path', () => {
    // Wall at [2, 0]
    const map = makeMap(10, 10, [[2, 0]]);
    const a: Position = { type: 'position', col: 0, row: 0, layer: 'gameplay' };
    const b: Position = { type: 'position', col: 4, row: 0, layer: 'gameplay' };
    expect(hasLineOfSight(a, b, map)).toBe(false);
  });

  it('returns true for a clear diagonal path', () => {
    const map = makeMap();
    const a: Position = { type: 'position', col: 0, row: 0, layer: 'gameplay' };
    const b: Position = { type: 'position', col: 3, row: 3, layer: 'gameplay' };
    expect(hasLineOfSight(a, b, map)).toBe(true);
  });
});

// ─── RESOLVE MELEE ────────────────────────────────────────────────────────────

describe('resolveMelee', () => {
  let world: World;
  beforeEach(() => { world = new World(); });

  it('reduces defender HP', () => {
    const attacker = spawnCombatant(world, 0, 0, 10, 8, 0, 'enemy');
    const player   = spawnCombatant(world, 1, 0, 30, 0, 0, 'player');
    const cb = makeCallbacks();
    resolveMelee(world, attacker, player, player, cb);
    expect(world.getComponent<Health>(player, 'health')!.current).toBeLessThan(30);
  });

  it('calls onPlayerHit', () => {
    const attacker = spawnCombatant(world, 0, 0, 10, 8, 0, 'enemy');
    const player   = spawnCombatant(world, 1, 0, 30, 0, 0, 'player');
    const cb = makeCallbacks();
    resolveMelee(world, attacker, player, player, cb);
    expect(cb.onPlayerHit).toHaveBeenCalled();
  });

  it('calls onPlayerDied when HP reaches 0', () => {
    const attacker = spawnCombatant(world, 0, 0, 10, 100, 0, 'enemy');
    const player   = spawnCombatant(world, 1, 0, 1, 0, 0, 'player');
    const cb = makeCallbacks();
    resolveMelee(world, attacker, player, player, cb);
    expect(cb.onPlayerDied).toHaveBeenCalled();
  });

  it('minimum 1 damage (attackPower - defense, floored at 1)', () => {
    // attacker power 1, defender defense 999 → still at least 1 dmg
    const attacker = spawnCombatant(world, 0, 0, 10, 1, 0, 'enemy');
    const player   = spawnCombatant(world, 1, 0, 30, 0, 999, 'player');
    const cb = makeCallbacks();
    resolveMelee(world, attacker, player, player, cb);
    expect(world.getComponent<Health>(player, 'health')!.current).toBeLessThan(30);
  });

  it('alerts enemies near the attacker', () => {
    const attacker = spawnCombatant(world, 5, 5, 10, 5, 0, 'enemy');
    const player   = spawnCombatant(world, 6, 5, 30, 0, 0, 'player');
    const cb = makeCallbacks();
    resolveMelee(world, attacker, player, player, cb);
    expect(cb.alertEnemies).toHaveBeenCalledWith(5, 5, expect.any(Number));
  });
});

// ─── RESOLVE RANGED ATTACK ────────────────────────────────────────────────────

describe('resolveRangedAttack', () => {
  let world: World;
  beforeEach(() => { world = new World(); });

  it('hits when LoS is clear', () => {
    const attacker = spawnCombatant(world, 0, 0, 10, 0, 0, 'enemy');
    const player   = spawnCombatant(world, 4, 0, 30, 0, 0, 'player');
    const cb = makeCallbacks();
    const hit = resolveRangedAttack(world, attacker, player, 8, makeMap(), player, cb);
    expect(hit).toBe(true);
    expect(world.getComponent<Health>(player, 'health')!.current).toBeLessThan(30);
  });

  it('misses when LoS is blocked', () => {
    const map = makeMap(10, 10, [[2, 0]]);
    const attacker = spawnCombatant(world, 0, 0, 10, 0, 0, 'enemy');
    const player   = spawnCombatant(world, 4, 0, 30, 0, 0, 'player');
    const cb = makeCallbacks();
    const hit = resolveRangedAttack(world, attacker, player, 8, map, player, cb);
    expect(hit).toBe(false);
    expect(world.getComponent<Health>(player, 'health')!.current).toBe(30);
  });

  it('respects target defense', () => {
    const attacker = spawnCombatant(world, 0, 0, 10, 0, 0, 'enemy');
    const player   = spawnCombatant(world, 4, 0, 30, 0, 5, 'player');
    const cb = makeCallbacks();
    resolveRangedAttack(world, attacker, player, 8, makeMap(), player, cb);
    // Damage = max(1, 8 - 5) = 3
    expect(world.getComponent<Health>(player, 'health')!.current).toBe(30 - 3);
  });
});
