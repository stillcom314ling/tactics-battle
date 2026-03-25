/**
 * InteractionSystem Tests
 *
 * Tests for all five built-in interaction rules plus the processInteractions dispatcher.
 */

import { describe, it, expect, vi } from 'vitest';
import { processInteractions, INTERACTION_RULES } from '../core/InteractionSystem';
import { World, Position, Health, StatusEffect, Terrain, applyStatus } from '../core/ECS';
import { BURNING_DAMAGE_PER_TURN, POISON_DAMAGE_PER_TURN, FIRE_TERRAIN_DAMAGE } from '../constants/combat';

// ─── FIXTURE HELPERS ─────────────────────────────────────────────────────────

function makeEntity(world: World, overrides: {
  col?: number; row?: number;
  hp?: number; status?: string[];
  terrain?: string[];
}) {
  const id = world.createEntity();
  world.addComponent(id, { type: 'position', col: overrides.col ?? 0, row: overrides.row ?? 0, layer: 'gameplay' } as Position);
  world.addComponent(id, { type: 'health', current: overrides.hp ?? 30, max: 30 } as Health);
  const effects = new Map<string, number>();
  for (const eff of overrides.status ?? []) effects.set(eff, 5);
  world.addComponent(id, { type: 'status', effects } as StatusEffect);
  return id;
}

function makeTerrain(world: World, col: number, row: number, props: string[]) {
  const id = world.createEntity();
  world.addComponent(id, { type: 'position', col, row, layer: 'gameplay' } as Position);
  world.addComponent(id, { type: 'terrain', walkable: true, transparent: true, properties: new Set(props) } as Terrain);
  return id;
}

// ─── BURNING DAMAGE ────────────────────────────────────────────────────────────

describe('burning_entity_damage rule', () => {
  it('deals damage on turn_start when burning', () => {
    const world = new World();
    const id = makeEntity(world, { hp: 20, status: ['burning'] });
    processInteractions(world, id, { trigger: 'turn_start', addMessage: vi.fn() });
    expect(world.getComponent<Health>(id, 'health')!.current).toBe(20 - BURNING_DAMAGE_PER_TURN);
  });

  it('does not trigger on other events', () => {
    const world = new World();
    const id = makeEntity(world, { hp: 20, status: ['burning'] });
    processInteractions(world, id, { trigger: 'attack', addMessage: vi.fn() });
    expect(world.getComponent<Health>(id, 'health')!.current).toBe(20);
  });

  it('does not trigger without burning status', () => {
    const world = new World();
    const id = makeEntity(world, { hp: 20, status: [] });
    processInteractions(world, id, { trigger: 'turn_start', addMessage: vi.fn() });
    expect(world.getComponent<Health>(id, 'health')!.current).toBe(20);
  });
});

// ─── POISON DAMAGE ────────────────────────────────────────────────────────────

describe('poisoned_entity_damage rule', () => {
  it('deals damage on turn_start when poisoned', () => {
    const world = new World();
    const id = makeEntity(world, { hp: 20, status: ['poisoned'] });
    processInteractions(world, id, { trigger: 'turn_start', addMessage: vi.fn() });
    expect(world.getComponent<Health>(id, 'health')!.current).toBe(20 - POISON_DAMAGE_PER_TURN);
  });
});

// ─── WET + LIGHTNING ─────────────────────────────────────────────────────────

describe('wet_plus_lightning rule', () => {
  it('deals bonus damage and stuns when wet + lightning attack', () => {
    const world = new World();
    const id = makeEntity(world, { hp: 30, status: ['wet'] });
    const addMessage = vi.fn();
    processInteractions(world, id, {
      trigger: 'attack',
      data:    { element: 'lightning', damage: 10 },
      addMessage,
    });
    const hp = world.getComponent<Health>(id, 'health')!.current;
    expect(hp).toBe(30 - 10); // bonus damage equal to spell damage
    const status = world.getComponent<StatusEffect>(id, 'status')!;
    expect(status.effects.has('stunned')).toBe(true);
    expect(status.effects.has('wet')).toBe(false); // wet consumed
  });

  it('does not trigger for non-lightning elements', () => {
    const world = new World();
    const id = makeEntity(world, { hp: 30, status: ['wet'] });
    processInteractions(world, id, {
      trigger: 'attack',
      data:    { element: 'fire', damage: 10 },
      addMessage: vi.fn(),
    });
    expect(world.getComponent<Health>(id, 'health')!.current).toBe(30);
  });

  it('does not trigger when entity is not wet', () => {
    const world = new World();
    const id = makeEntity(world, { hp: 30, status: [] });
    processInteractions(world, id, {
      trigger: 'attack',
      data:    { element: 'lightning', damage: 10 },
      addMessage: vi.fn(),
    });
    expect(world.getComponent<Health>(id, 'health')!.current).toBe(30);
  });
});

// ─── FIRE TERRAIN ─────────────────────────────────────────────────────────────

describe('fire_terrain_damage rule', () => {
  it('deals damage when standing on on_fire terrain', () => {
    const world = new World();
    const id = makeEntity(world, { col: 3, row: 3, hp: 30 });
    makeTerrain(world, 3, 3, ['on_fire']);
    processInteractions(world, id, { trigger: 'turn_start', addMessage: vi.fn() });
    expect(world.getComponent<Health>(id, 'health')!.current).toBe(30 - FIRE_TERRAIN_DAMAGE);
  });

  it('applies burning to entity standing on on_fire terrain', () => {
    const world = new World();
    const id = makeEntity(world, { col: 3, row: 3, hp: 30 });
    makeTerrain(world, 3, 3, ['on_fire']);
    processInteractions(world, id, { trigger: 'turn_start', addMessage: vi.fn() });
    const status = world.getComponent<StatusEffect>(id, 'status')!;
    expect(status.effects.has('burning')).toBe(true);
  });

  it('does not trigger on flammable (not on_fire) terrain', () => {
    const world = new World();
    const id = makeEntity(world, { col: 3, row: 3, hp: 30 });
    makeTerrain(world, 3, 3, ['flammable']);
    processInteractions(world, id, { trigger: 'turn_start', addMessage: vi.fn() });
    expect(world.getComponent<Health>(id, 'health')!.current).toBe(30);
  });
});

// ─── FIRE SPREADS TO FLAMMABLE ────────────────────────────────────────────────

describe('fire_spreads_to_flammable rule', () => {
  it('converts flammable terrain to on_fire when burning entity stands on it', () => {
    const world = new World();
    const id = makeEntity(world, { col: 2, row: 2, status: ['burning'] });
    const tid = makeTerrain(world, 2, 2, ['flammable']);
    // Rule does not require turn_start trigger - just a predicate check
    processInteractions(world, id, { trigger: 'turn_start', addMessage: vi.fn() });
    const terrain = world.getComponent<Terrain>(tid, 'terrain')!;
    expect(terrain.properties.has('on_fire')).toBe(true);
    expect(terrain.properties.has('flammable')).toBe(false);
  });
});
