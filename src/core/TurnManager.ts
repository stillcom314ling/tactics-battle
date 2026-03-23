/**
 * TURN MANAGER
 * 
 * Handles turn-based flow for tactical combat.
 * Uses rot.js scheduler under the hood.
 * 
 * Flow: player phase -> enemy phase -> environment phase -> repeat
 */

import * as ROT from 'rot-js';
import { EntityId, World } from './ECS';

export type TurnPhase = 'player' | 'enemy' | 'environment';

export interface TurnActor {
  entityId: EntityId;
  phase: TurnPhase;
  speed: number; // higher = acts more often
}

type WrappedActor = TurnActor & { getSpeed: () => number };

export class TurnManager {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  private scheduler: InstanceType<typeof ROT.Scheduler.Speed>;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  private engine: ROT.Engine;
  public currentActor: TurnActor | null = null;
  public currentPhase: TurnPhase = 'player';
  public turnNumber: number = 0;

  // Callbacks for when a phase starts
  public onPhaseStart: ((phase: TurnPhase) => void) | null = null;
  public onTurnStart: ((actor: TurnActor) => void) | null = null;

  constructor(private world: World) {
    this.scheduler = new ROT.Scheduler.Speed();
    this.engine = new ROT.Engine(this.scheduler);
  }

  addActor(actor: TurnActor) {
    // rot.js Speed scheduler needs a getSpeed() method
    const wrapped: WrappedActor = {
      ...actor,
      getSpeed: () => actor.speed,
    };
    this.scheduler.add(wrapped, true);
  }

  removeActor(entityId: EntityId) {
    // rot.js doesn't have a great removal API, so we clear and rebuild
    // This is fine for tactical games with ~20 units
    const actors: TurnActor[] = [];
    let next = this.scheduler.next();
    while (next) {
      if (next.entityId !== entityId) {
        actors.push(next);
      }
      next = this.scheduler.next();
    }
    this.scheduler.clear();
    for (const a of actors) {
      this.addActor(a);
    }
  }

  /**
   * Get the next actor. Returns null if no actors remain.
   */
  nextTurn(): TurnActor | null {
    const actor = this.scheduler.next();
    if (!actor) return null;

    // Track phase transitions
    if (actor.phase !== this.currentPhase) {
      this.currentPhase = actor.phase;
      if (actor.phase === 'player') {
        this.turnNumber++;
      }
      this.onPhaseStart?.(this.currentPhase);
    }

    this.currentActor = actor;
    this.onTurnStart?.(actor);
    return actor;
  }
}
