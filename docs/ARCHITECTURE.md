# Architecture

ASCII Tactics is a turn-based tactical game. This document describes the overall system layout, dependency rules, and data flow.

---

## Directory layout

```
src/
  constants/          # Tunable values — no logic
    map.ts            # MAP_W, MAP_H, dungeon options, tile char sets
    combat.ts         # Damage numbers, durations, alert radii, player stats
    rendering.ts      # Layer depths, colors, cell size, camera physics
    ui.ts             # Panel widths, font sizes, widget offsets

  utils/              # Pure helper functions — no state
    colors.ts         # lerpColor, elementColor, hpColor
    random.ts         # pick, randomInt, shuffleInPlace, weightedPick

  core/               # Game-agnostic engine layer
    ECS.ts            # World, components, applyStatus, tickStatuses
    TurnManager.ts    # Phase state (player/enemy/game_over), turn number
    InteractionSystem.ts  # Rule-predicate pairs for emergent interactions
    index.ts

  rendering/          # Visual output — depends on PixiJS only
    ParallaxAsciiRenderer.ts  # Multi-layer ASCII renderer with parallax
    CameraController.ts       # Drag/inertia camera
    HUDRenderer.ts            # HUD text, HP bars, hit-flash filter
    index.ts

  game/               # Game rules — depends on core + rendering
    Game.ts                   # Thin orchestrator (~300 lines)
    MapGenerator.ts           # rot.js dungeon → ECS entities + layer data
    SpellSystem.ts            # Spell factories, geometry, resolution
    RenderSystem.ts           # drawActors(), restoreTerrain()
    TargetingController.ts    # Spell-targeting state machine
    InputHandler.ts           # Mouse / touch / keyboard event binding

  ai/                 # Enemy AI — depends on core + combat, NOT on rendering
    AISystem.ts               # processEnemyTurns, alertEnemiesNear, registry
    strategies/
      BasicStrategy.ts        # Rush + melee
      RangedStrategy.ts       # Back off + ranged shot
      BruteStrategy.ts        # Every-other-turn heavy

  combat/             # Combat math — depends on core only
    CombatSystem.ts           # resolveMelee, resolveRangedAttack, getActorAt
    index.ts

  ui/                 # Self-contained PixiJS widgets
    ActionMenu.ts     # Spell/passive selection panel
    DPad.ts           # Virtual directional pad
    TooltipOverlay.ts # Entity inspect popup
    RadialWheel.ts    # (legacy, unused)
    index.ts

  tests/              # Vitest unit tests (run with `npm test`)
    ecs.test.ts
    spellSystem.test.ts
    interactionSystem.test.ts
    combatSystem.test.ts
    aiStrategies.test.ts
    utils.test.ts

  main.ts             # Entry point (4 lines)
```

---

## Dependency rules

```
constants/  →  (nothing)
utils/      →  constants/
core/       →  constants/  utils/
combat/     →  core/
ai/         →  core/  combat/  constants/
rendering/  →  constants/  utils/  (PixiJS)
game/       →  core/  combat/  ai/  rendering/  constants/  utils/  (rot-js)
ui/         →  core/  constants/  (PixiJS)
Game.ts     →  everything above
```

**Rule: inner layers must never import from outer layers.**
- `core/` must not import from `game/`, `ai/`, `combat/`, or `ui/`
- `combat/` must not import from `ai/` or `rendering/`
- `ai/` must not import from `rendering/` or `ui/`

---

## Data flow per turn

```
[Player input]
     │
     ▼
InputHandler.onTap / onDirection / onWait
     │
     ▼
Game.handleTap / movePlayerInDirection / waitTurn
     │
     ├── TargetingController.handleTap()   (if targeting mode)
     │       └── resolveSpell()            (if cast confirmed)
     │
     ├── moveEntity()                      (if floor tap / d-pad)
     │
     └── Game.endPlayerTurn()
             │
             ├── processInteractions() for all entities   (env tick)
             │
             └── setTimeout → Game.runEnemyPhase()
                     │
                     └── AISystem.processEnemyTurns()
                             │
                             ├── STRATEGY_REGISTRY[strategy](enemyId, ctx)
                             │       ├── ctx.resolveMelee()   → CombatSystem
                             │       ├── ctx.resolveRangedAttack() → CombatSystem
                             │       └── ctx.moveEntity()     → Game.moveEntity()
                             │
                             └── tickStatuses() for all entities
                     │
                     └── drawActors()   → RenderSystem
                     └── TurnManager.endEnemyPhase()

[Each frame]
     Camera.update() → renderer.updateCamera()
     HUDRenderer.update()
     HUDRenderer.drawHealthBars()
     HUDRenderer.tickHitFlash()
```

---

## ECS overview

Entities are just integer IDs. Components are plain TypeScript objects keyed by `type` string.

```typescript
// Creating an entity with components
const id = world.createEntity();
world.addComponent(id, { type: 'position', col: 5, row: 3, layer: 'gameplay' });
world.addComponent(id, { type: 'health',   current: 30, max: 30 });

// Querying
const actors = world.query('position', 'renderable', 'faction');
```

All component types are defined and exported from `src/core/ECS.ts`.

---

## Parallax layers

| Layer ID     | Depth | Purpose                       |
|--------------|-------|-------------------------------|
| `bg_far`     | 0.2   | Far background noise chars    |
| `bg_mid`     | 0.5   | Mid-distance atmospheric fill |
| `gameplay`   | 1.0   | Walls, floors, actors, spells |
| `foreground` | 1.3   | Floating debris (close)       |

`depth < 1` → slower than camera (feels distant).
`depth > 1` → faster than camera (feels close).

---

## Multi-agent development

Each directory below can be worked on in isolation without touching others:

| Agent task           | Files to touch                          | Safe to work in parallel? |
|----------------------|-----------------------------------------|---------------------------|
| New enemy type       | `MapGenerator.ts`, `ai/strategies/`    | Yes                       |
| New spell            | `SpellSystem.ts`                        | Yes                       |
| New interaction rule | `InteractionSystem.ts`                  | Yes                       |
| New status effect    | `ECS.ts` (add to STATUS_DURATIONS), `constants/combat.ts` | Yes |
| FOV / fog of war     | `RenderSystem.ts`, `Game.ts`            | Yes                       |
| Floor progression    | `MapGenerator.ts`, `Game.ts`            | Yes                       |
| UI widget changes    | `ui/` directory                         | Yes                       |
| Balance tuning       | `constants/combat.ts` only             | Yes                       |
