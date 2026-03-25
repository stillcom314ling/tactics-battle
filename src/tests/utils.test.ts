/**
 * Utils Tests
 *
 * Tests for: colors (lerpColor, elementColor, hpColor) and random (pick, randomInt, shuffleInPlace, weightedPick).
 */

import { describe, it, expect } from 'vitest';
import { lerpColor, elementColor, hpColor } from '../utils/colors';
import { pick, randomInt, shuffleInPlace, weightedPick } from '../utils/random';

// ─── COLORS ───────────────────────────────────────────────────────────────────

describe('lerpColor', () => {
  it('returns a at t=0', () => {
    expect(lerpColor(0xff0000, 0x0000ff, 0)).toBe(0xff0000);
  });

  it('returns b at t=1', () => {
    expect(lerpColor(0xff0000, 0x0000ff, 1)).toBe(0x0000ff);
  });

  it('blends midpoint correctly', () => {
    const mid = lerpColor(0x000000, 0xffffff, 0.5);
    const r = (mid >> 16) & 0xff;
    const g = (mid >>  8) & 0xff;
    const b =  mid        & 0xff;
    expect(r).toBeCloseTo(128, -1);
    expect(g).toBeCloseTo(128, -1);
    expect(b).toBeCloseTo(128, -1);
  });
});

describe('elementColor', () => {
  it('returns a hex number for known elements', () => {
    expect(typeof elementColor('fire')).toBe('number');
    expect(elementColor('fire')).toBeGreaterThan(0);
  });

  it('falls back to white for unknown elements', () => {
    expect(elementColor('unknown')).toBe(0xffffff);
  });
});

describe('hpColor', () => {
  it('is green at full HP', () => {
    const color = hpColor(1.0);
    const r = (color >> 16) & 0xff;
    const g = (color >>  8) & 0xff;
    // Green channel should dominate
    expect(g).toBeGreaterThan(r);
  });

  it('is red at 0 HP', () => {
    const color = hpColor(0);
    const r = (color >> 16) & 0xff;
    const g = (color >>  8) & 0xff;
    expect(r).toBeGreaterThan(g);
  });

  it('clamps below 0', () => {
    expect(() => hpColor(-0.5)).not.toThrow();
  });

  it('clamps above 1', () => {
    expect(() => hpColor(1.5)).not.toThrow();
  });
});

// ─── RANDOM ───────────────────────────────────────────────────────────────────

describe('pick', () => {
  it('returns an element from the array', () => {
    const arr = [1, 2, 3, 4, 5];
    const result = pick(arr);
    expect(arr).toContain(result);
  });

  it('works on single-element arrays', () => {
    expect(pick(['only'])).toBe('only');
  });
});

describe('randomInt', () => {
  it('stays within [min, max] inclusive over many calls', () => {
    for (let i = 0; i < 200; i++) {
      const v = randomInt(3, 7);
      expect(v).toBeGreaterThanOrEqual(3);
      expect(v).toBeLessThanOrEqual(7);
    }
  });

  it('returns exact value when min === max', () => {
    expect(randomInt(5, 5)).toBe(5);
  });
});

describe('shuffleInPlace', () => {
  it('returns the same array reference', () => {
    const arr = [1, 2, 3];
    expect(shuffleInPlace(arr)).toBe(arr);
  });

  it('contains the same elements after shuffle', () => {
    const arr = [1, 2, 3, 4, 5];
    const copy = [...arr];
    shuffleInPlace(arr);
    expect(arr.sort()).toEqual(copy.sort());
  });
});

describe('weightedPick', () => {
  it('returns an item from the array', () => {
    const items = [{ weight: 1, val: 'a' }, { weight: 3, val: 'b' }];
    const result = weightedPick(items);
    expect(items).toContain(result);
  });

  it('strongly prefers higher-weight items over many samples', () => {
    const items = [{ weight: 1, val: 'rare' }, { weight: 99, val: 'common' }];
    const counts: Record<string, number> = { rare: 0, common: 0 };
    for (let i = 0; i < 500; i++) {
      counts[weightedPick(items).val]++;
    }
    // 'common' should appear vastly more often
    expect(counts.common).toBeGreaterThan(counts.rare * 5);
  });
});
