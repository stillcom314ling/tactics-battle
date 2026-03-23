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
  rendering/
    ParallaxAsciiRenderer.ts  # Core visual system: multi-layer ASCII with depth parallax
    CameraController.ts       # Mouse/touch pan with inertia
  game/
    Game.ts                   # Top-level orchestrator, game loop
    MapGenerator.ts           # rot.js dungeon generation → ECS entities + render layers
    RenderSystem.ts           # Syncs ECS state to parallax renderer each frame
```

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

This project is designed for iterative development with Claude Code. Good next tasks:

1. **Implement player movement** — click/tap a floor tile to move the @ there (use rot.js A* pathfinding)
2. **Add FOV** — use rot.js PreciseShadowcasting so unseen areas are dimmed
3. **Wire up turn flow** — player moves, then enemies move toward player using Dijkstra maps
4. **Add combat** — bump into enemies to attack, process interaction rules on hit
5. **Add UI overlay** — health bar, turn indicator, action buttons for mobile
6. **Add more interaction rules** — poison, freeze, chain lightning, etc.
7. **AI-assisted ASCII art** — use an LLM to generate ASCII creature/terrain designs, import as layer data
8. **Add depth variation per-character** — slightly randomize character sizes by layer for more organic depth feel

## Design Goals

- ASCII art with layered parallax depth (inspired by Effulgence RPG's spatial feel)
- Tactical turn-based combat with broad mechanics and emergent interactions
- Mobile-friendly (touch pan, touch-to-move, touch-to-attack)
- Testable on phone via GitHub Pages URL
