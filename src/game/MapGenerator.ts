/**
 * MAP GENERATOR
 *
 * Uses rot.js Digger to generate dungeon maps, then populates the ECS world
 * and the parallax renderer layers.
 *
 * Background layers get procedural atmospheric ASCII (dots, noise chars).
 * Gameplay layer gets the actual dungeon walls and floors.
 * Foreground layer gets occasional floating particles / debris.
 */

import * as ROT from 'rot-js';
import {
  World, Position, Renderable, Terrain, Health, Faction, StatusEffect,
  Movement, Combat, Abilities, Passives, Label, AI,
} from '../core/ECS';
import { AsciiCell } from '../rendering/ParallaxAsciiRenderer';
import {
  makeDefaultPlayerAbilities,
  makeMarioSet, makeMarioPassives,
  makeFrostDKSet, makeFrostDKPassives,
} from './SpellSystem';

/**
 * Which ability set the player starts with.
 * 'default'  — original four starter spells (flame bolt, arc lightning, etc.)
 * 'mario'    — The Plumber: Ground Pound, Fire Flower, Star Power, Super Jump
 * 'frost_dk' — The Frost Death Knight: Obliterate, Frost Strike, Howling Blast, Pillar of Frost
 */
export const PLAYER_SET: 'default' | 'mario' | 'frost_dk' = 'frost_dk';
import {
  MAP_W, MAP_H, DUNGEON_OPTIONS,
  FLOOR_CHARS, WALL_CHARS, BG_FAR_CHARS, BG_MID_CHARS, FG_CHARS,
  ENEMY_COUNT_MIN, ENEMY_COUNT_VARIANCE,
} from '../constants/map';
import {
  WALL_COLORS, FLOOR_COLORS, BG_FAR_COLORS, BG_MID_COLORS, FG_COLORS,
  BG_LAYER_SCALE, FG_LAYER_SCALE,
} from '../constants/rendering';
import {
  PLAYER_HP_MAX, PLAYER_MOVE_RANGE, PLAYER_ATTACK_POWER, PLAYER_DEFENSE, PLAYER_ATTACK_RANGE,
} from '../constants/combat';
import { pick, weightedPick, shuffleInPlace, randomInt } from '../utils/random';
import { ENEMY_TYPES, EnemyTypeConfig } from '../constants/enemies';

export interface GeneratedMap {
  gameplay:   (AsciiCell | null)[][];
  bgFar:      (AsciiCell | null)[][];
  bgMid:      (AsciiCell | null)[][];
  foreground: (AsciiCell | null)[][];
  width:      number;
  height:     number;
  walkable:   boolean[][];
}

function spawnEnemy(world: World, pos: { col: number; row: number }, cfg: EnemyTypeConfig) {
  const hp = randomInt(cfg.hpMin, cfg.hpMax);
  const id = world.createEntity();
  world.addComponent(id, { type: 'position',  col: pos.col, row: pos.row, layer: 'gameplay' } as Position);
  world.addComponent(id, { type: 'renderable', char: cfg.char, fg: cfg.color } as Renderable);
  world.addComponent(id, { type: 'health',    current: hp, max: hp } as Health);
  world.addComponent(id, { type: 'faction',   team: 'enemy' } as Faction);
  world.addComponent(id, { type: 'status',    effects: new Map() } as StatusEffect);
  world.addComponent(id, { type: 'movement',  range: cfg.moveRange, canFly: false } as Movement);
  world.addComponent(id, { type: 'combat',    attackPower: cfg.attack, defense: cfg.defense, attackRange: 1 } as Combat);
  world.addComponent(id, { type: 'label',     name: cfg.name } as Label);
  world.addComponent(id, {
    type:        'ai',
    strategy:    cfg.strategy,
    alerted:     false,
    alertRange:  cfg.strategy === 'ranged' ? 7 : 5,
    moveCounter: 0,
  } as AI);
  return id;
}

// ─── MAP GENERATION ───────────────────────────────────────────────────────────

export function generateMap(width: number = MAP_W, height: number = MAP_H): GeneratedMap {
  const dungeon = new ROT.Map.Digger(width, height, DUNGEON_OPTIONS);

  const wallMap: boolean[][] = Array.from({ length: height }, () => new Array(width).fill(true));

  dungeon.create((x: number, y: number, wall: number) => {
    if (y >= 0 && y < height && x >= 0 && x < width) wallMap[y][x] = wall === 1;
  });

  // Gameplay layer
  const gameplay: (AsciiCell | null)[][] = wallMap.map(row =>
    row.map(isWall => isWall
      ? { char: pick(WALL_CHARS),  fg: pick(WALL_COLORS),  alpha: 0.9 }
      : { char: pick(FLOOR_CHARS), fg: pick(FLOOR_COLORS), alpha: 0.7 },
    )
  );

  // Background layers (larger than gameplay for parallax coverage)
  const bgW = Math.ceil(width  * BG_LAYER_SCALE);
  const bgH = Math.ceil(height * BG_LAYER_SCALE);

  const bgFar: (AsciiCell | null)[][] = Array.from({ length: bgH }, () =>
    Array.from({ length: bgW }, () => {
      const ch = pick(BG_FAR_CHARS);
      return ch === ' ' ? null : { char: ch, fg: pick(BG_FAR_COLORS), alpha: 0.3 + Math.random() * 0.2 };
    })
  );

  const bgMid: (AsciiCell | null)[][] = Array.from({ length: bgH }, () =>
    Array.from({ length: bgW }, () => {
      const ch = pick(BG_MID_CHARS);
      return ch === ' ' ? null : { char: ch, fg: pick(BG_MID_COLORS), alpha: 0.2 + Math.random() * 0.3 };
    })
  );

  // Foreground layer
  const fgW = Math.ceil(width  * FG_LAYER_SCALE);
  const fgH = Math.ceil(height * FG_LAYER_SCALE);
  const foreground: (AsciiCell | null)[][] = Array.from({ length: fgH }, () =>
    Array.from({ length: fgW }, () => {
      const ch = pick(FG_CHARS);
      return ch === ' ' ? null : { char: ch, fg: pick(FG_COLORS), alpha: 0.15 + Math.random() * 0.15 };
    })
  );

  const walkable: boolean[][] = wallMap.map(row => row.map(isWall => !isWall));

  return { gameplay, bgFar, bgMid, foreground, width, height, walkable };
}

// ─── WORLD POPULATION ────────────────────────────────────────────────────────

/**
 * Creates terrain, player, and enemy entities from a generated map.
 * Returns the player's EntityId and the remaining floor positions.
 */
export function populateWorld(
  world: World,
  map: GeneratedMap,
): { playerId: number; floorPositions: { col: number; row: number }[] } {
  const floorPositions: { col: number; row: number }[] = [];

  // Terrain entities
  for (let r = 0; r < map.height; r++) {
    for (let c = 0; c < map.width; c++) {
      const cell = map.gameplay[r][c];
      if (!cell) continue;
      const isWall = (WALL_CHARS as readonly string[]).includes(cell.char);
      const id = world.createEntity();
      world.addComponent(id, { type: 'position', col: c, row: r, layer: 'gameplay' } as Position);
      world.addComponent(id, { type: 'renderable', char: cell.char, fg: cell.fg, alpha: cell.alpha } as Renderable);
      world.addComponent(id, {
        type:        'terrain',
        walkable:    !isWall,
        transparent: !isWall,
        properties:  new Set(isWall ? [] : ['flammable']),
      } as Terrain);
      if (!isWall) floorPositions.push({ col: c, row: r });
    }
  }

  shuffleInPlace(floorPositions);

  // Player entity
  const playerPos = floorPositions.pop()!;
  const playerId  = world.createEntity();
  world.addComponent(playerId, { type: 'position',  col: playerPos.col, row: playerPos.row, layer: 'gameplay' } as Position);
  world.addComponent(playerId, { type: 'renderable', char: '@', fg: 0x44eeff } as Renderable);
  world.addComponent(playerId, { type: 'health',    current: PLAYER_HP_MAX, max: PLAYER_HP_MAX } as Health);
  world.addComponent(playerId, { type: 'faction',   team: 'player' } as Faction);
  world.addComponent(playerId, { type: 'status',    effects: new Map() } as StatusEffect);
  world.addComponent(playerId, { type: 'movement',  range: PLAYER_MOVE_RANGE, canFly: false } as Movement);
  world.addComponent(playerId, {
    type: 'combat',
    attackPower:  PLAYER_ATTACK_POWER,
    defense:      PLAYER_DEFENSE,
    attackRange:  PLAYER_ATTACK_RANGE,
  } as Combat);
  const abilityList =
    PLAYER_SET === 'mario'    ? makeMarioSet()    :
    PLAYER_SET === 'frost_dk' ? makeFrostDKSet()  :
    makeDefaultPlayerAbilities();

  world.addComponent(playerId, { type: 'abilities', list: abilityList } as Abilities);

  const passiveList =
    PLAYER_SET === 'mario'    ? makeMarioPassives()    :
    PLAYER_SET === 'frost_dk' ? makeFrostDKPassives()  :
    [];

  if (passiveList.length > 0) {
    world.addComponent(playerId, { type: 'passives', list: passiveList } as Passives);
  }

  world.addComponent(playerId, { type: 'label', name: 'Hero' } as Label);

  // Enemy entities — ENEMY_COUNT_MIN + rand(0, ENEMY_COUNT_VARIANCE)
  const targetCount = ENEMY_COUNT_MIN + randomInt(0, ENEMY_COUNT_VARIANCE - 1);
  let spawned = 0;

  while (spawned < targetCount && floorPositions.length > 0) {
    const cfg   = weightedPick(ENEMY_TYPES);
    const count = cfg.groupSize ?? 1;
    if (floorPositions.length < count) break;
    for (let g = 0; g < count && spawned < targetCount; g++) {
      spawnEnemy(world, floorPositions.pop()!, cfg);
      spawned++;
    }
  }

  return { playerId, floorPositions };
}
