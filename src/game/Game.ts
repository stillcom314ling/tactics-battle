/**
 * GAME
 *
 * Top-level orchestrator. Initialises PixiJS, parallax renderer, ECS world,
 * map generation, and the game loop.
 *
 * Turn flow: player acts via radial wheel → enemy phase → environment ticks → repeat
 * No bump-to-attack. All combat is explicit through the spell system.
 *
 * Game modes:
 *   free      — player can move or open wheel
 *   targeting — spell selected, tap tile to cast (tap own tile to cancel)
 *   game_over — display only
 */

import { Application, Text, TextStyle, Graphics, Container, ColorMatrixFilter } from 'pixi.js';
import * as ROT from 'rot-js';
import { ParallaxAsciiRenderer } from '../rendering/ParallaxAsciiRenderer';
import { CameraController } from '../rendering/CameraController';
import { World, Position, Renderable, Health, Faction, Combat, Ability, Abilities, StatusEffect, AI, tickStatuses } from '../core/ECS';
import { TooltipOverlay } from '../ui/TooltipOverlay';
import { DPad } from '../ui/DPad';
import { ActionMenu, ActiveItem } from '../ui/ActionMenu';
import { generateMap, populateWorld, GeneratedMap } from './MapGenerator';
import { processInteractions } from '../core/InteractionSystem';
import { resolveSpell, getRangeTiles, getSpellTiles } from './SpellSystem';

const MAP_W     = 32;
const MAP_H     = 22;
const CELL_SIZE = 24;

// Element → highlight colour for targeting mode
const ELEMENT_COLOR: Record<string, number> = {
  fire:      0xff5500,
  lightning: 0xffee00,
  ice:       0x44ccff,
  poison:    0x88ff44,
  arcane:    0xcc44ff,
  none:      0xffffff,
};

type GameMode  = 'free' | 'targeting';
type GamePhase = 'player' | 'enemy' | 'game_over';

/** Linearly interpolate between two 0xRRGGBB hex colors. */
function lerpColor(a: number, b: number, t: number): number {
  const ar = (a >> 16) & 0xff, ag = (a >> 8) & 0xff, ab = a & 0xff;
  const br = (b >> 16) & 0xff, bg = (b >> 8) & 0xff, bb = b & 0xff;
  const r = Math.round(ar + (br - ar) * t);
  const g = Math.round(ag + (bg - ag) * t);
  const bv = Math.round(ab + (bb - ab) * t);
  return (r << 16) | (g << 8) | bv;
}

export class Game {
  private app!: Application;
  private renderer!: ParallaxAsciiRenderer;
  private camera!: CameraController;
  private world!: World;
  private playerId!: number;
  private mapData!: GeneratedMap;

  private phase: GamePhase  = 'player';
  private mode:  GameMode   = 'free';
  private selectedAbility: Ability | null = null;
  private highlightTiles: [number, number][] = [];
  private spellPreviewTiles: [number, number][] = [];
  private pendingTargetCol: number | null = null;
  private pendingTargetRow: number | null = null;

  private turnCount: number = 0;
  private messages: string[] = [];

  private hudText!: Text;
  private msgText!: Text;
  private modeText!: Text;   // shows current mode / targeting hint
  private actionBtnGfx!: Graphics; // action button (bottom-right)
  private actionBtnLabel!: Text;
  private uiLayer!: Container; // fixed overlay (not in parallax)

  private dpad!: DPad;
  private actionMenu!: ActionMenu;
  private menuJustOpened = false;
  private tooltip!: TooltipOverlay;
  private entityOverlayGfx!: Graphics; // per-entity HP bars drawn each frame
  private hitFilter!: ColorMatrixFilter;
  private hitFlashFrames: number = 0;

  constructor(private container: HTMLElement) {}

  // ─── INIT ──────────────────────────────────────────────────────────────────

  async init() {
    this.app = new Application();
    await this.app.init({
      resizeTo: this.container,
      backgroundColor: 0x0a0a0c,
      antialias: false,
      resolution: window.devicePixelRatio || 1,
      autoDensity: true,
    });
    this.container.appendChild(this.app.canvas);

    this.camera = new CameraController(this.app.canvas as HTMLElement);

    this.renderer = new ParallaxAsciiRenderer();
    this.app.stage.addChild(this.renderer.root);

    // Parallax layers (far → near)
    const bgScale = 1.4;
    this.renderer.addLayer({ id: 'bg_far',    depth: 0.2, cols: Math.ceil(MAP_W * bgScale), rows: Math.ceil(MAP_H * bgScale), cellSize: CELL_SIZE, alpha: 0.4, tint: 0x8888aa });
    this.renderer.addLayer({ id: 'bg_mid',    depth: 0.5, cols: Math.ceil(MAP_W * bgScale), rows: Math.ceil(MAP_H * bgScale), cellSize: CELL_SIZE, alpha: 0.5, tint: 0x9999bb });
    this.renderer.addLayer({ id: 'gameplay',  depth: 1.0, cols: MAP_W,                       rows: MAP_H,                       cellSize: CELL_SIZE, alpha: 1.0 });
    this.renderer.addLayer({ id: 'foreground',depth: 1.3, cols: Math.ceil(MAP_W * 1.2),      rows: Math.ceil(MAP_H * 1.2),      cellSize: CELL_SIZE, alpha: 0.3 });

    // World + map
    this.world   = new World();
    this.mapData = generateMap(MAP_W, MAP_H);

    this.renderer.setLayerData('bg_far',     this.mapData.bgFar);
    this.renderer.setLayerData('bg_mid',     this.mapData.bgMid);
    this.renderer.setLayerData('foreground', this.mapData.foreground);
    this.renderer.setLayerData('gameplay',   this.mapData.gameplay);

    const { playerId } = populateWorld(this.world, this.mapData);
    this.playerId = playerId;

    // Init overlay graphics before drawActors() so the first call doesn't crash
    this.entityOverlayGfx = new Graphics();
    this.entityOverlayGfx.eventMode = 'none';
    // (added to uiLayer later in setupEntityOverlay after uiLayer is created)

    this.drawActors();

    // Center camera on player
    const startPos = this.world.getComponent<Position>(this.playerId, 'position')!;
    this.camera.state.x = (startPos.col - MAP_W / 2) * CELL_SIZE;
    this.camera.state.y = (startPos.row - MAP_H / 2) * CELL_SIZE;

    // Fixed UI overlay (sits above parallax, unaffected by camera)
    this.uiLayer = new Container();
    this.app.stage.addChild(this.uiLayer);

    this.setupHUD();
    this.setupActionButton();
    this.setupActionMenu();
    this.setupDPad();
    this.setupTooltip();
    this.setupInput();
    this.setupEntityOverlay();

    this.addMessage('Use d-pad to move. Tap ☰ for actions.');
    this.addMessage('Tap an enemy or yourself to inspect.');
  }

  // ─── HUD ───────────────────────────────────────────────────────────────────

  private setupHUD() {
    const hudStyle  = new TextStyle({ fontFamily: '"Courier New", monospace', fontSize: 18, fill: 0xaaddff, fontWeight: 'bold' });
    const msgStyle  = new TextStyle({ fontFamily: '"Courier New", monospace', fontSize: 15, fill: 0xccccaa });
    const modeStyle = new TextStyle({ fontFamily: '"Courier New", monospace', fontSize: 16, fill: 0xffee88, fontWeight: 'bold', wordWrap: true, wordWrapWidth: 360 });

    this.hudText  = new Text({ text: '', style: hudStyle });
    this.msgText  = new Text({ text: '', style: msgStyle });
    this.modeText = new Text({ text: '', style: modeStyle });

    this.hudText.x  = 10; this.hudText.y  = 10;
    this.msgText.x  = 10; this.msgText.y  = 36;
    this.modeText.x = 10; this.modeText.y = 0; // positioned in loop

    this.uiLayer.addChild(this.hudText);
    this.uiLayer.addChild(this.msgText);
    this.uiLayer.addChild(this.modeText);
  }

  private updateHUD() {
    const hp = this.world.getComponent<Health>(this.playerId, 'health');
    const hpStr = hp ? `HP: ${Math.max(0, Math.ceil(hp.current))}/${hp.max}` : 'HP: --';
    const phaseStr = this.phase === 'player' ? '[YOUR TURN]' : this.phase === 'enemy' ? '[ ... ]' : '[GAME OVER]';
    const enemies = this.world.query('position', 'faction').filter(id => {
      return this.world.getComponent<Faction>(id, 'faction')?.team === 'enemy';
    }).length;

    this.hudText.text = `${hpStr}   T:${this.turnCount}   Enemies:${enemies}   ${phaseStr}`;
    this.msgText.text = this.messages.slice(0, 3).join('\n');

    // Mode hint at bottom
    const sw = this.app.screen.width, sh = this.app.screen.height;
    this.modeText.y = sh - 100;
    this.modeText.x = 10;
    (this.modeText.style as TextStyle).wordWrapWidth = sw - 20;
    if (this.mode === 'targeting' && this.selectedAbility) {
      const a = this.selectedAbility;
      const needsPreview = a.pattern === 'aoe' || a.pattern === 'line';
      if (needsPreview && this.pendingTargetCol !== null) {
        this.modeText.text = `▶ ${a.name}  •  tap again to cast\ntap your tile (@) to cancel`;
      } else {
        this.modeText.text = `▶ ${a.name}  •  tap target tile\ntap your tile (@) to cancel`;
      }
    } else {
      this.modeText.text = '';
    }

    // Action button position (bottom-right, above thumb area)
    if (this.actionBtnGfx) {
      this.actionBtnGfx.x = sw - 60;
      this.actionBtnGfx.y = sh - 70;
      this.actionBtnLabel.x = sw - 60;
      this.actionBtnLabel.y = sh - 70;
    }
    // D-pad position (bottom-left)
    if (this.dpad) {
      this.dpad.resize(20, sh - 138);
    }
  }

  private addMessage(msg: string) {
    this.messages.unshift(msg);
    if (this.messages.length > 6) this.messages.pop();
  }

  // ─── ACTION BUTTON (bottom-right) ─────────────────────────────────────────

  private setupActionButton() {
    this.actionBtnGfx = new Graphics();
    this.actionBtnGfx.circle(0, 0, 34).fill({ color: 0x112244, alpha: 0.92 }).stroke({ color: 0x3366bb, alpha: 0.85, width: 2.5 });
    this.actionBtnGfx.eventMode = 'static';
    this.actionBtnGfx.cursor    = 'pointer';
    this.actionBtnGfx.on('pointerdown', (e) => {
      e.stopPropagation();
      if (this.phase !== 'player' || this.mode !== 'free') return;
      if (this.actionMenu.isOpen) { this.actionMenu.close(); return; }
      this.tooltip.hide();
      const sw = this.app.screen.width, sh = this.app.screen.height;
      this.menuJustOpened = true;
      requestAnimationFrame(() => requestAnimationFrame(() => { this.menuJustOpened = false; }));
      this.actionMenu.open(this.buildActiveItems(), sw, sh, () => {
        this.camera.disabled = false;
      });
      this.camera.disabled = true;
    });

    const style = new TextStyle({ fontFamily: '"Courier New", monospace', fontSize: 20, fill: 0x6699ff, fontWeight: 'bold' });
    this.actionBtnLabel = new Text({ text: '☰', style });
    this.actionBtnLabel.anchor.set(0.5);
    this.actionBtnLabel.eventMode = 'none';

    this.uiLayer.addChild(this.actionBtnGfx);
    this.uiLayer.addChild(this.actionBtnLabel);
  }

  // ─── ACTION MENU ──────────────────────────────────────────────────────────

  private setupActionMenu() {
    this.actionMenu = new ActionMenu();
    this.uiLayer.addChild(this.actionMenu.container);
  }

  // ─── D-PAD ────────────────────────────────────────────────────────────────

  private setupDPad() {
    this.dpad = new DPad(
      (dx, dy) => this.movePlayerInDirection(dx, dy),
      () => {
        if (this.phase !== 'player' || this.mode !== 'free') return;
        this.addMessage('You wait.');
        this.endPlayerTurn();
      },
    );
    this.uiLayer.addChild(this.dpad.container);
  }

  /** Move player one step in direction (dx, dy). Called by DPad. */
  private movePlayerInDirection(dx: number, dy: number) {
    if (this.phase !== 'player' || this.mode !== 'free') return;
    const playerStatus = this.world.getComponent<StatusEffect>(this.playerId, 'status');
    if (playerStatus?.effects.has('stunned')) {
      this.addMessage('You are stunned and lose your turn!');
      this.endPlayerTurn();
      return;
    }
    const pos = this.world.getComponent<Position>(this.playerId, 'position')!;
    const nc = pos.col + dx;
    const nr = pos.row + dy;
    if (nc < 0 || nc >= MAP_W || nr < 0 || nr >= MAP_H) return;
    if (!this.mapData.walkable[nr]?.[nc]) return;
    if (this.getActorAt(nc, nr, 'enemy') !== null) return; // no bump-attack
    this.tooltip.hide();
    this.moveEntity(this.playerId, nc, nr);
    this.endPlayerTurn();
  }

  private setupTooltip() {
    this.tooltip = new TooltipOverlay();
    this.uiLayer.addChild(this.tooltip.container);
  }

  private setupEntityOverlay() {
    // entityOverlayGfx was pre-created before drawActors(); just add it to uiLayer now
    this.uiLayer.addChild(this.entityOverlayGfx);

    // Red screen-flash filter applied to the parallax root on player hit
    this.hitFilter = new ColorMatrixFilter();
    // Boost red, suppress green/blue
    this.hitFilter.matrix = [
      2,   0,   0, 0, 0,
      0, 0.3,   0, 0, 0,
      0,   0, 0.3, 0, 0,
      0,   0,   0, 1, 0,
    ];
  }

  private buildActiveItems(): ActiveItem[] {
    const abilities = this.world.getComponent<Abilities>(this.playerId, 'abilities');
    if (!abilities) return [];
    return abilities.list.map(a => ({
      id:         a.id,
      name:       a.name,
      element:    a.element,
      charges:    a.charges,
      chargesMax: a.chargesMax,
      color:      ELEMENT_COLOR[a.element] ?? 0x888888,
      tooltip:    `DMG ${a.damage}  RNG ${a.range}${a.effects.length ? '  ' + a.effects.join(', ') : ''}`,
      action:     () => this.enterTargeting(a),
    }));
  }

  // ─── TARGETING MODE ───────────────────────────────────────────────────────

  private enterTargeting(ability: Ability) {
    this.mode = 'targeting';
    this.selectedAbility = ability;
    this.showRangeHighlight(ability);
    this.addMessage(`Select target for ${ability.name}. Tap yourself to cancel.`);
  }

  private cancelTargeting() {
    this.clearRangeHighlight(); // also clears spellPreview + pending
    this.mode = 'free';
    this.selectedAbility = null;
    this.addMessage('Cancelled.');
  }

  private showRangeHighlight(ability: Ability) {
    const pos   = this.world.getComponent<Position>(this.playerId, 'position')!;
    const color = ELEMENT_COLOR[ability.element] ?? 0xffffff;
    const allTiles = getRangeTiles(pos.col, pos.row, ability.range, this.mapData);

    // All walkable tiles in range are valid targets (including enemy tiles).
    this.highlightTiles = allTiles.filter(([c, r]) => this.mapData.walkable[r]?.[c]);

    for (const [c, r] of this.highlightTiles) {
      // If an enemy stands here, overlay their glyph with the spell colour.
      const actor = this.getActorAt(c, r, 'enemy');
      if (actor !== null) {
        const rend = this.world.getComponent<Renderable>(actor, 'renderable')!;
        this.renderer.setCell('gameplay', c, r, { char: rend.char, fg: color, alpha: 0.9 });
      } else {
        this.renderer.setCell('gameplay', c, r, { char: '·', fg: color, alpha: 0.55 });
      }
    }
  }

  private clearRangeHighlight() {
    this.clearSpellPreview();
    for (const [c, r] of this.highlightTiles) this.restoreTerrain(c, r);
    this.drawActors();
    this.highlightTiles = [];
  }

  /** Show blast-radius preview for aoe/line spells centered on (col, row). */
  private showSpellPreview(col: number, row: number) {
    this.clearSpellPreview();
    if (!this.selectedAbility) return;
    const { pattern } = this.selectedAbility;
    if (pattern !== 'aoe' && pattern !== 'line') return;

    const pos   = this.world.getComponent<Position>(this.playerId, 'position')!;
    const color = ELEMENT_COLOR[this.selectedAbility.element] ?? 0xffffff;
    const tiles = getSpellTiles(pos.col, pos.row, col, row, this.selectedAbility, this.mapData);

    for (const [c, r] of tiles) {
      this.spellPreviewTiles.push([c, r]);
      const actor = this.getActorAt(c, r, 'enemy');
      if (actor !== null) {
        const rend = this.world.getComponent<Renderable>(actor, 'renderable')!;
        this.renderer.setCell('gameplay', c, r, { char: rend.char, fg: color, alpha: 1.0 });
      } else {
        this.renderer.setCell('gameplay', c, r, { char: '*', fg: color, alpha: 0.85 });
      }
    }
  }

  /** Restore preview tiles back to range highlight or terrain. */
  private clearSpellPreview() {
    for (const [c, r] of this.spellPreviewTiles) {
      const inRange = this.highlightTiles.some(([hc, hr]) => hc === c && hr === r);
      if (inRange) {
        const actor = this.getActorAt(c, r, 'enemy');
        const color = ELEMENT_COLOR[this.selectedAbility?.element ?? 'none'] ?? 0xffffff;
        if (actor !== null) {
          const rend = this.world.getComponent<Renderable>(actor, 'renderable')!;
          this.renderer.setCell('gameplay', c, r, { char: rend.char, fg: color, alpha: 0.9 });
        } else {
          this.renderer.setCell('gameplay', c, r, { char: '·', fg: color, alpha: 0.55 });
        }
      } else {
        this.restoreTerrain(c, r);
        // Redraw any actor on this tile
        const actor = this.getActorAt(c, r);
        if (actor !== null) {
          const pos  = this.world.getComponent<Position>(actor, 'position')!;
          const rend = this.world.getComponent<Renderable>(actor, 'renderable')!;
          this.renderer.setCell('gameplay', pos.col, pos.row, { char: rend.char, fg: rend.fg, alpha: rend.alpha ?? 1.0 });
        }
      }
    }
    this.spellPreviewTiles = [];
    this.pendingTargetCol  = null;
    this.pendingTargetRow  = null;
  }

  // ─── INPUT ────────────────────────────────────────────────────────────────

  private setupInput() {
    const canvas = this.app.canvas as HTMLCanvasElement;
    const toCanvas = (clientX: number, clientY: number) => {
      const rect  = canvas.getBoundingClientRect();
      const scale = this.app.screen.width / rect.width;
      return [(clientX - rect.left) * scale, (clientY - rect.top) * scale] as const;
    };

    // ── Map taps (inspect / cast) ──────────────────────────────────────
    canvas.addEventListener('click', (e) => {
      if (this.camera.hasDragged || this.menuJustOpened) return;
      if (this.actionMenu.isOpen) { this.actionMenu.close(); return; }
      const [x, y] = toCanvas(e.clientX, e.clientY);
      this.handleTap(x, y);
    });

    let touchStartX = 0, touchStartY = 0;
    canvas.addEventListener('touchstart', (e) => {
      const t = e.touches[0];
      touchStartX = t.clientX; touchStartY = t.clientY;
    }, { passive: true });

    canvas.addEventListener('touchend', (e) => {
      if (this.camera.hasDragged || this.menuJustOpened) return;
      const t = e.changedTouches[0];
      if (Math.hypot(t.clientX - touchStartX, t.clientY - touchStartY) > 8) return;
      if (this.actionMenu.isOpen) { this.actionMenu.close(); return; }
      const [x, y] = toCanvas(t.clientX, t.clientY);
      this.handleTap(x, y);
    }, { passive: true });
  }

  private screenToTile(screenX: number, screenY: number): [number, number] {
    const layerOriginX = (this.app.screen.width  - MAP_W * CELL_SIZE) / 2 - this.camera.state.x;
    const layerOriginY = (this.app.screen.height - MAP_H * CELL_SIZE) / 2 - this.camera.state.y;
    const col = Math.floor((screenX - layerOriginX) / CELL_SIZE);
    const row = Math.floor((screenY - layerOriginY) / CELL_SIZE);
    return [col, row];
  }

  private handleTap(screenX: number, screenY: number) {
    if (this.phase === 'game_over') return;
    if (this.phase !== 'player') return;

    // Stunned player loses their turn immediately
    const playerStatus = this.world.getComponent<StatusEffect>(this.playerId, 'status');
    if (playerStatus?.effects.has('stunned')) {
      this.addMessage('You are stunned and lose your turn!');
      this.endPlayerTurn();
      return;
    }

    const [col, row] = this.screenToTile(screenX, screenY);
    if (col < 0 || col >= MAP_W || row < 0 || row >= MAP_H) return;

    const playerPos = this.world.getComponent<Position>(this.playerId, 'position')!;

    if (this.mode === 'targeting') {
      this.tooltip.hide();
      // Tap own tile = cancel
      if (col === playerPos.col && row === playerPos.row) {
        this.cancelTargeting();
        return;
      }
      // Check range
      const dist = Math.sqrt((col - playerPos.col) ** 2 + (row - playerPos.row) ** 2);
      if (dist > this.selectedAbility!.range) {
        this.addMessage('Out of range.');
        return;
      }

      const needsPreview = this.selectedAbility!.pattern === 'aoe' || this.selectedAbility!.pattern === 'line';
      if (needsPreview) {
        const sameTile = col === this.pendingTargetCol && row === this.pendingTargetRow;
        if (!sameTile) {
          // First tap: show preview and wait for second tap
          this.pendingTargetCol = col;
          this.pendingTargetRow = row;
          this.showSpellPreview(col, row);
          this.addMessage(`Tap again to cast ${this.selectedAbility!.name}.`);
          return;
        }
        // Second tap on same tile: cast
      }

      this.clearRangeHighlight();
      resolveSpell(this.world, this.playerId, this.selectedAbility!, col, row, this.mapData, this.addMessage.bind(this));
      this.alertEnemies(playerPos.col, playerPos.row, 5);
      this.mode = 'free';
      this.selectedAbility = null;
      this.drawActors();
      this.endPlayerTurn();
      return;
    }

    // free mode — tap own tile → inspect self
    if (col === playerPos.col && row === playerPos.row) {
      const sw = this.app.screen.width, sh = this.app.screen.height;
      this.tooltip.show(this.world, this.playerId, screenX, screenY, sw, sh);
      return;
    }

    // Tap enemy → inspect (tooltip), not attack
    const enemy = this.getActorAt(col, row, 'enemy');
    if (enemy !== null) {
      const sw = this.app.screen.width, sh = this.app.screen.height;
      this.tooltip.show(this.world, enemy, screenX, screenY, sw, sh);
      return;
    }

    // Tap floor → hide tooltip, pathfind & move one step
    this.tooltip.hide();
    if (!this.mapData.walkable[row]?.[col]) return;
    const path = this.findPath(playerPos.col, playerPos.row, col, row, this.playerId);
    if (path.length < 2) return;
    this.moveEntity(this.playerId, path[1][0], path[1][1]);
    this.endPlayerTurn();
  }

  // ─── TURN FLOW ────────────────────────────────────────────────────────────

  private endPlayerTurn() {
    this.turnCount++;
    this.phase = 'enemy';
    // Run environment interactions (burning damage, poison, etc.) at turn start
    for (const id of this.world.query('status', 'position')) {
      processInteractions(this.world, id, { trigger: 'turn_start', addMessage: this.addMessage.bind(this) });
    }
    setTimeout(() => this.processEnemyTurns(), 140);
  }

  private processEnemyTurns() {
    if (!this.world.hasComponent(this.playerId, 'health')) {
      this.phase = 'game_over';
      return;
    }

    const playerPos = this.world.getComponent<Position>(this.playerId, 'position')!;
    const enemies = this.world.query('position', 'faction', 'health', 'combat').filter(id =>
      this.world.getComponent<Faction>(id, 'faction')?.team === 'enemy'
    );

    for (const enemyId of enemies) {
      if (!this.world.hasComponent(this.playerId, 'health')) break;

      const status = this.world.getComponent<StatusEffect>(enemyId, 'status');
      // Stunned: skip entire turn
      if (status?.effects.has('stunned')) continue;

      const ai = this.world.getComponent<AI>(enemyId, 'ai');
      const strategy = ai?.strategy ?? 'basic';

      switch (strategy) {
        case 'ranged': this.enemyTurn_ranged(enemyId, playerPos, ai!, status); break;
        case 'brute':  this.enemyTurn_brute(enemyId, playerPos, ai!, status);  break;
        case 'swarm':  this.enemyTurn_basic(enemyId, playerPos, status);       break;
        default:       this.enemyTurn_basic(enemyId, playerPos, status);       break;
      }
    }

    // Tick status durations for all entities at end of round
    for (const id of this.world.query('status')) {
      const s = this.world.getComponent<StatusEffect>(id, 'status')!;
      tickStatuses(s);
    }

    this.drawActors();
    this.phase = this.world.hasComponent(this.playerId, 'health') ? 'player' : 'game_over';
    if (this.phase === 'game_over') this.addMessage('You have died. Refresh to play again.');
  }

  private enemyTurn_basic(
    enemyId: number,
    playerPos: Position,
    status: StatusEffect | undefined,
  ) {
    const ePos = this.world.getComponent<Position>(enemyId, 'position')!;
    const dx = Math.abs(playerPos.col - ePos.col);
    const dy = Math.abs(playerPos.row - ePos.row);
    const slowed = status?.effects.has('slowed') ?? false;

    if (dx <= 1 && dy <= 1) {
      this.enemyMelee(enemyId, this.playerId);
    } else if (!slowed) {
      const path = this.findPath(ePos.col, ePos.row, playerPos.col, playerPos.row, enemyId);
      if (path.length >= 2) {
        const [nc, nr] = path[1];
        if (nc !== playerPos.col || nr !== playerPos.row) this.moveEntity(enemyId, nc, nr);
      }
    }
  }

  private enemyTurn_ranged(
    enemyId: number,
    playerPos: Position,
    ai: AI,
    status: StatusEffect | undefined,
  ) {
    const ePos = this.world.getComponent<Position>(enemyId, 'position')!;
    const distChebyshev = Math.max(Math.abs(playerPos.col - ePos.col), Math.abs(playerPos.row - ePos.row));
    const distEucl = Math.hypot(playerPos.col - ePos.col, playerPos.row - ePos.row);
    const slowed = status?.effects.has('slowed') ?? false;

    // Too close and not alerted: back away
    if (distChebyshev <= 2 && !ai.alerted && !slowed) {
      const fc = ePos.col + Math.sign(ePos.col - playerPos.col);
      const fr = ePos.row + Math.sign(ePos.row - playerPos.row);
      if (this.mapData.walkable[fr]?.[fc] && this.getActorAt(fc, fr) === null) {
        this.moveEntity(enemyId, fc, fr);
        return;
      }
    }

    // In range — try ranged attack
    if (distEucl <= 5) {
      const hit = this.enemyRangedAttack(enemyId, this.playerId, 5);
      if (hit) return;
    }

    // Out of range or shot blocked — approach (unless slowed)
    if (!slowed) {
      const path = this.findPath(ePos.col, ePos.row, playerPos.col, playerPos.row, enemyId);
      if (path.length >= 2) {
        const [nc, nr] = path[1];
        if (nc !== playerPos.col || nr !== playerPos.row) this.moveEntity(enemyId, nc, nr);
      }
    }
  }

  private enemyTurn_brute(
    enemyId: number,
    playerPos: Position,
    ai: AI,
    status: StatusEffect | undefined,
  ) {
    ai.moveCounter = (ai.moveCounter ?? 0) + 1;
    // Brute acts only every other turn (sluggish)
    if (ai.moveCounter % 2 !== 0) return;

    const ePos = this.world.getComponent<Position>(enemyId, 'position')!;
    const dx = Math.abs(playerPos.col - ePos.col);
    const dy = Math.abs(playerPos.row - ePos.row);
    const slowed = status?.effects.has('slowed') ?? false;

    if (dx <= 1 && dy <= 1) {
      this.enemyMelee(enemyId, this.playerId);
    } else if (!slowed) {
      const path = this.findPath(ePos.col, ePos.row, playerPos.col, playerPos.row, enemyId);
      if (path.length >= 2) {
        const [nc, nr] = path[1];
        if (nc !== playerPos.col || nr !== playerPos.row) this.moveEntity(enemyId, nc, nr);
      }
    }
  }

  // ─── ALERT SYSTEM ─────────────────────────────────────────────────────────

  /** Wake enemies within Chebyshev distance `radius` — they will rush the player. */
  private alertEnemies(sourceCol: number, sourceRow: number, radius: number) {
    let anyNewAlert = false;
    for (const id of this.world.query('position', 'faction', 'ai')) {
      if (this.world.getComponent<Faction>(id, 'faction')?.team !== 'enemy') continue;
      const ai = this.world.getComponent<AI>(id, 'ai')!;
      if (ai.alerted) continue;
      const pos = this.world.getComponent<Position>(id, 'position')!;
      const dist = Math.max(Math.abs(pos.col - sourceCol), Math.abs(pos.row - sourceRow));
      if (dist <= radius) {
        ai.alerted = true;
        anyNewAlert = true;
      }
    }
    if (anyNewAlert) this.addMessage('The enemies are alerted!');
  }

  // ─── COMBAT ───────────────────────────────────────────────────────────────

  /** Enemy basic melee strike using their Combat component. */
  private enemyMelee(attackerId: number, defenderId: number) {
    const atk = this.world.getComponent<Combat>(attackerId, 'combat')!;
    const defCombat = this.world.getComponent<Combat>(defenderId, 'combat');
    const defHealth = this.world.getComponent<Health>(defenderId, 'health');
    if (!defHealth) return;

    const def = defCombat?.defense ?? 0;
    const dmg = Math.max(1, atk.attackPower + Math.floor(Math.random() * 4) - def);
    defHealth.current -= dmg;
    this.addMessage(`Enemy hits you for ${dmg}!`);
    this.hitFlashFrames = 4; // red screen flash

    // Melee contact alerts nearby enemies
    const aPos = this.world.getComponent<Position>(attackerId, 'position');
    if (aPos) this.alertEnemies(aPos.col, aPos.row, 4);

    if (defHealth.current <= 0) {
      this.addMessage('YOU DIED. Refresh to restart.');
      this.phase = 'game_over';
      this.world.removeComponent(this.playerId, 'health');
    }
  }

  /** Ranged line-of-sight attack from enemy to target. */
  private enemyRangedAttack(attackerId: number, targetId: number, damage: number): boolean {
    const aPos = this.world.getComponent<Position>(attackerId, 'position')!;
    const tPos = this.world.getComponent<Position>(targetId, 'position');
    const tHealth = this.world.getComponent<Health>(targetId, 'health');
    if (!tPos || !tHealth) return false;

    // Simple line-of-sight: check no walls block the direct line
    const dx = Math.sign(tPos.col - aPos.col);
    const dy = Math.sign(tPos.row - aPos.row);
    // Only allow cardinal + diagonal directions for simplicity
    if (dx !== 0 && dy !== 0 && Math.abs(tPos.col - aPos.col) !== Math.abs(tPos.row - aPos.row)) {
      // Non-diagonal non-cardinal — use proximity check only
    }

    let c = aPos.col + dx, r = aPos.row + dy;
    while (c !== tPos.col || r !== tPos.row) {
      if (!this.mapData.walkable[r]?.[c]) return false; // wall blocks shot
      c += dx; r += dy;
    }

    const def = this.world.getComponent<Combat>(targetId, 'combat')?.defense ?? 0;
    const dmg = Math.max(1, damage - def);
    tHealth.current -= dmg;
    this.addMessage(`Archer shoots you for ${dmg}!`);
    this.hitFlashFrames = 4; // red screen flash
    this.alertEnemies(aPos.col, aPos.row, 4);

    if (tHealth.current <= 0) {
      this.addMessage('YOU DIED. Refresh to restart.');
      this.phase = 'game_over';
      this.world.removeComponent(this.playerId, 'health');
    }
    return true;
  }

  // ─── MOVEMENT & RENDERING ─────────────────────────────────────────────────

  private moveEntity(entityId: number, newCol: number, newRow: number) {
    const pos  = this.world.getComponent<Position>(entityId, 'position')!;
    const rend = this.world.getComponent<Renderable>(entityId, 'renderable')!;

    this.restoreTerrain(pos.col, pos.row);
    pos.col = newCol;
    pos.row = newRow;

    this.renderer.setCell('gameplay', newCol, newRow, {
      char: rend.char, fg: rend.fg, alpha: rend.alpha ?? 1.0,
    });

    if (entityId === this.playerId) {
      this.camera.state.x = (newCol - MAP_W / 2) * CELL_SIZE;
      this.camera.state.y = (newRow - MAP_H / 2) * CELL_SIZE;
      this.camera.state.velocityX = 0;
      this.camera.state.velocityY = 0;
    }
  }

  private restoreTerrain(col: number, row: number) {
    this.renderer.setCell('gameplay', col, row, this.mapData.gameplay[row]?.[col] ?? null);
  }

  private drawActors() {
    for (const id of this.world.query('position', 'renderable', 'faction')) {
      const pos    = this.world.getComponent<Position>(id, 'position')!;
      const rend   = this.world.getComponent<Renderable>(id, 'renderable')!;
      const status = this.world.getComponent<StatusEffect>(id, 'status');
      const hp     = this.world.getComponent<Health>(id, 'health');

      // Glyph color: status > HP-tint > base
      let fg = rend.fg;
      const hasStatus = status && status.effects.size > 0;
      if (hasStatus) {
        if (status!.effects.has('burning'))  fg = 0xff5500;
        if (status!.effects.has('shocked'))  fg = 0xffff00;
        if (status!.effects.has('slowed'))   fg = 0x88ccff;
        if (status!.effects.has('poisoned')) fg = 0x66ff44;
      } else if (hp && hp.max > 0) {
        const ratio = Math.max(0, hp.current / hp.max);
        if (ratio < 0.6) {
          fg = lerpColor(rend.fg, 0x993333, (1 - ratio / 0.6) * 0.55);
        }
      }
      this.renderer.setCell('gameplay', pos.col, pos.row, { char: rend.char, fg, alpha: rend.alpha ?? 1.0 });
    }
  }

  /** Redrawn every frame — HP bars follow camera movement since they use live screen coords. */
  private drawHealthBars() {
    const overlay = this.entityOverlayGfx;
    overlay.clear();
    const { x: ox, y: oy, cellSize } = this.renderer.getLayerScreenOffset('gameplay');

    for (const id of this.world.query('position', 'health', 'faction')) {
      if (id === this.playerId) continue;
      const pos = this.world.getComponent<Position>(id, 'position')!;
      const hp  = this.world.getComponent<Health>(id, 'health')!;
      if (hp.max <= 0) continue;
      const ratio = Math.max(0, Math.min(1, hp.current / hp.max));
      if (ratio >= 1.0) continue; // hide at full HP

      const barW    = cellSize - 2;
      const barH    = 2;
      const barX    = ox + pos.col * cellSize + 1;
      const barY    = oy + pos.row * cellSize + cellSize - 3;
      const barColor = ratio > 0.6 ? 0x44dd44 : ratio > 0.3 ? 0xffcc00 : 0xff3333;

      overlay.rect(barX, barY - 1, barW, barH + 2).fill({ color: 0x000000, alpha: 0.7 });
      overlay.rect(barX, barY, Math.round(barW * ratio), barH).fill({ color: barColor });
    }
  }

  // ─── PATHFINDING ──────────────────────────────────────────────────────────

  private findPath(fromCol: number, fromRow: number, toCol: number, toRow: number, moverId: number): [number, number][] {
    const moverPos = this.world.getComponent<Position>(moverId, 'position')!;
    const passable = (x: number, y: number) => {
      if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W) return false;
      if (!this.mapData.walkable[y][x]) return false;
      if (x === moverPos.col && y === moverPos.row) return true;
      const occ = this.getActorAt(x, y);
      return occ === null || occ === moverId;
    };
    const path: [number, number][] = [];
    new ROT.Path.AStar(toCol, toRow, passable).compute(fromCol, fromRow, (x, y) => path.push([x, y]));
    return path;
  }

  // ─── HELPERS ──────────────────────────────────────────────────────────────

  private getActorAt(col: number, row: number, team?: string): number | null {
    for (const id of this.world.query('position', 'faction')) {
      const pos = this.world.getComponent<Position>(id, 'position')!;
      if (pos.col !== col || pos.row !== row) continue;
      if (team && this.world.getComponent<Faction>(id, 'faction')?.team !== team) continue;
      return id;
    }
    return null;
  }

  // ─── MAIN LOOP ────────────────────────────────────────────────────────────

  start() {
    this.app.ticker.add(() => this.loop());
  }

  private loop() {
    this.camera.update();
    this.renderer.cameraX = this.camera.state.x;
    this.renderer.cameraY = this.camera.state.y;
    this.renderer.updateCamera(this.app.screen.width, this.app.screen.height);
    this.updateHUD();
    this.drawHealthBars();

    // Hit-flash: briefly tint the whole scene red when player takes damage
    if (this.hitFlashFrames > 0) {
      this.renderer.root.filters = [this.hitFilter];
      this.hitFlashFrames--;
    } else if (this.renderer.root.filters?.length) {
      this.renderer.root.filters = [];
    }
  }
}
