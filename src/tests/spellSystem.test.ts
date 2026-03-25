/**
 * SpellSystem Tests
 *
 * Tests for: spell tile geometry (getSpellTiles, getRangeTiles)
 * and spell resolution (resolveSpell).
 *
 * No PixiJS or rot-js required — all geometry is pure math.
 */

import { describe, it, expect, vi } from 'vitest';
import { getSpellTiles, getRangeTiles, resolveSpell } from '../game/SpellSystem';
import { makeFlameBolt, makeArcLightning, makePoisonCloud, makeFrostShard } from '../game/SpellSystem';
import { World, Position, Health, Faction, StatusEffect, Ability } from '../core/ECS';
import type { GeneratedMap } from '../game/MapGenerator';

// ─── MINIMAL MAP FIXTURE ──────────────────────────────────────────────────────

function makeMap(width = 10, height = 10): GeneratedMap {
  const walkable = Array.from({ length: height }, () => new Array(width).fill(true));
  return {
    width, height, walkable,
    gameplay: [],  bgFar: [],  bgMid: [],  foreground: [],
  } as unknown as GeneratedMap;
}

// ─── SPELL FACTORIES ─────────────────────────────────────────────────────────

describe('spell factories', () => {
  it('makeFlameBolt produces expected values', () => {
    const s = makeFlameBolt();
    expect(s.id).toBe('flame_bolt');
    expect(s.element).toBe('fire');
    expect(s.pattern).toBe('single');
    expect(s.charges).toBe(s.chargesMax);
  });

  it('makePoisonCloud has aoeRadius', () => {
    const s = makePoisonCloud();
    expect(s.pattern).toBe('aoe');
    expect(s.aoeRadius).toBeGreaterThan(0);
  });

  it('makeArcLightning is a line spell', () => {
    expect(makeArcLightning().pattern).toBe('line');
  });
});

// ─── SINGLE TARGET GEOMETRY ──────────────────────────────────────────────────

describe('getSpellTiles — single', () => {
  const map = makeMap();

  it('returns only the target tile', () => {
    const spell: Ability = { ...makeFlameBolt() };
    const tiles = getSpellTiles(0, 0, 3, 3, spell, map);
    expect(tiles).toHaveLength(1);
    expect(tiles[0]).toEqual([3, 3]);
  });
});

// ─── SELF GEOMETRY ────────────────────────────────────────────────────────────

describe('getSpellTiles — self', () => {
  const map = makeMap();

  it('returns only the caster tile', () => {
    const spell: Ability = { ...makeFlameBolt(), pattern: 'self' };
    const tiles = getSpellTiles(2, 2, 5, 5, spell, map);
    expect(tiles).toHaveLength(1);
    expect(tiles[0]).toEqual([2, 2]);
  });
});

// ─── AOE GEOMETRY ────────────────────────────────────────────────────────────

describe('getSpellTiles — aoe', () => {
  const map = makeMap();

  it('includes center and cells within radius', () => {
    const spell: Ability = { ...makePoisonCloud(), aoeRadius: 1 };
    const tiles = getSpellTiles(0, 0, 5, 5, spell, map);
    expect(tiles).toContainEqual([5, 5]);
    expect(tiles).toContainEqual([5, 4]);
    expect(tiles).toContainEqual([4, 5]);
    // Corner (diagonal distance > 1) should NOT be included
    expect(tiles).not.toContainEqual([4, 4]);
  });

  it('respects map bounds', () => {
    const map2 = makeMap(3, 3);
    const spell: Ability = { ...makePoisonCloud(), aoeRadius: 2 };
    const tiles = getSpellTiles(0, 0, 0, 0, spell, map2);
    for (const [c, r] of tiles) {
      expect(c).toBeGreaterThanOrEqual(0);
      expect(c).toBeLessThan(3);
      expect(r).toBeGreaterThanOrEqual(0);
      expect(r).toBeLessThan(3);
    }
  });
});

// ─── LINE GEOMETRY ────────────────────────────────────────────────────────────

describe('getSpellTiles — line', () => {
  const map = makeMap();

  it('does not include the caster tile', () => {
    const spell: Ability = { ...makeArcLightning() };
    const tiles = getSpellTiles(0, 0, 4, 0, spell, map);
    expect(tiles).not.toContainEqual([0, 0]);
  });

  it('includes the target tile', () => {
    const spell: Ability = { ...makeArcLightning() };
    const tiles = getSpellTiles(0, 0, 4, 0, spell, map);
    expect(tiles).toContainEqual([4, 0]);
  });

  it('horizontal line has correct length', () => {
    const spell: Ability = { ...makeArcLightning() };
    const tiles = getSpellTiles(0, 0, 3, 0, spell, map);
    // 3 tiles: [1,0], [2,0], [3,0]
    expect(tiles).toHaveLength(3);
  });
});

// ─── RANGE TILES ─────────────────────────────────────────────────────────────

describe('getRangeTiles', () => {
  const map = makeMap();

  it('does not include origin', () => {
    const tiles = getRangeTiles(5, 5, 3, map);
    expect(tiles).not.toContainEqual([5, 5]);
  });

  it('all returned tiles are within Euclidean range', () => {
    const range = 3;
    const tiles = getRangeTiles(5, 5, range, map);
    for (const [c, r] of tiles) {
      const dist = Math.sqrt((c - 5) ** 2 + (r - 5) ** 2);
      expect(dist).toBeLessThanOrEqual(range + 0.001);
    }
  });

  it('respects map bounds', () => {
    const map2 = makeMap(4, 4);
    const tiles = getRangeTiles(0, 0, 5, map2);
    for (const [c, r] of tiles) {
      expect(c).toBeGreaterThanOrEqual(0);
      expect(r).toBeGreaterThanOrEqual(0);
    }
  });
});

// ─── RESOLVE SPELL ────────────────────────────────────────────────────────────

describe('resolveSpell', () => {
  function makeWorld() {
    const world = new World();
    const casterId = world.createEntity();
    world.addComponent(casterId, { type: 'position', col: 0, row: 0, layer: 'gameplay' } as Position);
    world.addComponent(casterId, { type: 'faction', team: 'player' } as Faction);

    const targetId = world.createEntity();
    world.addComponent(targetId, { type: 'position', col: 3, row: 0, layer: 'gameplay' } as Position);
    world.addComponent(targetId, { type: 'health', current: 20, max: 20 } as Health);
    world.addComponent(targetId, { type: 'faction', team: 'enemy' } as Faction);
    world.addComponent(targetId, { type: 'status', effects: new Map() } as StatusEffect);

    return { world, casterId, targetId };
  }

  it('deals damage to a target on the hit tile', () => {
    const { world, casterId, targetId } = makeWorld();
    const spell = makeFlameBolt();
    const addMessage = vi.fn();
    resolveSpell(world, casterId, spell, 3, 0, makeMap(), addMessage);
    const hp = world.getComponent<Health>(targetId, 'health')!;
    expect(hp.current).toBe(20 - spell.damage);
  });

  it('applies status effects', () => {
    const { world, casterId, targetId } = makeWorld();
    const spell = makeFlameBolt();
    resolveSpell(world, casterId, spell, 3, 0, makeMap(), vi.fn());
    const status = world.getComponent<StatusEffect>(targetId, 'status')!;
    expect(status.effects.has('burning')).toBe(true);
  });

  it('removes entity when HP drops to 0', () => {
    const { world, casterId, targetId } = makeWorld();
    world.getComponent<Health>(targetId, 'health')!.current = 1;
    resolveSpell(world, casterId, makeFlameBolt(), 3, 0, makeMap(), vi.fn());
    expect(world.hasComponent(targetId, 'health')).toBe(false);
  });

  it('reports miss when no target on tile', () => {
    const { world, casterId } = makeWorld();
    const addMessage = vi.fn();
    resolveSpell(world, casterId, makeFlameBolt(), 8, 8, makeMap(), addMessage);
    expect(addMessage).toHaveBeenCalledWith(expect.stringContaining('hits nothing'));
  });

  it('decrements charges by 1', () => {
    const { world, casterId } = makeWorld();
    const spell = makeFlameBolt();
    const before = spell.charges;
    resolveSpell(world, casterId, spell, 3, 0, makeMap(), vi.fn());
    expect(spell.charges).toBe(before - 1);
  });

  it('does not spend charges below 0', () => {
    const { world, casterId } = makeWorld();
    const spell = makeFlameBolt();
    spell.charges = 0;
    resolveSpell(world, casterId, spell, 3, 0, makeMap(), vi.fn());
    expect(spell.charges).toBe(0);
  });
});
