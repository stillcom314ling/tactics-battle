/**
 * ENEMY TYPE DEFINITIONS
 *
 * Data-only configuration for every enemy variant in the game.
 * MapGenerator reads this to spawn enemies; AISystem uses the strategy field.
 *
 * To add a new enemy type, add an entry here and (optionally) a new strategy
 * file under src/ai/strategies/.
 */

import type { AI } from '../core/ECS';

export interface EnemyTypeConfig {
  char:       string;
  name:       string;
  color:      number;
  hpMin:      number;
  hpMax:      number;
  attack:     number;
  defense:    number;
  moveRange:  number;
  strategy:   AI['strategy'];
  weight:     number;      // relative spawn probability
  groupSize?: number;      // if > 1, always spawns this many together
}

export const ENEMY_TYPES: EnemyTypeConfig[] = [
  // Goblin — basic rushdown, low HP — dusty peach
  { char: 'g', name: 'Goblin',  color: 0xffccaa,
    hpMin: 18, hpMax: 26,  attack: 6,  defense: 1, moveRange: 2,
    strategy: 'basic', weight: 3 },
  // Archer — stays at range, shoots; fragile — pale straw
  { char: 'a', name: 'Archer',  color: 0xeeeebb,
    hpMin: 8,  hpMax: 12,  attack: 5,  defense: 0, moveRange: 3,
    strategy: 'ranged', weight: 2 },
  // Brute — massive HP, moves every other turn — pale dusty rose
  { char: 'B', name: 'Brute',   color: 0xffbbbb,
    hpMin: 60, hpMax: 75,  attack: 13, defense: 4, moveRange: 2,
    strategy: 'brute', weight: 1 },
  // Swarmer — very weak, always spawns in clusters — pale sage
  { char: 'z', name: 'Swarmer', color: 0xbbddcc,
    hpMin: 8,  hpMax: 14,  attack: 3,  defense: 0, moveRange: 3,
    strategy: 'swarm', weight: 3, groupSize: 3 },
];
