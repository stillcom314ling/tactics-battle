# ASCII Tactics

A tactical ASCII combat game with parallax depth rendering. Pan the camera and ASCII layers shift at different speeds, creating a 3D depth effect.

## Tech Stack

- **TypeScript** + **Vite** (bundler, dev server)
- **PixiJS 8** (rendering engine — draws thousands of ASCII text objects fast)
- **rot-js** (roguelike toolkit — map generation, pathfinding, FOV, turn scheduling)
- **Custom ECS** (lightweight entity-component system in `src/core/ECS.ts`)

## Architecture

```
src/
  main.ts                     # Entry point
  core/
    ECS.ts                    # Entity-Component System (World, components)
    TurnManager.ts            # Turn-based flow using rot.js scheduler
    InteractionSystem.ts      # Emergent mechanic rules (fire+oil, wet+lightning, etc)
    index.ts                  # Re-exports
  rendering/
    ParallaxAsciiRenderer.ts  # Core visual system: multi-layer ASCII with depth parallax
    CameraController.ts       # Mouse/touch pan with inertia
    index.ts                  # Re-exports
  game/
    Game.ts                   # Top-level orchestrator, game loop, input handling
    MapGenerator.ts           # rot.js dungeon generation → ECS entities + render layers
    SpellSystem.ts            # Spell definitions, resolution, range geometry, cooldowns
    RenderSystem.ts           # Syncs ECS state to parallax renderer each frame
  ui/
    ActionMenu.ts             # Tap-to-open scrollable panel: Actives + Passives sections
    DPad.ts                   # Virtual 8-directional movement pad + Wait center button
    RadialWheel.ts            # (legacy) Drag-to-select action wheel — superseded by ActionMenu
    TooltipOverlay.ts         # Tap-to-inspect panel: name, HP bar, status effects
```

## Current State (what is already built)

### Core loop — fully playable
- **Movement** — tap any floor tile (A* pathfinding) OR use the D-Pad for step-by-step 8-directional movement
- **Turn flow** — player acts → enemies move/attack → environment ticks → repeat
- **Enemy AI** — enemies pathfind toward player; melee strike when adjacent
- **Death** — player HP reaches 0 → game_over phase, refresh to restart

### Controls

#### Touch / Mouse
| Action | Gesture |
|---|---|
| Move to tile | Tap any walkable floor tile |
| Move one step | D-Pad arrow buttons (bottom-left corner) |
| Wait a turn | D-Pad center `…` button |
| Open spell menu | Tap the `⚡ ACTIONS` FAB (bottom-right) |
| Cast spell | Select spell in menu → tap target tile |
| Cast (aoe/line) | Select spell → tap target → tap same tile again to confirm |
| Cancel targeting | Tap own tile (`@`) while in targeting mode |
| Inspect entity | Tap any actor; tooltip shows name, HP bar, status effects |
| Pan camera | Drag (with inertia); parallax layers shift at different speeds |

#### Keyboard (desktop)
| Key | Action |
|---|---|
| Arrow keys / WASD / numpad | Move one step |
| `.` or `5` | Wait a turn |

### Game modes (internal)
- `free` — normal play; tap tiles to move or inspect
- `targeting` — spell selected; range tiles highlighted; tap to aim, tap again if aoe/line to confirm

### Spell system
Four starter spells on a charge/cooldown system:
| Spell | Element | Pattern | Range | Damage | Effects |
|---|---|---|---|---|---|
| Flame Bolt | fire | single | 5 | 10 | burning |
| Arc Lightning | lightning | line | 5 | 7 | shocked |
| Frost Shard | ice | single | 4 | 6 | wet, slowed |
| Poison Cloud | poison | aoe (r=2) | 4 | 4 | poisoned |

Patterns: `single`, `line`, `aoe`, `self` — adding new spells is one object literal in `SpellSystem.ts`.
Charges refill each full turn. `InteractionSystem.ts` handles synergy rules (wet+lightning = bonus damage, etc.).

### UI
- **Action Menu** (`ActionMenu.ts`) — tap `⚡ ACTIONS` FAB to open a scrollable panel; ACTIVES section lists available spells with element icon, name, tooltip, and charges badge; PASSIVES section is a placeholder checklist; drag to scroll; tap outside to close
- **D-Pad** (`DPad.ts`) — 3×3 grid of 8 directional arrows + center Wait button; positioned bottom-left; fires `onDirection(dx,dy)` or `onWait()`
- **Targeting mode overlay** — spell selected → in-range floor tiles highlight in element colour; enemy glyphs tint to spell colour; mode hint shows spell name and instructions
- **Two-tap confirm** — aoe and line spells show blast preview (`*`) on first tap; second tap on same tile casts; tap own tile (`@`) cancels
- **Tooltip overlay** — tap any entity to inspect: name, animated HP bar (green→yellow→red), active status effects in their element colour
- **HUD** — HP, turn counter, enemy count, phase indicator, log of last 3 messages
- **Portrait-aware** — font sizes and FAB scaled for mobile; mode hint wraps to screen width

### ECS Components in use
`position`, `renderable`, `health`, `faction`, `status`, `movement`, `combat`, `abilities`, `terrain`, `label`, `ai`

---

## What to build next — design discussion

These are the most impactful directions. Discuss with a new chat to choose focus.

### 1. FOV / Fog of War
**What:** Use `rot.js PreciseShadowcasting` to track which tiles the player can see. Unseen tiles are hidden or dimmed; previously-seen tiles are shown at reduced brightness ("memory").
**Why:** Immediately makes exploration tense and meaningful. Enemies feel threatening once hidden.
**How:** Add a `fov: Set<string>` to game state, recompute each turn from player position. In `RenderSystem`/`drawActors`, skip or dim entities outside FOV. Dim gameplay layer cells outside FOV using `setCell` alpha override.
**Complexity:** Medium — rot.js does the hard part, rendering wiring is straightforward.

### 2. More enemy variety
**What:** Different enemy types with distinct stats and behaviours:
- *Archer* (`a`) — stays at range 3, shoots in a line (reuse line spell pattern)
- *Brute* (`B`) — high HP/attack, slow (1 step per 2 turns)
- *Mage* (`m`) — casts AoE fire at player position from distance
- *Swarmer* (`z`) — weak solo, spawns in groups of 3–4, fast

**Why:** Single enemy type gets boring fast. Variety forces spell choices.
**How:** Add optional `ai` component with `strategy: 'ranged'|'brute'|'swarm'|'caster'`. Branch in `processEnemyTurns` on strategy.
**Complexity:** Low–Medium per type.

### 3. Items on the ground
**What:** Potions, scrolls, and gear dropped on floor tiles. Player steps on tile to pick up; items go to a simple inventory (no UI needed — just apply effect immediately or queue it).
**Starter items:**
- Health Potion — restore 25 HP
- Scroll of Fireball — one-use AoE 8 damage, range 5
- Boots of Speed — +1 movement range permanently

**Why:** Gives a reason to explore; creates risk/reward (go get the potion or play it safe).
**How:** Add `item` component with `{ kind, effect }`. In `moveEntity`, check if destination tile has an item entity and auto-collect. Add to wheel as "Use Item" submenu.
**Complexity:** Low.

### 4. Win condition + dungeon progression
**What:** Kill all enemies on the floor → "FLOOR CLEAR" message → staircase tile (`>`) appears → step on it to descend to a harder generated floor.
**Progression hooks:** each floor scales enemy count, max HP, and attack. Player keeps HP and abilities between floors.
**Why:** Without a goal the game has no arc.
**How:** Track kill count, check against starting enemy count. `generateMap` already returns a new map; reset enemies, re-place player, keep components.
**Complexity:** Low–Medium.

### 5. Spell crafting / unlock tree
**What:** Killing enemies drops "essence" of their element. Collect enough → unlock new spells or upgrade existing ones (e.g. Flame Bolt → Inferno: AoE pattern, higher damage).
**Upgrade ideas:**
- Range +1, damage +3, add second effect, reduce cooldown by 1
- New spells: Chain Lightning (bounces 2 times), Ice Nova (AoE slowed + wet), Void Bolt (arcane, silences)

**Why:** The spell system is already clean and extensible — crafting gives it long-term depth without new systems.
**How:** Add `essence` resource to player. Show essence count in HUD. Add "Upgrade" submenu to ActionMenu ACTIVES section. Mutations are just property changes on `Ability` objects.
**Complexity:** Medium.

### 6. Spell/hit visual feedback
**What:** When a spell fires, flash the affected tiles with the element colour for 2–3 frames. Enemies briefly flash white on hit. Screen nudge (tiny shake) on taking damage.
**Why:** Currently combat is silent and instant — feedback makes hits feel impactful.
**How:** Add a `VFXQueue` of `{ tiles, color, framesLeft }` entries. Process queue in the game loop, calling `setCell` with tinted alpha; restore terrain when framesLeft hits 0.
**Complexity:** Low.

### 7. Persistent high score / floor tracking
**What:** On game over, save `{ floor, kills, turnsAlive }` to `localStorage`. Show on death screen.
**Why:** Tiny feature, big motivation to replay.
**Complexity:** Very low.

---

## Design Goals

## Key Concepts

### Parallax Depth (the main visual trick)
The renderer has multiple layers, each with a `depth` factor:
- `depth: 0.2` — far background, barely moves when camera pans
- `depth: 0.5` — mid background, moves at half speed  
- `depth: 1.0` — gameplay layer, moves 1:1 with camera
- `depth: 1.3` — foreground, moves FASTER than camera (feels close)

This creates a convincing 3D depth effect with just flat ASCII text.

### ECS (Entity-Component System)
Entities are just numeric IDs. Components are plain typed objects. Query entities by component type. This enables emergent interactions:
- An entity with `StatusEffect.burning` + standing on `Terrain.flammable` = fire spreads
- An entity with `StatusEffect.wet` + hit by lightning = double damage + stun

Add new interaction rules in `src/core/InteractionSystem.ts`.

### Turn-Based Combat
Uses rot.js Speed scheduler. Each entity has a speed value. Phases cycle: player → enemy → environment. The turn manager is in `src/core/TurnManager.ts`.

## Commands

```bash
npm install          # Install dependencies
npm run dev          # Start dev server (http://localhost:3000)
npm run build        # Build for production → dist/
npm run preview      # Preview production build locally
```

## Deployment

Push to `main` → GitHub Actions builds and deploys to GitHub Pages automatically.
To enable: go to repo Settings → Pages → Source: GitHub Actions.

Test on your phone by opening the GitHub Pages URL.

## Development with Claude Code

This project is designed for iterative development with Claude Code. See the **What to build next** section above for detailed design notes on each option. Quick priority order:

1. **FOV / fog of war** — biggest gameplay impact, rot.js does the heavy lifting
2. **Enemy variety** — archers, brutes, mages; branch on `ai.strategy` in `processEnemyTurns`
3. **Items on the ground** — potions/scrolls, auto-collect on step, instant effect
4. **Win condition + floor descent** — kill all enemies, staircase spawns, scale next floor
5. **Spell VFX** — tile flash + enemy hit flash via a lightweight `VFXQueue` in the game loop
6. **Spell upgrades** — essence drops from kills, spend to mutate `Ability` objects in ActionMenu
7. **High score in localStorage** — floor/kills/turns on death screen, trivial to add

### Key implementation notes
- **Adding a spell:** one object literal returned from a `makeXxx(): Ability` factory in `SpellSystem.ts`; wire it into `setupPlayer` in `Game.ts`
- **Two-tap targeting:** `aoe` and `line` patterns require `pendingTargetCol/Row` to be set on first tap; `clearSpellPreview` clears ONLY visual tiles — `pendingTargetCol/Row` is reset only in `clearRangeHighlight` (cast or cancel)
- **ActionMenu ACTIVES:** rebuilt each time `actionMenu.open()` is called; pass updated `ActiveItem[]` from player's `abilities` component
- **D-Pad position:** `dpad.resize(x, y)` called in the game loop resize handler; positioned bottom-left corner

## Design Goals

- ASCII art with layered parallax depth (inspired by Effulgence RPG's spatial feel)
- Tactical turn-based combat with broad mechanics and emergent interactions
- Mobile-friendly (touch pan, touch-to-move, touch-to-attack)
- Testable on phone via GitHub Pages URL
