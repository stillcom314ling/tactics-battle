# ASCII Tactics — Design Document

> Living document. Update this when core rules change. This is the source of truth for
> what we're building and why.

---

## Vision

A spell-centric tactical combat game rendered in ASCII with multi-layer parallax depth.
The aesthetic is the hook — ASCII characters floating at different depths create a 3D
feel. The gameplay is inspired by **Rift Wizard**: everything interesting is in the spells,
elements interact emergently, and each run feels different based on what you bring.

Mobile is the primary platform. Everything is designed to be played one-handed on a phone.

---

## Core Design Rules

These are non-negotiable. Every feature should be checked against these.

### 1. No Bump-to-Attack
Every action is **explicit**. The player never accidentally does something by walking
into it. Combat, spells, and interactions are all initiated through the action wheel.
Walking into an enemy just blocks movement.

### 2. Single Map, Everything Visible
One map at a time. Everything important is on screen. No fog of war hiding enemies
(for now). No scrolling overworld. You can pan the camera but it's for aesthetics and
orientation, not to reveal hidden content. If you're fighting, you can see the whole
battlefield.

### 3. Mobile First
- All tap targets are thumb-sized (minimum ~48px)
- No hover states — everything is tap-activated
- The radial wheel is the primary input, not a keyboard
- Test on phone via GitHub Pages before considering anything "done"
- Portrait and landscape should both work

### 4. Everything Is In The Spell
Combat damage, area of effect, status effects, element type, range — it all lives in
the spell definition. Enemies are interesting because of what spells they carry, not
because of raw stats. Adding a new spell should be as simple as writing a data object.

### 5. Parallax Always
The 4 parallax layers should always be used meaningfully:
- `bg_far` (depth 0.2) — distant atmospheric elements, barely moves
- `bg_mid` (depth 0.5) — environmental mid-ground
- `gameplay` (depth 1.0) — all entities and terrain
- `foreground` (depth 1.3) — spell effects, particles, close overlays

Spell effects, status indicators, and UI overlays belong on `foreground` or a dedicated
UI layer (depth 0), not baked into the gameplay layer.

---

## Combat Philosophy

### Spells Over Melee
There is no basic attack. Every offensive action costs a spell slot / cooldown. This
forces resource management and makes every engagement feel intentional.

### Elements + Synergies
Spells apply **elements** and **status effects**. The environment and other spells can
react to these. Synergies are the core "aha" moments:
- Wet + Lightning → shocked (double damage, stun)
- Burning + Flammable terrain → fire spreads
- Burning entity + Wet tile → extinguished
- More TBD — define new ones in `src/core/InteractionSystem.ts`

### Cooldowns, No Mana
Each spell has its own cooldown (in turns). No global mana pool. This means you can
always do *something*, but your best spells are rationed. Encourages spell variety.

### Turn Order
Player → Enemies → Environment (status effect ticks)
Enemies get a speed value; faster enemies act more often (rot.js scheduler).

---

## Action Wheel (Radial UI)

The primary input method. A nested radial wheel pops up when the player taps their
character or a floating action button.

### Structure
- **Root wheel**: Move | Spells | Wait | (future: Items, Inspect...)
- **Spells sub-wheel**: categories (Fire, Lightning, Ice, Arcane...)
- **Category sub-wheel**: specific spells from that element
- Each level animates in from the same center point
- **Back** segment always present to return to parent
- **Root** tap resets all the way to top
- Cooldown indicators shown on spell nodes (grayed if unavailable)
- Path breadcrumbs show where you are in the tree

### Scaling
The wheel system is designed to hold dozens of spells eventually. Adding a new spell
only requires adding it to the player's `Abilities` component — the wheel builds itself.

---

## Spell Definition

Each spell is an `Ability` object in the ECS `Abilities` component:

```typescript
{
  id: 'flame_bolt',
  name: 'Flame Bolt',
  element: 'fire',
  range: 5,
  pattern: 'single',   // 'single' | 'line' | 'aoe' | 'self'
  aoeRadius: undefined,
  damage: 8,
  effects: ['burning'],
  cooldownMax: 2,
  cooldownCurrent: 0,
}
```

To add a new spell: define the object, add it to the entity's `Abilities.list`.
No other wiring required — the wheel and spell system pick it up automatically.

---

## Element Reference

| Element   | Status Effect | Synergy Notes |
|-----------|--------------|---------------|
| Fire      | burning      | Spreads to flammable terrain. Extinguished by wet. |
| Lightning | shocked      | Wet targets take double damage + stun. |
| Ice       | slowed, wet  | Slowed targets skip every other turn. |
| Poison    | poisoned     | Deals damage each environment turn. |
| Arcane    | —            | Pure damage, no synergies. Reliable fallback. |

---

## ASCII Aesthetic Guidelines

- Use bright colors for spell effects on the foreground layer
- Status effects change entity color (burning = orange/red, frozen = blue/white)
- Different ASCII chars carry different visual weight — use `*`, `#`, `~`, `^` for effects
- Enemy ASCII chars should hint at type: `D` = dragon, `s` = snake, `T` = troll, etc.
- Consider per-character size variation on bg layers for organic depth feel

---

## Next Features (Backlog)

These are ordered roughly by priority for getting to a testable combat loop:

1. **Basic spell targeting** — tap spell → highlight tiles → tap to cast
2. **Flame Bolt, Arc Lightning, Frost Shard** — first 3 spells
3. **Enemy spell AI** — enemies cast spells when in range
4. **Status effect visuals** — burning = red tint, shocked = yellow flash
5. **FOV / line of sight** — can only target what you can see (rot.js PreciseShadowcasting)
6. **More spells** — describe a spell in plain language, AI implements it
7. **Loot drops** — enemies drop spell scrolls that add spells to wheel
8. **Multiple map rooms** — doorways that transition to a new generated room
9. **Boss enemies** — bigger ASCII art, multiple abilities, unique synergies
10. **Sound** — ASCII-appropriate bleeps and bloops via Web Audio API

---

## File Structure

```
src/
  main.ts
  core/
    ECS.ts                   # Entity-Component System — add new components here
    TurnManager.ts           # rot.js scheduler wrapper — turn flow
    InteractionSystem.ts     # Synergy rules — add new interactions here
  rendering/
    ParallaxAsciiRenderer.ts # Multi-layer ASCII renderer
    CameraController.ts      # Pan + inertia
  game/
    Game.ts                  # Orchestrator, game loop
    MapGenerator.ts          # Map + entity creation
    RenderSystem.ts          # ECS → renderer sync
    SpellSystem.ts           # Spell resolution pipeline
  ui/
    RadialWheel.ts           # Nested radial action wheel (PIXI overlay)
```
