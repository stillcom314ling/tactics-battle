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
import {
  World, Position, Renderable, Terrain, Health, Faction, StatusEffect,
  Movement, Combat, Abilities, Label, AI
} from '../core/ECS';
import { AsciiCell } from '../rendering/ParallaxAsciiRenderer';
import { makeFlameBolt, makeArcLightning, makeFrostShard, makePoisonCloud } from './SpellSystem';

export interface GeneratedMap {
  gameplay:   (AsciiCell | null)[][];
  bgFar:      (AsciiCell | null)[][];
  bgMid:      (AsciiCell | null)[][];
  foreground: (AsciiCell | null)[][];
  width:  number;
  height: number;
  walkable: boolean[][];
}

const FLOOR_CHARS = ['.', '·', ',', '`'];
const WALL_CHARS  = ['#', '█', '▓', '▒'];
const BG_FAR_CHARS = ['·', '.', '∙', ' ', ' ', ' ', ' '];
const BG_MID_CHARS = ['░', '·', '∘', '°', ' ', ' '];
const FG_CHARS     = ['✦', '·', '∗', ' ', ' ', ' ', ' ', ' '];

const WALL_COLORS  = [0x445566, 0x334455, 0x3a4a5a, 0x2d3d4d];
const FLOOR_COLORS = [0x556677, 0x667788, 0x5a6a7a];
const BG_FAR_COLORS = [0x1a1a2e, 0x16213e, 0x0f1626];
const BG_MID_COLORS = [0x222244, 0x1a1a3a, 0x2a2a44];
const FG_COLORS    = [0x8899aa, 0x99aabb, 0x778899];

function pick<T>(arr: T[]): T {
  return arr[Math.floor(Math.random() * arr.length)];
}

// ─── ENEMY TYPE CONFIG ───────────────────────────────────────────────────────

interface EnemyTypeConfig {
  char: string;
  name: string;
  color: number;
  hpMin: number;
  hpMax: number;
  attack: number;
  defense: number;
  moveRange: number;
  strategy: AI['strategy'];
  weight: number;       // relative spawn probability
  groupSize?: number;   // if > 1, always spawns this many together (swarmers)
}

const ENEMY_TYPES: EnemyTypeConfig[] = [
  // Goblin — basic rushdown, low HP
  { char: 'g', name: 'Goblin', color: 0xff5544, hpMin: 18, hpMax: 26,
    attack: 6, defense: 1, moveRange: 2, strategy: 'basic', weight: 3 },
  // Archer — stays at range, shoots; fragile
  { char: 'a', name: 'Archer', color: 0xffdd44, hpMin: 8, hpMax: 12,
    attack: 5, defense: 0, moveRange: 3, strategy: 'ranged', weight: 2 },
  // Brute — massive HP, moves every other turn
  { char: 'B', name: 'Brute',  color: 0xcc2211, hpMin: 60, hpMax: 75,
    attack: 13, defense: 4, moveRange: 2, strategy: 'brute', weight: 1 },
  // Swarmer — very weak, always spawns in clusters
  { char: 'z', name: 'Swarmer', color: 0xaa9988, hpMin: 8, hpMax: 14,
    attack: 3, defense: 0, moveRange: 3, strategy: 'swarm', weight: 3, groupSize: 3 },
];

// Build weighted lookup
function pickEnemyType(): EnemyTypeConfig {
  const total = ENEMY_TYPES.reduce((s, t) => s + t.weight, 0);
  let r = Math.random() * total;
  for (const t of ENEMY_TYPES) {
    r -= t.weight;
    if (r <= 0) return t;
  }
  return ENEMY_TYPES[0];
}

function spawnEnemy(world: World, pos: { col: number; row: number }, cfg: EnemyTypeConfig) {
  const hp = cfg.hpMin + Math.floor(Math.random() * (cfg.hpMax - cfg.hpMin + 1));
  const id = world.createEntity();
  world.addComponent(id, { type: 'position', col: pos.col, row: pos.row, layer: 'gameplay' } as Position);
  world.addComponent(id, { type: 'renderable', char: cfg.char, fg: cfg.color } as Renderable);
  world.addComponent(id, { type: 'health', current: hp, max: hp } as Health);
  world.addComponent(id, { type: 'faction', team: 'enemy' } as Faction);
  world.addComponent(id, { type: 'status', effects: new Map() } as StatusEffect);
  world.addComponent(id, { type: 'movement', range: cfg.moveRange, canFly: false } as Movement);
  world.addComponent(id, { type: 'combat', attackPower: cfg.attack, defense: cfg.defense, attackRange: 1 } as Combat);
  world.addComponent(id, { type: 'label', name: cfg.name } as Label);
  world.addComponent(id, {
    type: 'ai',
    strategy: cfg.strategy,
    alerted: false,
    alertRange: cfg.strategy === 'ranged' ? 7 : 5,
    moveCounter: 0,
  } as AI);
  return id;
}

// ─── MAP GENERATION ──────────────────────────────────────────────────────────

export function generateMap(width: number, height: number): GeneratedMap {
  const dungeon = new ROT.Map.Digger(width, height, {
    roomWidth: [4, 9],
    roomHeight: [3, 6],
    corridorLength: [2, 5],
    dugPercentage: 0.4,
  });

  const wallMap: boolean[][] = [];
  for (let r = 0; r < height; r++) {
    wallMap[r] = [];
    for (let c = 0; c < width; c++) wallMap[r][c] = true;
  }

  dungeon.create((x: number, y: number, wall: number) => {
    if (y >= 0 && y < height && x >= 0 && x < width) wallMap[y][x] = wall === 1;
  });

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

  const bgScale = 1.4;
  const bgW = Math.ceil(width * bgScale), bgH = Math.ceil(height * bgScale);

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

  const fgW = Math.ceil(width * 1.2), fgH = Math.ceil(height * 1.2);
  const foreground: (AsciiCell | null)[][] = [];
  for (let r = 0; r < fgH; r++) {
    foreground[r] = [];
    for (let c = 0; c < fgW; c++) {
      const ch = pick(FG_CHARS);
      foreground[r][c] = ch === ' ' ? null : { char: ch, fg: pick(FG_COLORS), alpha: 0.15 + Math.random() * 0.15 };
    }
  }

  const walkable: boolean[][] = [];
  for (let r = 0; r < height; r++) {
    walkable[r] = [];
    for (let c = 0; c < width; c++) walkable[r][c] = !wallMap[r][c];
  }

  return { gameplay, bgFar, bgMid, foreground, width, height, walkable };
}

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
      if (!isWall) floorPositions.push({ col: c, row: r });
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
  world.addComponent(playerId, { type: 'status', effects: new Map() } as StatusEffect);
  world.addComponent(playerId, { type: 'movement', range: 5, canFly: false } as Movement);
  world.addComponent(playerId, { type: 'combat', attackPower: 15, defense: 3, attackRange: 1 } as Combat);
  world.addComponent(playerId, {
    type: 'abilities',
    list: [makeFlameBolt(), makeArcLightning(), makeFrostShard(), makePoisonCloud()],
  } as Abilities);
  world.addComponent(playerId, { type: 'label', name: 'Hero' } as Label);

  // Place enemies — 9–14 total
  const targetCount = 9 + Math.floor(Math.random() * 6);
  let spawned = 0;

  while (spawned < targetCount && floorPositions.length > 0) {
    const cfg = pickEnemyType();
    const count = cfg.groupSize ?? 1;

    if (floorPositions.length < count) break;

    for (let g = 0; g < count && spawned < targetCount; g++) {
      const pos = floorPositions.pop()!;
      spawnEnemy(world, pos, cfg);
      spawned++;
    }
  }

  return { playerId, floorPositions };
}
