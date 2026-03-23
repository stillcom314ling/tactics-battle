/**
 * ENTITY-COMPONENT SYSTEM (lightweight)
 * 
 * Not a full ECS library — just enough structure for emergent tactical mechanics.
 * Entities are just IDs. Components are plain objects stored by type.
 * Systems are functions that iterate over entities with specific components.
 * 
 * This keeps things simple for Claude Code to extend while still enabling
 * the composable "Fire + Oil = Explosion" interaction patterns you want.
 */

export type EntityId = number;

export interface Component {
  type: string;
}

// --- POSITION ---
export interface Position extends Component {
  type: 'position';
  col: number;
  row: number;
  layer: string; // which render layer this entity appears on
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

// --- STATUS EFFECTS (for emergent interactions) ---
export interface StatusEffect extends Component {
  type: 'status';
  effects: Set<string>; // e.g. 'burning', 'wet', 'stunned', 'poisoned'
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
export interface Ability {
  id: string;
  name: string;
  element: 'fire' | 'lightning' | 'ice' | 'poison' | 'arcane' | 'none';
  range: number;
  pattern: 'single' | 'line' | 'aoe' | 'self';
  aoeRadius?: number;
  damage: number;
  effects: string[]; // status labels to apply: 'burning', 'wet', 'stunned', etc.
  cooldownMax: number;
  cooldownCurrent: number;
}

// --- ABILITIES (entity's spell list) ---
export interface Abilities extends Component {
  type: 'abilities';
  list: Ability[];
}

// ---------- THE WORLD ----------

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

  /**
   * Get all entity IDs.
   */
  allEntities(): EntityId[] {
    return [...this.entities];
  }
}
