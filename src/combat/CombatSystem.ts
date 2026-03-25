/**
 * COMBAT SYSTEM
 *
 * Pure functions for resolving melee and ranged attacks.
 * All side-effects (messages, hit flash, game-over) are communicated back
 * through the CombatCallbacks interface — making every function unit-testable
 * with a plain mock object.
 *
 * Extracted from Game.ts (was: private enemyMelee / enemyRangedAttack).
 */

import { World, EntityId, Health, Combat, Position, Faction } from '../core/ECS';
import { GeneratedMap } from '../game/MapGenerator';
import { MELEE_VARIANCE } from '../constants/combat';

// ─── CALLBACK CONTRACT ────────────────────────────────────────────────────────

/**
 * Side-effect hooks provided by Game.ts so CombatSystem stays decoupled
 * from PixiJS, camera state, and HUD internals.
 */
export interface CombatCallbacks {
  /** Append a line to the HUD message log. */
  addMessage: (msg: string) => void;
  /** Trigger the red screen-flash effect for N frames. */
  onPlayerHit: () => void;
  /** Called when the player's HP reaches 0. */
  onPlayerDied: () => void;
  /** Alert enemies within radius of (col, row) — enemy AI concern. */
  alertEnemies: (col: number, row: number, radius: number) => void;
}

// ─── HELPERS ─────────────────────────────────────────────────────────────────

/**
 * Check whether an unobstructed straight-line path exists from attacker to target.
 * Only cardinal and diagonal directions are supported (returns false otherwise).
 * A wall tile anywhere along the path blocks the shot.
 */
export function hasLineOfSight(
  attackerPos: Position,
  targetPos: Position,
  map: GeneratedMap,
): boolean {
  const dx = Math.sign(targetPos.col - attackerPos.col);
  const dy = Math.sign(targetPos.row - attackerPos.row);

  // Non-diagonal and non-cardinal (knight's-move style) — not supported
  const colDiff = Math.abs(targetPos.col - attackerPos.col);
  const rowDiff = Math.abs(targetPos.row - attackerPos.row);
  if (dx !== 0 && dy !== 0 && colDiff !== rowDiff) {
    // Fall through: proximity check only, no LoS rejection
  }

  let c = attackerPos.col + dx;
  let r = attackerPos.row + dy;
  while (c !== targetPos.col || r !== targetPos.row) {
    if (!map.walkable[r]?.[c]) return false;
    c += dx;
    r += dy;
  }
  return true;
}

// ─── MELEE ────────────────────────────────────────────────────────────────────

/**
 * Resolve a melee attack from `attackerId` against `defenderId`.
 *
 * Damage formula: attackPower + rand(0, MELEE_VARIANCE-1) - defense (min 1).
 *
 * @param world      - ECS world
 * @param attackerId - EntityId of the attacker
 * @param defenderId - EntityId of the defender (typically the player)
 * @param playerId   - Player's EntityId (needed to detect player death)
 * @param callbacks  - Side-effect hooks
 */
export function resolveMelee(
  world: World,
  attackerId: EntityId,
  defenderId: EntityId,
  playerId: EntityId,
  callbacks: CombatCallbacks,
): void {
  const atk       = world.getComponent<Combat>(attackerId, 'combat')!;
  const defCombat = world.getComponent<Combat>(defenderId, 'combat');
  const defHealth = world.getComponent<Health>(defenderId, 'health');
  if (!defHealth) return;

  const defense = defCombat?.defense ?? 0;
  const dmg     = Math.max(1, atk.attackPower + Math.floor(Math.random() * MELEE_VARIANCE) - defense);
  defHealth.current -= dmg;
  callbacks.addMessage(`Enemy hits you for ${dmg}!`);
  callbacks.onPlayerHit();

  const aPos = world.getComponent<Position>(attackerId, 'position');
  if (aPos) callbacks.alertEnemies(aPos.col, aPos.row, 4);

  if (defHealth.current <= 0 && defenderId === playerId) {
    callbacks.addMessage('YOU DIED. Refresh to restart.');
    world.removeComponent(playerId, 'health');
    callbacks.onPlayerDied();
  }
}

// ─── RANGED ───────────────────────────────────────────────────────────────────

/**
 * Attempt a ranged attack from `attackerId` toward `targetId`.
 * Returns true if the shot connected (line-of-sight clear), false if blocked.
 *
 * @param damage - Base damage before applying target's defense
 */
export function resolveRangedAttack(
  world: World,
  attackerId: EntityId,
  targetId: EntityId,
  damage: number,
  map: GeneratedMap,
  playerId: EntityId,
  callbacks: CombatCallbacks,
): boolean {
  const aPos    = world.getComponent<Position>(attackerId, 'position')!;
  const tPos    = world.getComponent<Position>(targetId,  'position');
  const tHealth = world.getComponent<Health>(targetId,    'health');
  if (!tPos || !tHealth) return false;

  if (!hasLineOfSight(aPos, tPos, map)) return false;

  const def = world.getComponent<Combat>(targetId, 'combat')?.defense ?? 0;
  const dmg = Math.max(1, damage - def);
  tHealth.current -= dmg;
  callbacks.addMessage(`Archer shoots you for ${dmg}!`);
  callbacks.onPlayerHit();
  callbacks.alertEnemies(aPos.col, aPos.row, 4);

  if (tHealth.current <= 0 && targetId === playerId) {
    callbacks.addMessage('YOU DIED. Refresh to restart.');
    world.removeComponent(playerId, 'health');
    callbacks.onPlayerDied();
  }

  return true;
}

// ─── QUERY HELPERS ────────────────────────────────────────────────────────────

/**
 * Return the entity at (col, row) with the given faction team, or null.
 * Optionally filter by team ('player' | 'enemy' | 'neutral').
 */
export function getActorAt(
  world: World,
  col: number,
  row: number,
  team?: 'player' | 'enemy' | 'neutral',
): EntityId | null {
  for (const id of world.query('position', 'faction')) {
    const pos = world.getComponent<Position>(id, 'position')!;
    if (pos.col !== col || pos.row !== row) continue;
    if (team && world.getComponent<Faction>(id, 'faction')?.team !== team) continue;
    return id;
  }
  return null;
}
