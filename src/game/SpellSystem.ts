/**
 * SPELL SYSTEM
 *
 * Resolves spell casts: hit detection, damage, status effects, interactions.
 * Each spell is an Ability data object — adding a new spell is a single factory function.
 *
 * Spell patterns:
 *   single — one tile at target position
 *   line   — Bresenham line from caster to target
 *   aoe    — circle of aoeRadius around target
 *   self   — caster's tile (or aoe around caster if aoeRadius is set)
 *
 * Spells use per-level CHARGES instead of per-turn cooldowns.
 * This rewards aggressive forward play — no waiting between rooms.
 * Charges refill when populateWorld() is called for a new level.
 */

import {
  World, EntityId, Ability, Abilities, Passives, Position, Health, StatusEffect,
  applyStatus, STATUS_DURATIONS,
} from '../core/ECS';
import { processInteractions } from '../core/InteractionSystem';
import { GeneratedMap } from './MapGenerator';

// ─── SPELL CALLBACKS ─────────────────────────────────────────────────────────

/** Side-effect hooks provided by the caller so resolveSpell stays decoupled. */
export interface SpellCallbacks {
  addMessage: (msg: string) => void;
  /** Called when a target entity is killed by this spell. */
  onKill?: (killedId: EntityId) => void;
  /** Called each time a status effect is applied to a target. */
  onStatusApplied?: (targetId: EntityId, effect: string) => void;
  /** Called for knockback: move entity to the computed push tile. */
  onKnockback?: (entityId: EntityId, toCol: number, toRow: number) => void;
  /** Called for super_jump: teleport caster to target tile before damage. */
  onTeleportCaster?: (toCol: number, toRow: number) => void;
}

// ─── STARTER SPELL FACTORIES (original four) ─────────────────────────────────

export function makeFlameBolt(): Ability {
  return {
    id: 'flame_bolt', name: 'Flame Bolt', element: 'fire',
    range: 5, pattern: 'single', damage: 10,
    effects: ['burning'],
    charges: 3, chargesMax: 3,
  };
}

export function makeArcLightning(): Ability {
  return {
    id: 'arc_lightning', name: 'Arc Lightning', element: 'lightning',
    range: 5, pattern: 'line', damage: 7,
    effects: ['shocked'],
    charges: 2, chargesMax: 2,
  };
}

export function makeFrostShard(): Ability {
  return {
    id: 'frost_shard', name: 'Frost Shard', element: 'ice',
    range: 4, pattern: 'single', damage: 6,
    effects: ['wet', 'slowed'],
    charges: 3, chargesMax: 3,
  };
}

export function makePoisonCloud(): Ability {
  return {
    id: 'poison_cloud', name: 'Poison Cloud', element: 'poison',
    range: 4, pattern: 'aoe', aoeRadius: 2, damage: 4,
    effects: ['poisoned'],
    charges: 2, chargesMax: 2,
  };
}

/** Returns the default set of player abilities for a new game / floor reset. */
export function makeDefaultPlayerAbilities(): Ability[] {
  return [makeFlameBolt(), makeArcLightning(), makeFrostShard(), makePoisonCloud()];
}

// ─── SET 1: THE PLUMBER (Mario) ───────────────────────────────────────────────

export function makeGroundPound(): Ability {
  return {
    id: 'ground_pound', name: 'Ground Pound', element: 'none',
    range: 0, pattern: 'self', aoeRadius: 1, damage: 8,
    effects: [],
    knockbackOnHit: true,
    charges: 3, chargesMax: 3,
  };
}

export function makeFireFlower(): Ability {
  return {
    id: 'fire_flower', name: 'Fire Flower', element: 'fire',
    range: 4, pattern: 'line', damage: 9,
    effects: ['burning'],
    charges: 3, chargesMax: 3,
  };
}

export function makeStarPower(): Ability {
  return {
    id: 'star_power', name: 'Star Power', element: 'arcane',
    range: 0, pattern: 'self', damage: 0,
    effects: ['invincible'],
    appliesEffectsToSelf: true,
    charges: 1, chargesMax: 1,
  };
}

export function makeSuperJump(): Ability {
  return {
    id: 'super_jump', name: 'Super Jump', element: 'none',
    range: 4, pattern: 'aoe', aoeRadius: 1, damage: 6,
    effects: [],
    teleportCaster: true,
    charges: 2, chargesMax: 2,
  };
}

export function makeMarioSet(): Ability[] {
  return [makeGroundPound(), makeFireFlower(), makeStarPower(), makeSuperJump()];
}

// ─── SET 2: THE FROST DEATH KNIGHT (WoW) ─────────────────────────────────────

export function makeObliterate(): Ability {
  return {
    id: 'obliterate', name: 'Obliterate', element: 'ice',
    range: 1, pattern: 'single', damage: 14,
    effects: ['wet'],
    casterProcOnHit: [{ status: 'killing_machine', chance: 0.3 }],
    charges: 2, chargesMax: 2,
  };
}

export function makeFrostStrike(): Ability {
  return {
    id: 'frost_strike', name: 'Frost Strike', element: 'ice',
    range: 1, pattern: 'single', damage: 10,
    effects: ['stunned'],
    charges: 3, chargesMax: 3,
  };
}

export function makeHowlingBlast(): Ability {
  return {
    id: 'howling_blast', name: 'Howling Blast', element: 'ice',
    range: 4, pattern: 'aoe', aoeRadius: 2, damage: 7,
    effects: ['wet', 'slowed'],
    charges: 2, chargesMax: 2,
  };
}

export function makePillarOfFrost(): Ability {
  return {
    id: 'pillar_of_frost', name: 'Pillar of Frost', element: 'ice',
    range: 0, pattern: 'self', damage: 0,
    effects: ['empowered'],
    appliesEffectsToSelf: true,
    charges: 1, chargesMax: 1,
  };
}

export function makeFrostDKSet(): Ability[] {
  return [makeObliterate(), makeFrostStrike(), makeHowlingBlast(), makePillarOfFrost()];
}

// ─── PASSIVE FACTORIES ────────────────────────────────────────────────────────

import { Passive, PassiveContext } from '../core/ECS';

/** Mario passives — returned as a list for the Passives component. */
export function makeMarioPassives(): Passive[] {
  return [
    {
      id: 'coin_collector',
      name: 'Coin Collector',
      description: 'On kill: restore 5 HP',
      trigger: 'on_kill',
      effect: (world, playerId, ctx) => {
        const health = world.getComponent<Health>(playerId, 'health');
        if (!health) return;
        health.current = Math.min(health.max, health.current + 5);
        ctx.addMessage?.('🪙 Coin Collector: +5 HP!');
      },
    },
    {
      id: 'double_jump',
      name: 'Double Jump',
      description: 'On cast: gain one free movement step',
      trigger: 'on_cast',
      effect: (world, playerId, ctx) => {
        const status = world.getComponent<StatusEffect>(playerId, 'status');
        if (!status) return;
        applyStatus(status, 'hasted', STATUS_DURATIONS['hasted'] ?? 1);
        ctx.addMessage?.('Double Jump! Move one step for free.');
      },
    },
    {
      id: 'underdog',
      name: 'Underdog',
      description: 'When HP < 50%: +3 spell damage',
      trigger: 'passive',
      effect: () => { /* damage bonus applied inline in resolveSpell */ },
    },
  ];
}

/** Frost DK passives — returned as a list for the Passives component. */
export function makeFrostDKPassives(): Passive[] {
  return [
    {
      id: 'killing_machine_passive',
      name: 'Killing Machine',
      description: 'On kill: next Obliterate or Frost Strike deals 2× damage',
      trigger: 'on_kill',
      effect: (world, playerId, ctx) => {
        const status = world.getComponent<StatusEffect>(playerId, 'status');
        if (!status) return;
        applyStatus(status, 'killing_machine', STATUS_DURATIONS['killing_machine'] ?? 1);
        ctx.addMessage?.('⚔ Killing Machine: next strike hits twice as hard!');
      },
    },
    {
      id: 'rime',
      name: 'Rime',
      description: 'Applying wet or slowed: 40% chance for a free Howling Blast',
      trigger: 'on_status_apply',
      effect: (world, playerId, ctx) => {
        if (ctx.statusApplied !== 'wet' && ctx.statusApplied !== 'slowed') return;
        if (Math.random() >= 0.4) return;
        const status = world.getComponent<StatusEffect>(playerId, 'status');
        if (!status) return;
        applyStatus(status, 'rime_proc', STATUS_DURATIONS['rime_proc'] ?? 1);
        ctx.addMessage?.('❄ Rime proc! Next Howling Blast is free.');
      },
    },
    {
      id: 'permafrost',
      name: 'Permafrost',
      description: 'Slowed enemies take +2 from all ice spells',
      trigger: 'passive',
      effect: () => { /* damage bonus applied inline in resolveSpell */ },
    },
    {
      id: 'runic_empowerment',
      name: 'Runic Empowerment',
      description: 'On kill while Empowered: refill 1 random ability charge',
      trigger: 'on_kill',
      effect: (world, playerId, ctx) => {
        const status = world.getComponent<StatusEffect>(playerId, 'status');
        if (!status?.effects.has('empowered')) return;
        const abilities = world.getComponent<Abilities>(playerId, 'abilities');
        if (!abilities) return;
        const depleted = abilities.list.filter(a => a.charges < a.chargesMax);
        if (depleted.length === 0) return;
        const target = depleted[Math.floor(Math.random() * depleted.length)];
        target.charges = Math.min(target.charges + 1, target.chargesMax);
        ctx.addMessage?.(`⚡ Runic Empowerment: ${target.name} +1 charge!`);
      },
    },
  ];
}

// ─── TILE GEOMETRY ───────────────────────────────────────────────────────────

/** All tiles hit by a spell given caster + target position. */
export function getSpellTiles(
  casterCol: number, casterRow: number,
  targetCol: number, targetRow: number,
  ability: Ability,
  map: GeneratedMap,
): [number, number][] {
  const inBounds = (c: number, r: number) => c >= 0 && c < map.width && r >= 0 && r < map.height;

  switch (ability.pattern) {
    case 'single':
      return [[targetCol, targetRow]];

    case 'self': {
      // If aoeRadius is set, produce an AoE circle around the CASTER
      const rad = ability.aoeRadius;
      if (rad) {
        const tiles: [number, number][] = [];
        for (let dy = -rad; dy <= rad; dy++) {
          for (let dx = -rad; dx <= rad; dx++) {
            if (dx * dx + dy * dy <= rad * rad && (dx !== 0 || dy !== 0)) {
              const c = casterCol + dx, r = casterRow + dy;
              if (inBounds(c, r)) tiles.push([c, r]);
            }
          }
        }
        return tiles;
      }
      return [[casterCol, casterRow]];
    }

    case 'aoe': {
      const rad = ability.aoeRadius ?? 1;
      const tiles: [number, number][] = [];
      for (let dy = -rad; dy <= rad; dy++) {
        for (let dx = -rad; dx <= rad; dx++) {
          if (dx * dx + dy * dy <= rad * rad) {
            const c = targetCol + dx, r = targetRow + dy;
            if (inBounds(c, r)) tiles.push([c, r]);
          }
        }
      }
      return tiles;
    }

    case 'line': {
      // Bresenham line from caster (exclusive) to target (inclusive)
      const tiles: [number, number][] = [];
      let x = casterCol, y = casterRow;
      const adx = Math.abs(targetCol - x), ady = Math.abs(targetRow - y);
      const sx = x < targetCol ? 1 : -1;
      const sy = y < targetRow ? 1 : -1;
      let err = adx - ady;
      while (true) {
        if ((x !== casterCol || y !== casterRow) && inBounds(x, y)) {
          tiles.push([x, y]);
        }
        if (x === targetCol && y === targetRow) break;
        const e2 = 2 * err;
        if (e2 > -ady) { err -= ady; x += sx; }
        if (e2 < adx)  { err += adx; y += sy; }
      }
      return tiles;
    }
  }
}

/** All tiles in a circle of `range` around (col, row) for targeting highlight. */
export function getRangeTiles(
  col: number, row: number,
  range: number,
  map: GeneratedMap,
): [number, number][] {
  const tiles: [number, number][] = [];
  for (let dy = -range; dy <= range; dy++) {
    for (let dx = -range; dx <= range; dx++) {
      if (dx === 0 && dy === 0) continue;
      if (Math.sqrt(dx * dx + dy * dy) <= range) {
        const c = col + dx, r = row + dy;
        if (c >= 0 && c < map.width && r >= 0 && r < map.height) {
          tiles.push([c, r]);
        }
      }
    }
  }
  return tiles;
}

// ─── RESOLUTION ──────────────────────────────────────────────────────────────

/** Cast a spell: applies damage, status effects, and runs interaction rules. */
export function resolveSpell(
  world: World,
  caster: EntityId,
  ability: Ability,
  targetCol: number,
  targetRow: number,
  map: GeneratedMap,
  callbacks: SpellCallbacks,
): void {
  // ── Teleport caster first (super_jump) ────────────────────────────────────
  if (ability.teleportCaster && callbacks.onTeleportCaster) {
    callbacks.onTeleportCaster(targetCol, targetRow);
  }

  const casterPos    = world.getComponent<Position>(caster, 'position')!;
  const casterStatus = world.getComponent<StatusEffect>(caster, 'status');
  const casterHp     = world.getComponent<Health>(caster, 'health');
  const casterPassives = world.getComponent<Passives>(caster, 'passives');

  // ── Consume Rime proc for Howling Blast ───────────────────────────────────
  const freeHowlingBlast =
    ability.id === 'howling_blast' && casterStatus?.effects.has('rime_proc');
  if (freeHowlingBlast) casterStatus!.effects.delete('rime_proc');

  // ── Self-effect spells (star_power, pillar_of_frost) ─────────────────────
  if (ability.appliesEffectsToSelf) {
    if (casterStatus) {
      for (const eff of ability.effects) {
        applyStatus(casterStatus, eff, STATUS_DURATIONS[eff] ?? 2);
        callbacks.addMessage?.(`${ability.name}: ${eff}!`);
      }
    }
    if (!freeHowlingBlast) ability.charges = Math.max(0, ability.charges - 1);
    return;
  }

  // ── Passive-based damage modifiers ────────────────────────────────────────
  const hasUnderdog   = casterPassives?.list.some(p => p.id === 'underdog')   ?? false;
  const hasPermafrost = casterPassives?.list.some(p => p.id === 'permafrost') ?? false;

  const tiles = getSpellTiles(casterPos.col, casterPos.row, targetCol, targetRow, ability, map);

  let hitCount = 0;

  for (const [tc, tr] of tiles) {
    const targets = world.query('position', 'health', 'faction').filter(id => {
      if (id === caster) return false;
      const p = world.getComponent<Position>(id, 'position')!;
      return p.col === tc && p.row === tr;
    });

    for (const targetId of targets) {
      const health       = world.getComponent<Health>(targetId, 'health')!;
      const targetStatus = world.getComponent<StatusEffect>(targetId, 'status');

      // ── Compute damage with passive bonuses ─────────────────────────────
      let dmg = ability.damage;

      // Empowered: +5 dmg on all spells
      if (casterStatus?.effects.has('empowered')) dmg += 5;

      // Underdog: +3 dmg when caster HP < 50%
      if (hasUnderdog && casterHp && casterHp.current < casterHp.max * 0.5) dmg += 3;

      // Permafrost: +2 on ice spells vs slowed targets
      if (hasPermafrost && ability.element === 'ice' && targetStatus?.effects.has('slowed')) dmg += 2;

      // Killing Machine: 2× dmg for obliterate / frost_strike
      const kmActive = casterStatus?.effects.has('killing_machine')
        && (ability.id === 'obliterate' || ability.id === 'frost_strike');
      if (kmActive) {
        dmg *= 2;
        casterStatus!.effects.delete('killing_machine');
        callbacks.addMessage('⚔ Killing Machine!');
      }

      health.current -= dmg;
      hitCount++;

      // ── Apply status effects ─────────────────────────────────────────────
      if (targetStatus) {
        for (const eff of ability.effects) {
          applyStatus(targetStatus, eff, STATUS_DURATIONS[eff] ?? 3);
          callbacks.onStatusApplied?.(targetId, eff);
        }
      }

      // ── Caster procs on hit ──────────────────────────────────────────────
      if (ability.casterProcOnHit && casterStatus) {
        for (const { status, chance } of ability.casterProcOnHit) {
          if (Math.random() < chance) {
            applyStatus(casterStatus, status, STATUS_DURATIONS[status] ?? 1);
            if (status === 'killing_machine') {
              callbacks.addMessage('⚔ Killing Machine proc!');
            }
          }
        }
      }

      // ── Interaction synergies (wet+lightning, burning+terrain, etc.) ─────
      processInteractions(world, targetId, {
        trigger:    'attack',
        sourceId:   caster,
        data:       { element: ability.element, damage: dmg },
        addMessage: callbacks.addMessage,
      });

      // ── Knockback (ground_pound) ─────────────────────────────────────────
      if (ability.knockbackOnHit && callbacks.onKnockback) {
        const ddx = tc - casterPos.col;
        const ddy = tr - casterPos.row;
        const len = Math.max(Math.abs(ddx), Math.abs(ddy), 1);
        const nx = tc + Math.sign(ddx === 0 ? 0 : ddx / len);
        const ny = tr + Math.sign(ddy === 0 ? 0 : ddy / len);
        if (nx >= 0 && nx < map.width && ny >= 0 && ny < map.height && map.walkable[ny]?.[nx]) {
          callbacks.onKnockback(targetId, nx, ny);
        }
      }

      // ── Kill or damage message ───────────────────────────────────────────
      if (health.current <= 0) {
        callbacks.addMessage(`${ability.name} slays the enemy!`);
        callbacks.onKill?.(targetId);
        world.removeEntity(targetId);
      } else {
        const fx = ability.effects.length > 0 ? ` [${ability.effects.join(', ')}]` : '';
        callbacks.addMessage(`${ability.name}: ${dmg} dmg${fx}`);
      }
    }
  }

  if (hitCount === 0) callbacks.addMessage(`${ability.name} hits nothing.`);

  // ── Spend a charge (skip for free Howling Blast) ──────────────────────────
  if (!freeHowlingBlast) ability.charges = Math.max(0, ability.charges - 1);
}

/** Refill all spell charges (call when starting a new level). */
export function refillSpellCharges(world: World): void {
  for (const id of world.query('abilities')) {
    const abs = world.getComponent<Abilities>(id, 'abilities')!;
    for (const a of abs.list) {
      a.charges = a.chargesMax;
    }
  }
}
