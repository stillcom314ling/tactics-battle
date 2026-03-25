/**
 * RANGED STRATEGY — Archer
 *
 * Behaviour: keep distance; shoot when in range.
 *   - If too close (Chebyshev ≤ 2) and not alerted: back away.
 *   - If in range (Euclidean ≤ ARCHER_SHOOT_RANGE): attempt ranged shot.
 *   - If out of range or shot blocked: approach (unless slowed).
 */

import { EntityId, Position, StatusEffect, AI } from '../../core/ECS';
import { EnemyTurnContext } from '../AISystem';
import { ARCHER_SHOOT_RANGE, ARCHER_SHOT_DAMAGE, ARCHER_BACK_OFF_DIST } from '../../constants/combat';

export function executeRangedTurn(enemyId: EntityId, ctx: EnemyTurnContext): void {
  const ePos   = ctx.world.getComponent<Position>(enemyId, 'position')!;
  const ai     = ctx.world.getComponent<AI>(enemyId, 'ai')!;
  const status = ctx.world.getComponent<StatusEffect>(enemyId, 'status');
  const slowed = status?.effects.has('slowed') ?? false;

  const chebyshev = Math.max(
    Math.abs(ctx.playerPos.col - ePos.col),
    Math.abs(ctx.playerPos.row - ePos.row),
  );
  const euclidean = Math.hypot(ctx.playerPos.col - ePos.col, ctx.playerPos.row - ePos.row);

  // Back away if the player closes in (un-alerted only)
  if (chebyshev <= ARCHER_BACK_OFF_DIST && !ai.alerted && !slowed) {
    const fc = ePos.col + Math.sign(ePos.col - ctx.playerPos.col);
    const fr = ePos.row + Math.sign(ePos.row - ctx.playerPos.row);
    if (ctx.map.walkable[fr]?.[fc] && ctx.getActorAt(fc, fr) === null) {
      ctx.moveEntity(enemyId, fc, fr);
      return;
    }
  }

  // Attempt ranged shot
  if (euclidean <= ARCHER_SHOOT_RANGE) {
    const hit = ctx.resolveRangedAttack(enemyId, ctx.playerId, ARCHER_SHOT_DAMAGE);
    if (hit) return;
  }

  // Approach if out of range or shot was blocked
  if (!slowed) {
    const path = ctx.findPath(ePos.col, ePos.row, ctx.playerPos.col, ctx.playerPos.row, enemyId);
    if (path.length >= 2) {
      const [nc, nr] = path[1];
      if (nc !== ctx.playerPos.col || nr !== ctx.playerPos.row) {
        ctx.moveEntity(enemyId, nc, nr);
      }
    }
  }
}
