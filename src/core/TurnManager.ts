/**
 * TURN MANAGER
 *
 * Owns the authoritative turn-phase state for the game loop.
 * Previously Game.ts managed phase with raw string fields and setTimeout calls.
 * Now Game.ts delegates all phase bookkeeping here.
 *
 * Phase flow:
 *   player → (player acts) → endPlayerTurn()
 *        → enemy  → (AISystem.processEnemyTurns()) → endEnemyPhase()
 *        → player → … repeat
 *
 * The rot.js Speed scheduler is retained internally for future use (e.g.
 * per-entity speed variation), but phase transitions are driven explicitly
 * by Game.ts for clarity and testability.
 */

import * as ROT from 'rot-js';
import { EntityId, World } from './ECS';

export type TurnPhase = 'player' | 'enemy' | 'game_over';

export interface TurnActor {
  entityId: EntityId;
  speed:    number;
}

type WrappedActor = TurnActor & { getSpeed: () => number };

export class TurnManager {
  private _phase:      TurnPhase = 'player';
  private _turnNumber: number    = 0;

  // rot.js Speed scheduler — available for future per-entity speed variation
  private scheduler: InstanceType<typeof ROT.Scheduler.Speed>;
  private engine:    ROT.Engine;

  /** Fired when the phase changes (e.g. to dim the HUD during enemy turns). */
  public onPhaseChange: ((phase: TurnPhase) => void) | null = null;

  constructor(private world: World) {
    this.scheduler = new ROT.Scheduler.Speed();
    this.engine    = new ROT.Engine(this.scheduler);
  }

  // ── Getters ────────────────────────────────────────────────────────────────

  get phase():      TurnPhase { return this._phase; }
  get turnNumber(): number    { return this._turnNumber; }
  get isPlayerTurn(): boolean { return this._phase === 'player'; }
  get isGameOver():   boolean { return this._phase === 'game_over'; }

  // ── Phase transitions ──────────────────────────────────────────────────────

  /** Player has finished acting. Switch to enemy phase. */
  endPlayerTurn(): void {
    this._turnNumber++;
    this._setPhase('enemy');
  }

  /** All enemies have acted. Return to player phase. */
  endEnemyPhase(): void {
    this._setPhase('player');
  }

  /** Player has died. No more input accepted. */
  setGameOver(): void {
    this._setPhase('game_over');
  }

  private _setPhase(phase: TurnPhase): void {
    this._phase = phase;
    this.onPhaseChange?.(phase);
  }

  // ── rot.js actor registry (for future use) ────────────────────────────────

  addActor(actor: TurnActor): void {
    const wrapped: WrappedActor = { ...actor, getSpeed: () => actor.speed };
    this.scheduler.add(wrapped, true);
  }

  removeActor(entityId: EntityId): void {
    // rot.js has no direct remove — drain, filter, re-add
    const retained: TurnActor[] = [];
    let next = this.scheduler.next() as WrappedActor | null;
    while (next) {
      if (next.entityId !== entityId) retained.push(next);
      next = this.scheduler.next() as WrappedActor | null;
    }
    this.scheduler.clear();
    for (const a of retained) this.addActor(a);
  }
}
