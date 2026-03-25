/**
 * COLOR UTILITIES
 *
 * Shared color math used by multiple modules.
 * Previously lerpColor was duplicated in Game.ts and TooltipOverlay.ts.
 */

import { ELEMENT_COLORS } from '../constants/rendering';

/**
 * Linearly interpolate between two 0xRRGGBB hex colours.
 * t=0 returns `a`, t=1 returns `b`.
 */
export function lerpColor(a: number, b: number, t: number): number {
  const ar = (a >> 16) & 0xff, ag = (a >> 8) & 0xff, ab = a & 0xff;
  const br = (b >> 16) & 0xff, bg = (b >> 8) & 0xff, bb = b & 0xff;
  const r  = Math.round(ar + (br - ar) * t);
  const g  = Math.round(ag + (bg - ag) * t);
  const bv = Math.round(ab + (bb - ab) * t);
  return (r << 16) | (g << 8) | bv;
}

/**
 * Return the hex colour associated with a spell element.
 * Falls back to white for unknown elements.
 */
export function elementColor(element: string): number {
  return ELEMENT_COLORS[element] ?? 0xffffff;
}

/**
 * Produce an HP-ratio colour blending green → yellow → red.
 * ratio=1 → green, ratio=0.5 → yellow, ratio=0 → red.
 */
export function hpColor(ratio: number): number {
  const clamped = Math.max(0, Math.min(1, ratio));
  return clamped > 0.5
    ? lerpColor(0xffcc00, 0x44dd44, (clamped - 0.5) * 2)
    : lerpColor(0xff3333, 0xffcc00, clamped * 2);
}
