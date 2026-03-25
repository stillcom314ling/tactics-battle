/**
 * ENTITY-COMPONENT SYSTEM (lightweight)
 *
 * Not a full ECS library — just enough structure for emergent tactical mechanics.
 * Entities are just IDs. Components are plain objects stored by type.
 * Systems are functions that iterate over entities with specific components.
 *
 * This keeps things simple while enabling composable "Fire + Oil = Explosion"
 * interaction patterns.
 */

import {
  STATUS_DURATION_BURNING,
  STATUS_DURATION_POISONED,
  STATUS_DURATION_SLOWED,
  STATUS_DURATION_STUNNED,
  STATUS_DURATION_SHOCKED,
  STATUS_DURATION_WET,
} from '../constants/combat';

export type EntityId = number;

export interface Component {
  type: string;
}

// --- POSITION ---
export interface Position extends Component {
  type: 'position';
  col: number;
  row: number;
  /** Which render layer this entity appears on (e.g. 'gameplay'). */
  layer: string;
}

// --- RENDERABLE ---
export interface Renderable extends Component {
  type: 'renderable';
  char: string;
  fg: number;
  bg?: number;
  alpha?: number;
}

// --- HEALTH ---
export interface Health extends Component {
  type: 'health';
  current: number;
  max: number;
}

// --- FACTION ---
export interface Faction extends Component {
  type: 'faction';
  team: 'player' | 'enemy' | 'neutral';
}

// --- STATUS EFFECTS ---
/** effects: Map of effect name → turns remaining (-1 = permanent until cleared). */
export interface StatusEffect extends Component {
  type: 'status';
  effects: Map<string, number>;
}

/**
 * Default durations per status effect.
 * Values live in constants/combat.ts so they can be tuned in one place.
 */
export const STATUS_DURATIONS: Record<string, number> = {
  burning:  STATUS_DURATION_BURNING,
  poisoned: STATUS_DURATION_POISONED,
  slowed:   STATUS_DURATION_SLOWED,
  stunned:  STATUS_DURATION_STUNNED,
  shocked:  STATUS_DURATION_SHOCKED,
  wet:      STATUS_DURATION_WET,
};

/** Apply a status effect, keeping the longer duration if already present. */
export function applyStatus(status: StatusEffect, effect: string, turns: number): void {
  const existing = status.effects.get(effect) ?? 0;
  if (existing < turns) status.effects.set(effect, turns);
}

/** Tick all status durations down by 1, removing expired effects. */
export function tickStatuses(status: StatusEffect): void {
  for (const [eff, turns] of status.effects) {
    if (turns <= 1) status.effects.delete(eff);
    else status.effects.set(eff, turns - 1);
  }
}

// --- MOVEMENT ---
export interface Movement extends Component {
  type: 'movement';
  range: number;    // tiles per turn
  canFly: boolean;
}

// --- COMBAT ---
export interface Combat extends Component {
  type: 'combat';
  attackPower: number;
  defense: number;
  attackRange: number;
}

// --- TERRAIN ---
export interface Terrain extends Component {
  type: 'terrain';
  walkable: boolean;
  transparent: boolean; // for line of sight
  properties: Set<string>; // e.g. 'flammable', 'conductive', 'slippery'
}

// --- ABILITY (a single spell / skill) ---
/**
 * Uses per-level charges instead of per-turn cooldowns.
 * Charges refill at the start of each new level.
 */
export interface Ability {
  id: string;
  name: string;
  element: 'fire' | 'lightning' | 'ice' | 'poison' | 'arcane' | 'none';
  range: number;
  pattern: 'single' | 'line' | 'aoe' | 'self';
  aoeRadius?: number;
  damage: number;
  effects: string[];   // status labels to apply on hit
  charges: number;     // remaining uses this level
  chargesMax: number;  // total uses per level
}

// --- ABILITIES (entity's spell list) ---
export interface Abilities extends Component {
  type: 'abilities';
  list: Ability[];
}

// --- AI (enemy behaviour strategy) ---
export interface AI extends Component {
  type: 'ai';
  strategy: 'basic' | 'ranged' | 'brute' | 'swarm';
  alerted: boolean;    // true = rush player regardless of normal caution
  alertRange: number;  // passive detection radius
  moveCounter: number; // brute: incremented each turn, acts only on even counts
}

// --- LABEL ---
export interface Label extends Component {
  type: 'label';
  name: string;
}

// ─── THE WORLD ────────────────────────────────────────────────────────────────

export class World {
  private nextId: EntityId = 1;
  private entities: Set<EntityId> = new Set();
  private components: Map<string, Map<EntityId, Component>> = new Map();

  createEntity(): EntityId {
    const id = this.nextId++;
    this.entities.add(id);
    return id;
  }

  removeEntity(id: EntityId) {
    this.entities.delete(id);
    for (const store of this.components.values()) {
      store.delete(id);
    }
  }

  addComponent<T extends Component>(entityId: EntityId, component: T) {
    if (!this.components.has(component.type)) {
      this.components.set(component.type, new Map());
    }
    this.components.get(component.type)!.set(entityId, component);
  }

  getComponent<T extends Component>(entityId: EntityId, type: string): T | undefined {
    return this.components.get(type)?.get(entityId) as T | undefined;
  }

  hasComponent(entityId: EntityId, type: string): boolean {
    return this.components.get(type)?.has(entityId) ?? false;
  }

  removeComponent(entityId: EntityId, type: string) {
    this.components.get(type)?.delete(entityId);
  }

  /**
   * Query all entities that have ALL of the specified component types.
   */
  query(...types: string[]): EntityId[] {
    const results: EntityId[] = [];
    for (const id of this.entities) {
      if (types.every((t) => this.hasComponent(id, t))) {
        results.push(id);
      }
    }
    return results;
  }

  /** Get all entity IDs. */
  allEntities(): EntityId[] {
    return [...this.entities];
  }
}
