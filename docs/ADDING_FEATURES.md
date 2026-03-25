# Adding Features — Cookbook

Step-by-step guides for the most common extension tasks.
Each section lists exactly which files to touch and in what order.

---

## 1. Add a new spell

**Files to touch:** `src/game/SpellSystem.ts` only.

```typescript
// 1. Add a factory function at the bottom of SpellSystem.ts
export function makeChainLightning(): Ability {
  return {
    id: 'chain_lightning',
    name: 'Chain Lightning',
    element: 'lightning',
    range: 4,
    pattern: 'line',
    damage: 12,
    effects: ['shocked', 'stunned'],
    charges: 1,
    chargesMax: 1,
  };
}
```

```typescript
// 2. Register it in MapGenerator.ts → populateWorld() → player abilities list
world.addComponent(playerId, {
  type: 'abilities',
  list: [makeFlameBolt(), makeArcLightning(), makeFrostShard(), makePoisonCloud(), makeChainLightning()],
} as Abilities);
```

That's it. The ActionMenu, targeting, and spell resolution all pick it up automatically.

---

## 2. Add a new enemy type

**Files to touch:** `src/game/MapGenerator.ts` (ENEMY_TYPES array) and optionally a new strategy file.

```typescript
// In ENEMY_TYPES array in MapGenerator.ts:
{ char: 'M', name: 'Mage', color: 0xcc44ff,
  hpMin: 12, hpMax: 18, attack: 8, defense: 0, moveRange: 2,
  strategy: 'basic',   // use existing or add a new one
  weight: 1 },
```

If you need custom behaviour, add a new strategy:

```typescript
// src/ai/strategies/MageStrategy.ts
import { EntityId } from '../../core/ECS';
import { EnemyTurnContext } from '../AISystem';

export function executeMageTurn(enemyId: EntityId, ctx: EnemyTurnContext): void {
  // custom logic here
}
```

```typescript
// Register in src/ai/AISystem.ts → STRATEGY_REGISTRY:
import { executeMageTurn } from './strategies/MageStrategy';

export const STRATEGY_REGISTRY: Record<string, EnemyStrategy> = {
  // …existing…
  mage: executeMageTurn,
};
```

---

## 3. Add a new status effect

**Files to touch:** `src/constants/combat.ts`, `src/core/ECS.ts`, optionally `src/core/InteractionSystem.ts`.

```typescript
// 1. Add duration constant in src/constants/combat.ts
export const STATUS_DURATION_FROZEN = 2;

// 2. Add to STATUS_DURATIONS in src/core/ECS.ts
import { STATUS_DURATION_FROZEN } from '../constants/combat';

export const STATUS_DURATIONS: Record<string, number> = {
  // …existing…
  frozen: STATUS_DURATION_FROZEN,
};

// 3. (optional) Add a visual tint in src/constants/rendering.ts → STATUS_COLORS
export const STATUS_COLORS: Record<string, number> = {
  // …existing…
  frozen: 0x88ddff,
};

// 4. (optional) Add an interaction rule in src/core/InteractionSystem.ts
{
  name: 'frozen_entity_skip',
  description: 'Frozen entities cannot move',
  predicate: (_world, entityId, ctx) => {
    if (ctx.trigger !== 'turn_start') return false;
    const status = _world.getComponent<StatusEffect>(entityId, 'status');
    return status?.effects.has('frozen') ?? false;
  },
  effect: (world, entityId, ctx) => {
    // Logic here
  },
},
```

---

## 4. Add a new interaction rule

**Files to touch:** `src/core/InteractionSystem.ts` only.

Add one object to the `INTERACTION_RULES` array:

```typescript
{
  name: 'slowed_plus_ice',
  description: 'Slowed entities hit by ice are frozen',
  predicate: (_world, entityId, ctx) => {
    if (ctx.trigger !== 'attack' || ctx.data?.['element'] !== 'ice') return false;
    const status = _world.getComponent<StatusEffect>(entityId, 'status');
    return status?.effects.has('slowed') ?? false;
  },
  effect: (world, entityId, ctx) => {
    const status = world.getComponent<StatusEffect>(entityId, 'status');
    if (status) {
      applyStatus(status, 'frozen', STATUS_DURATIONS['frozen'] ?? 2);
      ctx.addMessage?.('Frozen solid!');
    }
  },
},
```

---

## 5. Balance tuning

Edit **`src/constants/combat.ts`** only. No logic files need to change:

```typescript
export const BURNING_DAMAGE_PER_TURN = 4;   // was 3
export const ENEMY_TURN_DELAY_MS     = 80;   // faster enemies
export const PLAYER_HP_MAX           = 120;  // tankier player
```

---

## 6. Add FOV / fog of war

**Files to touch:** `src/game/RenderSystem.ts`, `src/game/Game.ts`.

```typescript
// In Game.ts — recompute FOV set after each player move
import * as ROT from 'rot-js';
const fov = new ROT.FOV.PreciseShadowcasting((x, y) => map.walkable[y]?.[x] ?? false);
const visible = new Set<string>();
fov.compute(playerCol, playerRow, 8, (x, y) => visible.add(`${x},${y}`));

// In RenderSystem.drawActors() — pass the visible set
export function drawActors(world, renderer, visible?: Set<string>) {
  for (const id of world.query('position', 'renderable', 'faction')) {
    const pos = world.getComponent<Position>(id, 'position')!;
    const key = `${pos.col},${pos.row}`;
    if (visible && !visible.has(key)) continue; // hide or dim
    // … rest of draw logic
  }
}
```

---

## 7. Add tests for a new module

Create `src/tests/myModule.test.ts`:

```typescript
import { describe, it, expect, vi } from 'vitest';
import { myFunction } from '../myModule';

describe('myFunction', () => {
  it('does the expected thing', () => {
    const result = myFunction(mockInput);
    expect(result).toEqual(expectedOutput);
  });
});
```

Run with `npm test`. The Vitest config picks up any file matching `src/tests/**/*.test.ts`.

---

## 8. Add floor progression

**Files to touch:** `src/game/Game.ts`, `src/game/MapGenerator.ts`.

```typescript
// In Game.ts — detect floor clear
const enemyCount = world.query('faction').filter(
  id => world.getComponent<Faction>(id, 'faction')?.team === 'enemy'
).length;

if (enemyCount === 0) {
  this.hudRenderer.addMessage('Floor cleared! Find the stairs.');
  // Spawn staircase entity at a random floor tile
}

// On staircase step — reset world, generate new map, keep player HP + abilities
const { playerId: newPlayerId } = populateWorld(newWorld, newMap);
// Copy player HP and abilities from old player to new player
```
