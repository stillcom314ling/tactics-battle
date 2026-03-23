export { World } from './ECS';
export type { EntityId, Component, Position, Renderable, Health, Faction, StatusEffect, Movement, Combat, Terrain } from './ECS';
export { TurnManager } from './TurnManager';
export type { TurnActor, TurnPhase } from './TurnManager';
export { processInteractions, INTERACTION_RULES } from './InteractionSystem';
export type { InteractionRule, InteractionContext } from './InteractionSystem';
