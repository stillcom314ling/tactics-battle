# Systems Reference

Per-module contracts: what each module owns, what it accepts, and what it returns or mutates.

---

## src/core/ECS.ts

**Owns:** Entity registry, component storage, query engine.

| Export | Signature | Notes |
|--------|-----------|-------|
| `World` class | — | Central ECS state. Pass by reference everywhere. |
| `World.createEntity()` | `() → EntityId` | Monotonically increasing integer. |
| `World.addComponent()` | `(id, component) → void` | Overwrites if same type already exists. |
| `World.getComponent<T>()` | `(id, type) → T \| undefined` | Returns undefined if not present. |
| `World.query()` | `(...types) → EntityId[]` | Returns entities having ALL listed components. |
| `World.removeEntity()` | `(id) → void` | Removes all components. |
| `applyStatus()` | `(status, effect, turns) → void` | Keeps the longer duration if already set. |
| `tickStatuses()` | `(status) → void` | Decrements durations; removes expired effects. |

**Component types defined here:** `Position`, `Renderable`, `Health`, `Faction`, `StatusEffect`, `Movement`, `Combat`, `Terrain`, `Ability`, `Abilities`, `AI`, `Label`.

---

## src/core/TurnManager.ts

**Owns:** The authoritative game phase state and turn counter.

| Export | Notes |
|--------|-------|
| `TurnManager.phase` | `'player' \| 'enemy' \| 'game_over'` |
| `TurnManager.turnNumber` | Incremented on each `endPlayerTurn()` call |
| `TurnManager.isPlayerTurn` | Convenience boolean |
| `TurnManager.isGameOver` | Convenience boolean |
| `TurnManager.endPlayerTurn()` | Switches phase → enemy |
| `TurnManager.endEnemyPhase()` | Switches phase → player |
| `TurnManager.setGameOver()` | Switches phase → game_over |
| `TurnManager.onPhaseChange` | Optional callback fired on every transition |

---

## src/core/InteractionSystem.ts

**Owns:** Emergent rule registry and dispatcher.

| Export | Signature |
|--------|-----------|
| `INTERACTION_RULES` | `InteractionRule[]` — add to this array to add rules |
| `processInteractions()` | `(world, entityId, context) → void` |

`InteractionContext.trigger` values: `'turn_start'` | `'attack'` | `'move'` | `'ability'`.

---

## src/combat/CombatSystem.ts

**Owns:** Melee and ranged attack resolution. No state.

| Export | Signature | Notes |
|--------|-----------|-------|
| `resolveMelee()` | `(world, attackerId, defenderId, playerId, callbacks) → void` | Calls `callbacks.onPlayerDied` if defender HP ≤ 0 and is the player. |
| `resolveRangedAttack()` | `(world, attackerId, targetId, damage, map, playerId, callbacks) → boolean` | Returns false if LoS blocked. |
| `hasLineOfSight()` | `(attackerPos, targetPos, map) → boolean` | Cardinal + diagonal only. |
| `getActorAt()` | `(world, col, row, team?) → EntityId \| null` | Optional team filter. |

`CombatCallbacks`: `{ addMessage, onPlayerHit, onPlayerDied, alertEnemies }`.

---

## src/ai/AISystem.ts

**Owns:** Enemy turn dispatch and alert system. No state (stateless functions).

| Export | Signature | Notes |
|--------|-----------|-------|
| `processEnemyTurns()` | `(world, playerId, context) → boolean` | Returns false if player died during enemy turns. Ticks statuses at end. |
| `alertEnemiesNear()` | `(world, col, row, radius) → boolean` | Returns true if any new enemies were alerted. |
| `STRATEGY_REGISTRY` | `Record<string, EnemyStrategy>` | Register new strategies here. |

`EnemyTurnContext`: all dependencies injected by `Game.ts` including `findPath`, `moveEntity`, `resolveMelee`, `resolveRangedAttack`, `getActorAt`, `alertEnemies`.

---

## src/ai/strategies/

**Each strategy:** `(enemyId: EntityId, ctx: EnemyTurnContext) → void`

| File | Strategy name | Behaviour |
|------|--------------|-----------|
| `BasicStrategy.ts` | `basic`, `swarm` | Rush → melee if adjacent |
| `RangedStrategy.ts` | `ranged` | Back off if too close; ranged shot; approach if out of range |
| `BruteStrategy.ts`  | `brute` | Acts only every other turn; same as basic when acting |

---

## src/game/SpellSystem.ts

**Owns:** Spell factories and resolution math. No state.

| Export | Signature | Notes |
|--------|-----------|-------|
| `makeFlameBolt()` etc. | `() → Ability` | Returns a fresh Ability with full charges. |
| `getSpellTiles()` | `(casterCol, casterRow, targetCol, targetRow, ability, map) → [number,number][]` | Returns tiles hit by the spell. |
| `getRangeTiles()` | `(col, row, range, map) → [number,number][]` | Circle of tiles for targeting highlight. |
| `resolveSpell()` | `(world, caster, ability, targetCol, targetRow, map, addMessage) → void` | Applies damage, status, interactions. Decrements charge. |
| `refillSpellCharges()` | `(world) → void` | Call on floor start. |

---

## src/game/TargetingController.ts

**Owns:** Spell-targeting state machine and visual highlight.

| Method | Notes |
|--------|-------|
| `enter(ability)` | Enter targeting mode; shows range highlight. |
| `cancel()` | Clear and return to free mode. |
| `clearAfterCast()` | Clear without messaging (after a successful cast). |
| `handleTap(col, row)` | Returns `'cancel' \| 'preview' \| 'cast' \| 'out_of_range'`. |
| `getPendingTarget()` | First-tap position for aoe/line two-tap confirm, or null. |
| `state` getter | `TargetingState` — mode, selectedAbility, pendingTarget. |

Constructor requires: `world`, `renderer`, `map`, `playerId`, `TargetingCallbacks`.

---

## src/game/InputHandler.ts

**Owns:** All browser event listeners.

Constructor: `(app, camera, isMenuOpen, callbacks)`.

`InputCallbacks`: `{ onTap(x,y), onDirection(dx,dy), onWait() }`.

Keyboard map (already implemented):
- Arrow keys / WASD → `onDirection`
- Numpad 1-9 → `onDirection` + `onWait` (5)
- `.` or `5` → `onWait`

Call `destroy()` to remove all listeners.

---

## src/rendering/ParallaxAsciiRenderer.ts

**Owns:** PixiJS Text objects for all layers. Camera parallax offset.

| Method | Notes |
|--------|-------|
| `addLayer(config)` | Call once during init. Sorted by depth automatically. |
| `setCell(layerId, col, row, cell)` | Pass `null` to clear a cell. |
| `setLayerData(layerId, data[][])` | Bulk-set from a 2D array. |
| `updateCamera(screenW, screenH)` | Call every frame after setting `cameraX`/`cameraY`. |
| `getLayerScreenOffset(layerId)` | Returns `{x, y, cellSize}` for overlay alignment. |

---

## src/rendering/HUDRenderer.ts

**Owns:** HUD text nodes, entity HP bars, hit-flash filter.

| Method | Notes |
|--------|-------|
| `addMessage(msg)` | Prepends to rolling log (max `MESSAGE_LOG_MAX` lines). |
| `triggerHitFlash()` | Starts red-screen countdown. |
| `update(world, playerId, phase, mode, ability, pendingSet, dpad, fab, fabLabel)` | Call every frame. Updates all text and repositions FAB/D-Pad. |
| `drawHealthBars(world, playerId)` | Call every frame. Re-draws HP bars from live screen coords. |
| `tickHitFlash()` | Call every frame. Applies/removes hit-flash filter. |

---

## src/game/RenderSystem.ts

**Owns:** Actor glyph synchronisation to the gameplay layer.

| Export | Signature |
|--------|-----------|
| `drawActors(world, renderer)` | Draw all `position + renderable + faction` entities with status/HP tint. |
| `restoreTerrain(renderer, map, col, row)` | Reset one gameplay tile to map data. |
