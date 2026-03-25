# Best Practices

Conventions for this codebase. Read before writing code.

---

## 1. No magic numbers — ever

All tunable values live in `src/constants/`. If you type a raw number outside a constants file, it's wrong.

```typescript
// BAD
setTimeout(() => this.runEnemyPhase(), 140);

// GOOD
import { ENEMY_TURN_DELAY_MS } from '../constants/combat';
setTimeout(() => this.runEnemyPhase(), ENEMY_TURN_DELAY_MS);
```

---

## 2. Pure functions before classes

Prefer exported functions over methods when a piece of logic has no persistent state.

```typescript
// Prefer this (testable, importable anywhere)
export function resolveMelee(world, attackerId, defenderId, playerId, callbacks) { … }

// Over embedding it in a class as a private method (untestable without the whole class)
class Game {
  private enemyMelee(attackerId, defenderId) { … }
}
```

---

## 3. Callbacks / context objects for side-effects

When a pure function needs to trigger a side-effect (log a message, play a flash, signal death), pass it a narrow context or callback interface. Never import `Game` or `HUDRenderer` into `CombatSystem` or `AISystem`.

```typescript
export interface CombatCallbacks {
  addMessage:   (msg: string)  => void;
  onPlayerHit:  ()             => void;
  onPlayerDied: ()             => void;
  alertEnemies: (col, row, r)  => void;
}
```

This is the functional equivalent of C# interfaces. Mock the interface in tests with `vi.fn()`.

---

## 4. Layer isolation — no upward imports

Inner layers must never import from outer layers:

| Layer      | May import from              |
|------------|------------------------------|
| `constants/` | nothing                    |
| `utils/`   | `constants/`                 |
| `core/`    | `constants/` `utils/`        |
| `combat/`  | `core/`                      |
| `ai/`      | `core/` `combat/` `constants/` |
| `rendering/` | `constants/` `utils/`      |
| `game/`    | everything above             |
| `ui/`      | `core/` `constants/`         |
| `Game.ts`  | everything                   |

If you find yourself importing `Game` inside `AISystem`, that's a sign the dependency belongs the other way — use a callback.

---

## 5. ECS queries — use the smallest set of components

```typescript
// BAD — queries too broadly, iterates every entity with 'position'
for (const id of world.query('position')) { … }

// GOOD — only entities with all three components are returned
for (const id of world.query('position', 'health', 'faction')) { … }
```

---

## 6. Never force-unwrap a component without a guard

```typescript
// BAD — throws if position is missing
const pos = world.getComponent<Position>(id, 'position')!;

// GOOD — guard before using
const pos = world.getComponent<Position>(id, 'position');
if (!pos) return;
```

The `!` (non-null assertion) is only acceptable inside loops that already filtered via `world.query()` for that component type — because the query guarantees presence.

---

## 7. Adding a test is mandatory for new systems

Every new module in `core/`, `combat/`, `ai/`, `game/SpellSystem`, or `utils/` must have a corresponding test file in `src/tests/`. Systems that only touch PixiJS (rendering) can be tested at the integration level.

Run tests: `npm test`
Watch mode: `npm run test:watch`
Coverage:   `npm run test:coverage`

---

## 8. Strategy pattern for enemy AI

New enemy behaviour = new file in `src/ai/strategies/`, exporting one function:

```typescript
export function executeMyTurn(enemyId: EntityId, ctx: EnemyTurnContext): void { … }
```

Register it in `STRATEGY_REGISTRY` in `AISystem.ts`. Add the matching `strategy` value to `AI['strategy']` union in `ECS.ts`. Do not add if/else chains to existing strategies.

---

## 9. TypeScript strictness

`tsconfig.json` has `strict: true`. This means:
- No implicit `any` — type everything explicitly
- No `!` on values that might genuinely be null at runtime
- Use `as const` for literal arrays/objects used as read-only config

---

## 10. File size targets

| File               | Target max lines |
|--------------------|-----------------|
| `Game.ts`          | ~320            |
| Any system file    | ~250            |
| Strategy files     | ~60             |
| Constants files    | ~60             |
| Test files         | ~200            |

If a file exceeds its target, it probably needs splitting.

---

## 11. Commit hygiene

- One logical change per commit
- Commits to `main` only via PR
- Feature work on `claude/<feature-name>` branches
- Run `npm test && npm run build` before committing — both must pass
