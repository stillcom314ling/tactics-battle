/**
 * ECS Tests
 *
 * Tests for the Entity-Component System core:
 * World, component CRUD, query, applyStatus, tickStatuses.
 */

import { describe, it, expect, beforeEach } from 'vitest';
import {
  World, applyStatus, tickStatuses,
  Position, Health, Faction, StatusEffect,
} from '../core/ECS';

// ─── WORLD ────────────────────────────────────────────────────────────────────

describe('World', () => {
  let world: World;
  beforeEach(() => { world = new World(); });

  it('creates entities with unique IDs', () => {
    const a = world.createEntity();
    const b = world.createEntity();
    expect(a).not.toBe(b);
  });

  it('stores and retrieves a component', () => {
    const id = world.createEntity();
    world.addComponent(id, { type: 'position', col: 3, row: 5, layer: 'gameplay' } as Position);
    const pos = world.getComponent<Position>(id, 'position');
    expect(pos?.col).toBe(3);
    expect(pos?.row).toBe(5);
  });

  it('returns undefined for missing component', () => {
    const id = world.createEntity();
    expect(world.getComponent(id, 'health')).toBeUndefined();
  });

  it('hasComponent returns correct boolean', () => {
    const id = world.createEntity();
    expect(world.hasComponent(id, 'faction')).toBe(false);
    world.addComponent(id, { type: 'faction', team: 'player' } as Faction);
    expect(world.hasComponent(id, 'faction')).toBe(true);
  });

  it('removeComponent deletes a single component', () => {
    const id = world.createEntity();
    world.addComponent(id, { type: 'faction', team: 'enemy' } as Faction);
    world.removeComponent(id, 'faction');
    expect(world.hasComponent(id, 'faction')).toBe(false);
  });

  it('removeEntity clears all its components', () => {
    const id = world.createEntity();
    world.addComponent(id, { type: 'health', current: 10, max: 10 } as Health);
    world.removeEntity(id);
    expect(world.hasComponent(id, 'health')).toBe(false);
    expect(world.allEntities()).not.toContain(id);
  });

  it('query returns entities with ALL required components', () => {
    const a = world.createEntity();
    const b = world.createEntity();
    world.addComponent(a, { type: 'position', col: 0, row: 0, layer: 'gameplay' } as Position);
    world.addComponent(a, { type: 'health',   current: 10, max: 10 } as Health);
    world.addComponent(b, { type: 'position', col: 1, row: 1, layer: 'gameplay' } as Position);
    // b has no health

    const result = world.query('position', 'health');
    expect(result).toContain(a);
    expect(result).not.toContain(b);
  });

  it('query returns empty array when no entities match', () => {
    world.createEntity();
    expect(world.query('health', 'combat')).toHaveLength(0);
  });

  it('overwrites a component when added again', () => {
    const id = world.createEntity();
    world.addComponent(id, { type: 'health', current: 10, max: 10 } as Health);
    world.addComponent(id, { type: 'health', current: 50, max: 100 } as Health);
    expect(world.getComponent<Health>(id, 'health')?.current).toBe(50);
  });
});

// ─── STATUS EFFECTS ────────────────────────────────────────────────────────────

describe('applyStatus', () => {
  it('sets a new effect', () => {
    const s: StatusEffect = { type: 'status', effects: new Map() };
    applyStatus(s, 'burning', 3);
    expect(s.effects.get('burning')).toBe(3);
  });

  it('keeps the longer duration when re-applied shorter', () => {
    const s: StatusEffect = { type: 'status', effects: new Map([['burning', 5]]) };
    applyStatus(s, 'burning', 2);
    expect(s.effects.get('burning')).toBe(5);
  });

  it('overwrites with a longer duration', () => {
    const s: StatusEffect = { type: 'status', effects: new Map([['burning', 2]]) };
    applyStatus(s, 'burning', 6);
    expect(s.effects.get('burning')).toBe(6);
  });
});

describe('tickStatuses', () => {
  it('decrements durations by 1', () => {
    const s: StatusEffect = { type: 'status', effects: new Map([['poisoned', 3]]) };
    tickStatuses(s);
    expect(s.effects.get('poisoned')).toBe(2);
  });

  it('removes effects that reach 0', () => {
    const s: StatusEffect = { type: 'status', effects: new Map([['slowed', 1]]) };
    tickStatuses(s);
    expect(s.effects.has('slowed')).toBe(false);
  });

  it('removes effects at 1 but keeps effects > 1', () => {
    const s: StatusEffect = { type: 'status', effects: new Map([['burning', 1], ['wet', 3]]) };
    tickStatuses(s);
    expect(s.effects.has('burning')).toBe(false);
    expect(s.effects.get('wet')).toBe(2);
  });
});
