/**
 * MAP GENERATOR
 * 
 * Uses rot.js to generate dungeon maps, then populates the ECS world
 * and the parallax renderer layers.
 * 
 * Background layers get procedural atmospheric ASCII (dots, noise chars).
 * Gameplay layer gets the actual dungeon.
 * Foreground layer gets occasional floating particles/debris.
 */

import * as ROT from 'rot-js';
import { World, Position, Renderable, Terrain, Health, Faction, StatusEffect, Movement, Combat, Abilities } from '../core/ECS';
import { AsciiCell } from '../rendering/ParallaxAsciiRenderer';
import { makeFlameBolt, makeArcLightning, makeFrostShard } from './SpellSystem';

export interface GeneratedMap {
  /** Gameplay layer cell data */
  gameplay: (AsciiCell | null)[][];
  /** Background layer (far) */
  bgFar: (AsciiCell | null)[][];
  /** Background layer (mid) */
  bgMid: (AsciiCell | null)[][];
  /** Foreground layer */
  foreground: (AsciiCell | null)[][];
  /** Width in tiles */
  width: number;
  /** Height in tiles */
  height: number;
  /** walkable[row][col] — true if a unit can move there */
  walkable: boolean[][];
}

const FLOOR_CHARS = ['.', '·', ',', '`'];
const WALL_CHARS = ['#', '█', '▓', '▒'];
const BG_FAR_CHARS = ['·', '.', '∙', ' ', ' ', ' ', ' '];
const BG_MID_CHARS = ['░', '·', '∘', '°', ' ', ' '];
const FG_CHARS = ['✦', '·', '∗', ' ', ' ', ' ', ' ', ' '];

const WALL_COLORS = [0x445566, 0x334455, 0x3a4a5a, 0x2d3d4d];
const FLOOR_COLORS = [0x556677, 0x667788, 0x5a6a7a];
const BG_FAR_COLORS = [0x1a1a2e, 0x16213e, 0x0f1626];
const BG_MID_COLORS = [0x222244, 0x1a1a3a, 0x2a2a44];
const FG_COLORS = [0x8899aa, 0x99aabb, 0x778899];

function pick<T>(arr: T[]): T {
  return arr[Math.floor(Math.random() * arr.length)];
}

export function generateMap(width: number, height: number): GeneratedMap {
  // Generate dungeon layout with rot.js
  const dungeon = new ROT.Map.Digger(width, height, {
    roomWidth: [4, 9],
    roomHeight: [3, 6],
    corridorLength: [2, 5],
    dugPercentage: 0.4,
  });

  const wallMap: boolean[][] = [];
  for (let r = 0; r < height; r++) {
    wallMap[r] = [];
    for (let c = 0; c < width; c++) {
      wallMap[r][c] = true; // default to wall
    }
  }

  dungeon.create((x: number, y: number, wall: number) => {
    if (y >= 0 && y < height && x >= 0 && x < width) {
      wallMap[y][x] = wall === 1;
    }
  });

  // Build gameplay layer
  const gameplay: (AsciiCell | null)[][] = [];
  for (let r = 0; r < height; r++) {
    gameplay[r] = [];
    for (let c = 0; c < width; c++) {
      if (wallMap[r][c]) {
        gameplay[r][c] = { char: pick(WALL_CHARS), fg: pick(WALL_COLORS), alpha: 0.9 };
      } else {
        gameplay[r][c] = { char: pick(FLOOR_CHARS), fg: pick(FLOOR_COLORS), alpha: 0.7 };
      }
    }
  }

  // Build background layers (larger than gameplay for parallax overflow)
  const bgW = Math.ceil(width * 1.4);
  const bgH = Math.ceil(height * 1.4);

  const bgFar: (AsciiCell | null)[][] = [];
  for (let r = 0; r < bgH; r++) {
    bgFar[r] = [];
    for (let c = 0; c < bgW; c++) {
      const ch = pick(BG_FAR_CHARS);
      bgFar[r][c] = ch === ' ' ? null : { char: ch, fg: pick(BG_FAR_COLORS), alpha: 0.3 + Math.random() * 0.2 };
    }
  }

  const bgMid: (AsciiCell | null)[][] = [];
  for (let r = 0; r < bgH; r++) {
    bgMid[r] = [];
    for (let c = 0; c < bgW; c++) {
      const ch = pick(BG_MID_CHARS);
      bgMid[r][c] = ch === ' ' ? null : { char: ch, fg: pick(BG_MID_COLORS), alpha: 0.2 + Math.random() * 0.3 };
    }
  }

  // Build foreground layer (sparse)
  const fgW = Math.ceil(width * 1.2);
  const fgH = Math.ceil(height * 1.2);

  const foreground: (AsciiCell | null)[][] = [];
  for (let r = 0; r < fgH; r++) {
    foreground[r] = [];
    for (let c = 0; c < fgW; c++) {
      const ch = pick(FG_CHARS);
      foreground[r][c] = ch === ' ' ? null : { char: ch, fg: pick(FG_COLORS), alpha: 0.15 + Math.random() * 0.15 };
    }
  }

  // Build walkable grid (true = passable)
  const walkable: boolean[][] = [];
  for (let r = 0; r < height; r++) {
    walkable[r] = [];
    for (let c = 0; c < width; c++) {
      walkable[r][c] = !wallMap[r][c];
    }
  }

  return { gameplay, bgFar, bgMid, foreground, width, height, walkable };
}

/**
 * Populate the ECS world with entities from the generated map.
 * Returns the player entity ID and a list of free floor positions.
 */
export function populateWorld(
  world: World,
  map: GeneratedMap
): { playerId: number; floorPositions: { col: number; row: number }[] } {
  const floorPositions: { col: number; row: number }[] = [];

  // Create terrain entities
  for (let r = 0; r < map.height; r++) {
    for (let c = 0; c < map.width; c++) {
      const cell = map.gameplay[r][c];
      if (!cell) continue;

      const isWall = WALL_CHARS.includes(cell.char);
      const id = world.createEntity();

      world.addComponent(id, { type: 'position', col: c, row: r, layer: 'gameplay' } as Position);
      world.addComponent(id, { type: 'renderable', char: cell.char, fg: cell.fg, alpha: cell.alpha } as Renderable);
      world.addComponent(id, {
        type: 'terrain',
        walkable: !isWall,
        transparent: !isWall,
        properties: new Set(isWall ? [] : ['flammable']),
      } as Terrain);

      if (!isWall) {
        floorPositions.push({ col: c, row: r });
      }
    }
  }

  // Shuffle floor positions
  for (let i = floorPositions.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [floorPositions[i], floorPositions[j]] = [floorPositions[j], floorPositions[i]];
  }

  // Place player
  const playerPos = floorPositions.pop()!;
  const playerId = world.createEntity();
  world.addComponent(playerId, { type: 'position', col: playerPos.col, row: playerPos.row, layer: 'gameplay' } as Position);
  world.addComponent(playerId, { type: 'renderable', char: '@', fg: 0x44eeff } as Renderable);
  world.addComponent(playerId, { type: 'health', current: 100, max: 100 } as Health);
  world.addComponent(playerId, { type: 'faction', team: 'player' } as Faction);
  world.addComponent(playerId, { type: 'status', effects: new Set() } as StatusEffect);
  world.addComponent(playerId, { type: 'movement', range: 5, canFly: false } as Movement);
  world.addComponent(playerId, { type: 'combat', attackPower: 15, defense: 5, attackRange: 1 } as Combat);
  world.addComponent(playerId, { type: 'abilities', list: [makeFlameBolt(), makeArcLightning(), makeFrostShard()] } as Abilities);

  // Place some enemies
  const enemyCount = 4 + Math.floor(Math.random() * 4);
  for (let i = 0; i < enemyCount && floorPositions.length > 0; i++) {
    const pos = floorPositions.pop()!;
    const enemyId = world.createEntity();
    const enemyChar = pick(['g', 'o', 'k', 's', 'r']);
    const enemyColor = pick([0xff4444, 0xff6644, 0xee5533, 0xcc3322]);

    world.addComponent(enemyId, { type: 'position', col: pos.col, row: pos.row, layer: 'gameplay' } as Position);
    world.addComponent(enemyId, { type: 'renderable', char: enemyChar, fg: enemyColor } as Renderable);
    world.addComponent(enemyId, { type: 'health', current: 30 + Math.random() * 20, max: 50 } as Health);
    world.addComponent(enemyId, { type: 'faction', team: 'enemy' } as Faction);
    world.addComponent(enemyId, { type: 'status', effects: new Set() } as StatusEffect);
    world.addComponent(enemyId, { type: 'movement', range: 3, canFly: false } as Movement);
    world.addComponent(enemyId, { type: 'combat', attackPower: 8, defense: 3, attackRange: 1 } as Combat);
  }

  return { playerId, floorPositions };
}
