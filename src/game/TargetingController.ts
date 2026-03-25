/**
 * TARGETING CONTROLLER
 *
 * Manages the spell-targeting state machine extracted from Game.ts.
 *
 * States:
 *   free      — no spell selected, normal input
 *   targeting — spell selected; in-range tiles highlighted; two-tap confirm
 *               required for aoe/line patterns
 *
 * The controller owns the visual highlight/preview state and delegates all
 * ECS/renderer mutations back through TargetingCallbacks.
 */

import { World, EntityId, Position, Renderable, Ability } from '../core/ECS';
import { ParallaxAsciiRenderer } from '../rendering/ParallaxAsciiRenderer';
import { GeneratedMap } from './MapGenerator';
import { getRangeTiles, getSpellTiles } from './SpellSystem';
import { elementColor } from '../utils/colors';
import { getActorAt } from '../combat/CombatSystem';
import { makeActorPattern } from '../constants/patterns';

// ─── SUB-CELL HELPERS ────────────────────────────────────────────────────────

/**
 * Write a 3×3 actor pattern into the gameplay layer at game-tile (col, row).
 * Mirrors the helper in RenderSystem — duplicated here to avoid circular deps.
 */
function setActorPattern(
  renderer: ParallaxAsciiRenderer,
  col: number, row: number,
  char: string, fg: number, alpha = 1.0,
): void {
  const pat = makeActorPattern(char, fg);
  for (let dy = 0; dy < 3; dy++) {
    for (let dx = 0; dx < 3; dx++) {
      const cell = pat[dy][dx];
      renderer.setCell(
        'gameplay', col * 3 + dx, row * 3 + dy,
        cell ? { ...cell, alpha: (cell.alpha ?? 1) * alpha } : null,
      );
    }
  }
}

/**
 * Highlight a single floor tile by overwriting only its centre sub-cell.
 * Terrain surround chars remain untouched, giving a subtle indicator.
 */
function setFloorHighlight(
  renderer: ParallaxAsciiRenderer,
  col: number, row: number,
  char: string, fg: number, alpha: number,
): void {
  renderer.setCell('gameplay', col * 3 + 1, row * 3 + 1, { char, fg, alpha });
}

export type TargetingMode = 'free' | 'targeting';

// ─── CALLBACKS ───────────────────────────────────────────────────────────────

export interface TargetingCallbacks {
  /** Append a line to the HUD message log. */
  addMessage: (msg: string) => void;
  /** Restore the terrain tile at (col, row) to its map appearance. */
  restoreTerrain: (col: number, row: number) => void;
  /** Redraw all actor glyphs onto the gameplay layer. */
  drawActors: () => void;
}

// ─── STATE ────────────────────────────────────────────────────────────────────

export interface TargetingState {
  readonly mode:            TargetingMode;
  readonly selectedAbility: Ability | null;
  /** Pending first-tap position for aoe/line two-tap confirm. */
  readonly pendingTargetCol: number | null;
  readonly pendingTargetRow: number | null;
}

// ─── CONTROLLER ──────────────────────────────────────────────────────────────

export class TargetingController {
  private _mode:            TargetingMode = 'free';
  private _selectedAbility: Ability | null = null;
  private _pendingCol:      number | null  = null;
  private _pendingRow:      number | null  = null;

  /** Tiles currently tinted as in-range targets. */
  private highlightTiles:    [number, number][] = [];
  /** Tiles tinted as blast-preview (aoe / line first tap). */
  private spellPreviewTiles: [number, number][] = [];

  constructor(
    private readonly world:     World,
    private readonly renderer:  ParallaxAsciiRenderer,
    private readonly map:       GeneratedMap,
    private readonly playerId:  EntityId,
    private readonly callbacks: TargetingCallbacks,
  ) {}

  // ── Public getters ─────────────────────────────────────────────────────────

  get state(): TargetingState {
    return {
      mode:             this._mode,
      selectedAbility:  this._selectedAbility,
      pendingTargetCol: this._pendingCol,
      pendingTargetRow: this._pendingRow,
    };
  }

  get isTargeting(): boolean { return this._mode === 'targeting'; }

  // ── State transitions ──────────────────────────────────────────────────────

  /** Enter targeting mode for the given spell. */
  enter(ability: Ability): void {
    this._mode            = 'targeting';
    this._selectedAbility = ability;
    this._showRangeHighlight(ability);
    this.callbacks.addMessage(`Select target for ${ability.name}. Tap yourself to cancel.`);
  }

  /** Cancel targeting — clear highlights and return to free mode. */
  cancel(): void {
    this._clearRangeHighlight();
    this._mode            = 'free';
    this._selectedAbility = null;
    this.callbacks.addMessage('Cancelled.');
  }

  /** Clear state without messaging (used after a successful cast). */
  clearAfterCast(): void {
    this._clearRangeHighlight();
    this._mode            = 'free';
    this._selectedAbility = null;
  }

  // ── First/second tap handling ──────────────────────────────────────────────

  /**
   * Handle a targeting-mode tap at (col, row).
   *
   * Returns:
   *   'cancel'  — player tapped their own tile
   *   'preview' — first tap on an aoe/line spell; blast preview shown
   *   'cast'    — spell ready to be cast (call castPending() next)
   *   'out_of_range' — tap was outside range
   */
  handleTap(col: number, row: number): 'cancel' | 'preview' | 'cast' | 'out_of_range' {
    const ability   = this._selectedAbility!;
    const playerPos = this.world.getComponent<Position>(this.playerId, 'position')!;

    // Tap own tile → cancel
    if (col === playerPos.col && row === playerPos.row) {
      this.cancel();
      return 'cancel';
    }

    // Range check
    const dist = Math.sqrt((col - playerPos.col) ** 2 + (row - playerPos.row) ** 2);
    if (dist > ability.range) {
      this.callbacks.addMessage('Out of range.');
      return 'out_of_range';
    }

    const needsPreview = ability.pattern === 'aoe' || ability.pattern === 'line';
    if (needsPreview) {
      const sameTile = col === this._pendingCol && row === this._pendingRow;
      if (!sameTile) {
        // First tap: show preview and wait for second tap on same tile
        this._pendingCol = col;
        this._pendingRow = row;
        this._showSpellPreview(col, row);
        this.callbacks.addMessage(`Tap again to cast ${ability.name}.`);
        return 'preview';
      }
      // Second tap on same tile: fall through to 'cast'
    }

    return 'cast';
  }

  /** Return the pending target position (set after first tap on aoe/line). */
  getPendingTarget(): [number, number] | null {
    if (this._pendingCol === null || this._pendingRow === null) return null;
    return [this._pendingCol, this._pendingRow];
  }

  // ── Private visual helpers ─────────────────────────────────────────────────

  private _showRangeHighlight(ability: Ability): void {
    const pos   = this.world.getComponent<Position>(this.playerId, 'position')!;
    const color = elementColor(ability.element);
    const all   = getRangeTiles(pos.col, pos.row, ability.range, this.map);

    this.highlightTiles = all.filter(([c, r]) => this.map.walkable[r]?.[c]);

    for (const [c, r] of this.highlightTiles) {
      const actor = getActorAt(this.world, c, r, 'enemy');
      if (actor !== null) {
        const rend = this.world.getComponent<Renderable>(actor, 'renderable')!;
        setActorPattern(this.renderer, c, r, rend.char, color, 0.9);
      } else {
        setFloorHighlight(this.renderer, c, r, '·', color, 0.65);
      }
    }
  }

  private _clearRangeHighlight(): void {
    this._clearSpellPreview();
    this._pendingCol = null;
    this._pendingRow = null;
    for (const [c, r] of this.highlightTiles) this.callbacks.restoreTerrain(c, r);
    this.callbacks.drawActors();
    this.highlightTiles = [];
  }

  private _showSpellPreview(col: number, row: number): void {
    this._clearSpellPreview();
    const ability = this._selectedAbility;
    if (!ability) return;
    if (ability.pattern !== 'aoe' && ability.pattern !== 'line') return;

    const pos   = this.world.getComponent<Position>(this.playerId, 'position')!;
    const color = elementColor(ability.element);
    const tiles = getSpellTiles(pos.col, pos.row, col, row, ability, this.map);

    for (const [c, r] of tiles) {
      this.spellPreviewTiles.push([c, r]);
      const actor = getActorAt(this.world, c, r, 'enemy');
      if (actor !== null) {
        const rend = this.world.getComponent<Renderable>(actor, 'renderable')!;
        setActorPattern(this.renderer, c, r, rend.char, color, 1.0);
      } else {
        setFloorHighlight(this.renderer, c, r, '*', color, 0.90);
      }
    }
  }

  private _clearSpellPreview(): void {
    const color = elementColor(this._selectedAbility?.element ?? 'none');

    for (const [c, r] of this.spellPreviewTiles) {
      const inRange = this.highlightTiles.some(([hc, hr]) => hc === c && hr === r);
      if (inRange) {
        const actor = getActorAt(this.world, c, r, 'enemy');
        if (actor !== null) {
          const rend = this.world.getComponent<Renderable>(actor, 'renderable')!;
          setActorPattern(this.renderer, c, r, rend.char, color, 0.9);
        } else {
          setFloorHighlight(this.renderer, c, r, '·', color, 0.65);
        }
      } else {
        this.callbacks.restoreTerrain(c, r);
        // Redraw actor on this tile if present
        const actor = getActorAt(this.world, c, r);
        if (actor !== null) {
          const pos  = this.world.getComponent<Position>(actor, 'position')!;
          const rend = this.world.getComponent<Renderable>(actor, 'renderable')!;
          setActorPattern(this.renderer, pos.col, pos.row, rend.char, rend.fg, rend.alpha ?? 1.0);
        }
      }
    }
    this.spellPreviewTiles = [];
  }
}
