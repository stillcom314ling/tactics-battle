/**
 * RANDOM UTILITIES
 *
 * Small helpers for procedural generation and dice rolls.
 * Previously `pick` was a private function inside MapGenerator.ts.
 */

/**
 * Return a random element from a readonly or mutable array.
 */
export function pick<T>(arr: readonly T[]): T {
  return arr[Math.floor(Math.random() * arr.length)];
}

/**
 * Return a random integer in [min, max] inclusive.
 */
export function randomInt(min: number, max: number): number {
  return min + Math.floor(Math.random() * (max - min + 1));
}

/**
 * Pick an item from a weighted array.
 * Each entry must have a `weight: number` field.
 * Higher weight = more likely to be chosen.
 */
export function weightedPick<T extends { weight: number }>(items: T[]): T {
  const total = items.reduce((sum, item) => sum + item.weight, 0);
  let r = Math.random() * total;
  for (const item of items) {
    r -= item.weight;
    if (r <= 0) return item;
  }
  return items[0];
}

/**
 * Fisher-Yates shuffle. Mutates the array in-place and returns it.
 */
export function shuffleInPlace<T>(arr: T[]): T[] {
  for (let i = arr.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [arr[i], arr[j]] = [arr[j], arr[i]];
  }
  return arr;
}
