/**
 * RENDER SYSTEM
 * 
 * Reads all entities with Position + Renderable components
 * and draws them onto the parallax renderer's gameplay layer.
 * 
 * This runs every frame to keep visuals in sync with game state.
 */

import { World, Position, Renderable } from '../core/ECS';
import { ParallaxAsciiRenderer } from '../rendering/ParallaxAsciiRenderer';

export function renderEntities(world: World, renderer: ParallaxAsciiRenderer) {
  const entities = world.query('position', 'renderable');

  for (const id of entities) {
    const pos = world.getComponent<Position>(id, 'position')!;
    const rend = world.getComponent<Renderable>(id, 'renderable')!;

    renderer.setCell(pos.layer, pos.col, pos.row, {
      char: rend.char,
      fg: rend.fg,
      bg: rend.bg,
      alpha: rend.alpha,
    });
  }
}
