/**
 * HUD RENDERER
 *
 * Owns and updates all fixed-overlay UI text:
 *   - HP / turn count / enemy count / phase indicator (top-left)
 *   - Rolling message log (below stats)
 *   - Mode hint text (bottom, during targeting)
 *   - Entity HP bars drawn each frame over actor tiles
 *   - Red screen-flash filter on player hit
 *
 * Extracted from Game.ts (was: setupHUD, updateHUD, drawHealthBars,
 * setupEntityOverlay, hitFlashFrames).
 */

import { Application, Container, Graphics, Text, TextStyle, ColorMatrixFilter } from 'pixi.js';
import { World, Health, Position, EntityId, Ability } from '../core/ECS';
import { ParallaxAsciiRenderer } from './ParallaxAsciiRenderer';
import {
  HUD_FONT_SIZE, MSG_FONT_SIZE, MODE_FONT_SIZE,
  HUD_X, HUD_Y, MSG_Y, MODE_BOTTOM_OFFSET,
  MESSAGE_LOG_MAX, MESSAGE_LOG_VISIBLE,
  UI_FONT_FAMILY,
  FAB_RADIUS, FAB_OFFSET_X, FAB_OFFSET_Y,
  DPAD_OFFSET_X, DPAD_BOTTOM_OFFSET,
} from '../constants/ui';
import { HIT_FLASH_FRAMES } from '../constants/rendering';
import { hpColor } from '../utils/colors';
import { DPad } from '../ui/DPad';

export type GamePhase = 'player' | 'enemy' | 'game_over';
export type GameMode  = 'free'   | 'targeting';

// ─── HUD RENDERER ────────────────────────────────────────────────────────────

export class HUDRenderer {
  private hudText:  Text;
  private msgText:  Text;
  private modeText: Text;

  /** Overlay Graphics re-drawn every frame for HP bars. */
  private entityOverlayGfx: Graphics;

  /** ColorMatrixFilter boosting red on hit. Applied to renderer.root. */
  private hitFilter: ColorMatrixFilter;

  /** Frames remaining for the hit flash effect. */
  private hitFlashFrames = 0;

  /** Rolling message log (newest at index 0). */
  private messages: string[] = [];

  constructor(
    private readonly uiLayer:   Container,
    private readonly renderer:  ParallaxAsciiRenderer,
    private readonly app:       Application,
  ) {
    const hudStyle  = new TextStyle({ fontFamily: UI_FONT_FAMILY, fontSize: HUD_FONT_SIZE,  fill: 0xaaddff, fontWeight: 'bold' });
    const msgStyle  = new TextStyle({ fontFamily: UI_FONT_FAMILY, fontSize: MSG_FONT_SIZE,  fill: 0xccccaa });
    const modeStyle = new TextStyle({
      fontFamily: UI_FONT_FAMILY, fontSize: MODE_FONT_SIZE,
      fill: 0xffee88, fontWeight: 'bold',
      wordWrap: true, wordWrapWidth: 360,
    });

    this.hudText  = new Text({ text: '', style: hudStyle });
    this.msgText  = new Text({ text: '', style: msgStyle });
    this.modeText = new Text({ text: '', style: modeStyle });

    this.hudText.x  = HUD_X; this.hudText.y  = HUD_Y;
    this.msgText.x  = HUD_X; this.msgText.y  = MSG_Y;
    this.modeText.x = HUD_X; this.modeText.y = 0;   // updated each frame

    uiLayer.addChild(this.hudText);
    uiLayer.addChild(this.msgText);
    uiLayer.addChild(this.modeText);

    // Entity HP bar overlay (drawn on top of gameplay each frame)
    this.entityOverlayGfx = new Graphics();
    this.entityOverlayGfx.eventMode = 'none';
    uiLayer.addChild(this.entityOverlayGfx);

    // Hit-flash filter: boost red channel, dim green/blue
    this.hitFilter = new ColorMatrixFilter();
    this.hitFilter.matrix = [
      2,   0,   0, 0, 0,
      0, 0.3,   0, 0, 0,
      0,   0, 0.3, 0, 0,
      0,   0,   0, 1, 0,
    ];
  }

  // ── Message log ────────────────────────────────────────────────────────────

  addMessage(msg: string): void {
    this.messages.unshift(msg);
    if (this.messages.length > MESSAGE_LOG_MAX) this.messages.pop();
  }

  /** Signal that the player was hit — starts the screen-flash countdown. */
  triggerHitFlash(): void {
    this.hitFlashFrames = HIT_FLASH_FRAMES;
  }

  // ── Per-frame update ───────────────────────────────────────────────────────

  /**
   * Update all HUD text elements.
   * Call once per frame from the main loop.
   */
  update(
    world: World,
    playerId: EntityId,
    phase: GamePhase,
    mode: GameMode,
    selectedAbility: Ability | null,
    pendingTargetSet: boolean,
    dpad: DPad,
    fabGfx: Graphics,
    fabLabel: Text,
  ): void {
    const sw = this.app.screen.width;
    const sh = this.app.screen.height;

    // ── Stats line ──────────────────────────────────────────────────────────
    const hp = world.getComponent<Health>(playerId, 'health');
    const hpStr    = hp ? `HP: ${Math.max(0, Math.ceil(hp.current))}/${hp.max}` : 'HP: --';
    const phaseStr = phase === 'player' ? '[YOUR TURN]' : phase === 'enemy' ? '[ ... ]' : '[GAME OVER]';
    const enemies  = world.query('position', 'faction').filter(id =>
      world.getComponent<any>(id, 'faction')?.team === 'enemy'
    ).length;
    this.hudText.text = `${hpStr}   Enemies:${enemies}   ${phaseStr}`;

    // ── Message log ─────────────────────────────────────────────────────────
    this.msgText.text = this.messages.slice(0, MESSAGE_LOG_VISIBLE).join('\n');

    // ── Mode hint (bottom of screen) ─────────────────────────────────────
    this.modeText.y = sh - MODE_BOTTOM_OFFSET;
    this.modeText.x = HUD_X;
    (this.modeText.style as TextStyle).wordWrapWidth = sw - 20;

    if (mode === 'targeting' && selectedAbility) {
      const needsPreview = selectedAbility.pattern === 'aoe' || selectedAbility.pattern === 'line';
      if (needsPreview && pendingTargetSet) {
        this.modeText.text = `▶ ${selectedAbility.name}  •  tap again to cast\ntap your tile (@) to cancel`;
      } else {
        this.modeText.text = `▶ ${selectedAbility.name}  •  tap target tile\ntap your tile (@) to cancel`;
      }
    } else {
      this.modeText.text = '';
    }

    // ── FAB position (bottom-right) ─────────────────────────────────────
    fabGfx.x   = sw - FAB_OFFSET_X;
    fabGfx.y   = sh - FAB_OFFSET_Y;
    fabLabel.x = sw - FAB_OFFSET_X;
    fabLabel.y = sh - FAB_OFFSET_Y;

    // ── D-Pad position (bottom-left) ─────────────────────────────────────
    dpad.resize(DPAD_OFFSET_X, sh - DPAD_BOTTOM_OFFSET);
  }

  // ── HP bars ───────────────────────────────────────────────────────────────

  /**
   * Draw small HP bars beneath each non-player entity at less than full HP.
   * Re-drawn every frame so bars track camera movement automatically.
   */
  drawHealthBars(world: World, playerId: EntityId): void {
    const overlay = this.entityOverlayGfx;
    overlay.clear();

    const { x: ox, y: oy, cellSize } = this.renderer.getLayerScreenOffset('gameplay');

    for (const id of world.query('position', 'health', 'faction')) {
      if (id === playerId) continue;
      const pos = world.getComponent<Position>(id, 'position')!;
      const hp  = world.getComponent<Health>(id, 'health')!;
      if (hp.max <= 0) continue;

      const ratio = Math.max(0, Math.min(1, hp.current / hp.max));
      if (ratio >= 1.0) continue; // hide at full HP

      const barW  = cellSize - 2;
      const barH  = 2;
      const barX  = ox + pos.col * cellSize + 1;
      const barY  = oy + pos.row * cellSize + cellSize - 3;
      const color = ratio > 0.6 ? 0x44dd44 : ratio > 0.3 ? 0xffcc00 : 0xff3333;

      overlay.rect(barX, barY - 1, barW, barH + 2).fill({ color: 0x000000, alpha: 0.7 });
      overlay.rect(barX, barY, Math.round(barW * ratio), barH).fill({ color });
    }
  }

  // ── Hit flash ─────────────────────────────────────────────────────────────

  /**
   * Apply or remove the hit-flash filter on the parallax root.
   * Call once per frame from the main loop.
   */
  tickHitFlash(): void {
    if (this.hitFlashFrames > 0) {
      this.renderer.root.filters = [this.hitFilter];
      this.hitFlashFrames--;
    } else if (this.renderer.root.filters?.length) {
      this.renderer.root.filters = [];
    }
  }
}
