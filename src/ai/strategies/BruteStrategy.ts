/**
 * BRUTE STRATEGY — Heavy enemy
 *
 * Behaviour: same as basic (rush + melee), but acts only every other turn.
 * The AI component's moveCounter is incremented here and used to gate action.
 */

import { EntityId, Position, StatusEffect, AI } from '../../core/ECS';
import { EnemyTurnContext } from '../AISystem';

export function executeBruteTurn(enemyId: EntityId, ctx: EnemyTurnContext): void {
  const ai = ctx.world.getComponent<AI>(enemyId, 'ai')!;

  ai.moveCounter = (ai.moveCounter ?? 0) + 1;
  // Brute skips every odd-numbered tick (acts on 2, 4, 6 …)
  if (ai.moveCounter % 2 !== 0) return;

  const ePos   = ctx.world.getComponent<Position>(enemyId, 'position')!;
  const status = ctx.world.getComponent<StatusEffect>(enemyId, 'status');
  const slowed = status?.effects.has('slowed') ?? false;

  const dx = Math.abs(ctx.playerPos.col - ePos.col);
  const dy = Math.abs(ctx.playerPos.row - ePos.row);

  if (dx <= 1 && dy <= 1) {
    ctx.resolveMelee(enemyId, ctx.playerId);
    return;
  }

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
