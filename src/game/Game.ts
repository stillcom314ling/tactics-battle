/**
 * GAME — Top-level orchestrator
 *
 * Responsibilities:
 *   - Create and wire all systems (world, renderer, camera, AI, combat, UI)
 *   - Own the main render loop
 *   - Coordinate the turn flow (player turn → enemy phase → back)
 *   - Handle pathfinding and entity movement (still lives here as they
 *     need both ECS world and renderer in concert)
 *
 * Everything else is delegated:
 *   AI          → src/ai/AISystem.ts
 *   Combat      → src/combat/CombatSystem.ts
 *   Input       → src/game/InputHandler.ts
 *   Targeting   → src/game/TargetingController.ts
 *   HUD / bars  → src/rendering/HUDRenderer.ts
 *   Actor draw  → src/game/RenderSystem.ts
 *   Turn phase  → src/core/TurnManager.ts
 */

import { Application, Text, TextStyle, Graphics, Container } from 'pixi.js';
import * as ROT from 'rot-js';

import { ParallaxAsciiRenderer }  from '../rendering/ParallaxAsciiRenderer';
import { CameraController }        from '../rendering/CameraController';
import { HUDRenderer }             from '../rendering/HUDRenderer';
import { drawActors, restoreTerrain } from './RenderSystem';

import { World, Position, Abilities, StatusEffect, EntityId } from '../core/ECS';
import { TurnManager }             from '../core/TurnManager';
import { processInteractions }     from '../core/InteractionSystem';

import { TooltipOverlay }          from '../ui/TooltipOverlay';
import { DPad }                    from '../ui/DPad';
import { ActionMenu, ActiveItem }  from '../ui/ActionMenu';

import { generateMap, populateWorld, GeneratedMap } from './MapGenerator';
import { resolveSpell }            from './SpellSystem';
import { TargetingController }     from './TargetingController';
import { InputHandler }            from './InputHandler';

import { resolveMelee, resolveRangedAttack, getActorAt } from '../combat/CombatSystem';
import { processEnemyTurns, alertEnemiesNear }           from '../ai/AISystem';

import {
  MAP_W, MAP_H,
}                                   from '../constants/map';
import {
  CELL_SIZE,
  DEPTH_BG_FAR, DEPTH_BG_MID, DEPTH_GAMEPLAY, DEPTH_FOREGROUND,
  BG_LAYER_SCALE, FG_LAYER_SCALE,
  BG_FAR_ALPHA, BG_FAR_TINT, BG_MID_ALPHA, BG_MID_TINT, GAMEPLAY_ALPHA, FOREGROUND_ALPHA,
  APP_BG_COLOR, ELEMENT_COLORS,
}                                   from '../constants/rendering';
import {
  ENEMY_TURN_DELAY_MS, ALERT_RADIUS_SPELL,
}                                   from '../constants/combat';
import { FAB_RADIUS, UI_FONT_FAMILY } from '../constants/ui';
import { elementColor }             from '../utils/colors';

// ─── GAME CLASS ───────────────────────────────────────────────────────────────

export class Game {
  // ── PixiJS / rendering ────────────────────────────────────────────────────
  private app!:      Application;
  private renderer!: ParallaxAsciiRenderer;
  private camera!:   CameraController;
  private uiLayer!:  Container;

  // ── ECS / map ─────────────────────────────────────────────────────────────
  private world!:    World;
  private playerId!: EntityId;
  private mapData!:  GeneratedMap;

  // ── Systems ───────────────────────────────────────────────────────────────
  private turnManager!:      TurnManager;
  private targetingCtrl!:    TargetingController;
  private inputHandler!:     InputHandler;
  private hudRenderer!:      HUDRenderer;

  // ── UI widgets ────────────────────────────────────────────────────────────
  private dpad!:          DPad;
  private actionMenu!:    ActionMenu;
  private tooltip!:       TooltipOverlay;
  private fabGfx!:        Graphics;
  private fabLabel!:      Text;

  /** Debounce flag: prevents an action-menu tap from also firing a map-tap. */
  private menuJustOpened = false;

  constructor(private container: HTMLElement) {}

  // ─── INIT ─────────────────────────────────────────────────────────────────

  async init() {
    // ── PixiJS app ─────────────────────────────────────────────────────────
    this.app = new Application();
    await this.app.init({
      resizeTo:        this.container,
      backgroundColor: APP_BG_COLOR,
      antialias:       false,
      resolution:      window.devicePixelRatio || 1,
      autoDensity:     true,
    });
    this.container.appendChild(this.app.canvas);

    // ── Camera ─────────────────────────────────────────────────────────────
    this.camera = new CameraController(this.app.canvas as HTMLElement);

    // ── Parallax renderer ─────────────────────────────────────────────────
    this.renderer = new ParallaxAsciiRenderer();
    this.app.stage.addChild(this.renderer.root);

    const bgCols = Math.ceil(MAP_W * BG_LAYER_SCALE);
    const bgRows = Math.ceil(MAP_H * BG_LAYER_SCALE);
    this.renderer.addLayer({ id: 'bg_far',     depth: DEPTH_BG_FAR,    cols: bgCols,                     rows: bgRows,                     cellSize: CELL_SIZE, alpha: BG_FAR_ALPHA, tint: BG_FAR_TINT });
    this.renderer.addLayer({ id: 'bg_mid',     depth: DEPTH_BG_MID,    cols: bgCols,                     rows: bgRows,                     cellSize: CELL_SIZE, alpha: BG_MID_ALPHA, tint: BG_MID_TINT });
    this.renderer.addLayer({ id: 'gameplay',   depth: DEPTH_GAMEPLAY,  cols: MAP_W,                      rows: MAP_H,                      cellSize: CELL_SIZE, alpha: GAMEPLAY_ALPHA });
    this.renderer.addLayer({ id: 'foreground', depth: DEPTH_FOREGROUND, cols: Math.ceil(MAP_W * FG_LAYER_SCALE), rows: Math.ceil(MAP_H * FG_LAYER_SCALE), cellSize: CELL_SIZE, alpha: FOREGROUND_ALPHA });

    // ── ECS world + map ────────────────────────────────────────────────────
    this.world   = new World();
    this.mapData = generateMap();

    this.renderer.setLayerData('bg_far',     this.mapData.bgFar);
    this.renderer.setLayerData('bg_mid',     this.mapData.bgMid);
    this.renderer.setLayerData('foreground', this.mapData.foreground);
    this.renderer.setLayerData('gameplay',   this.mapData.gameplay);

    const { playerId } = populateWorld(this.world, this.mapData);
    this.playerId = playerId;

    // ── Turn manager ───────────────────────────────────────────────────────
    this.turnManager = new TurnManager(this.world);

    // ── UI layer (fixed overlay above parallax) ────────────────────────────
    this.uiLayer = new Container();
    this.app.stage.addChild(this.uiLayer);

    // ── HUD renderer ──────────────────────────────────────────────────────
    this.hudRenderer = new HUDRenderer(this.uiLayer, this.renderer, this.app);

    // ── UI widgets ────────────────────────────────────────────────────────
    this.setupFAB();
    this.setupActionMenu();
    this.setupDPad();
    this.setupTooltip();

    // ── Targeting controller ──────────────────────────────────────────────
    this.targetingCtrl = new TargetingController(
      this.world,
      this.renderer,
      this.mapData,
      this.playerId,
      {
        addMessage:     (msg) => this.hudRenderer.addMessage(msg),
        restoreTerrain: (c, r) => restoreTerrain(this.renderer, this.mapData, c, r),
        drawActors:     () => drawActors(this.world, this.renderer),
      },
    );

    // ── Input handler ─────────────────────────────────────────────────────
    this.inputHandler = new InputHandler(
      this.app,
      this.camera,
      () => this.actionMenu.isOpen,
      {
        onTap:       (x, y) => this.handleTap(x, y),
        onDirection: (dx, dy) => this.movePlayerInDirection(dx, dy),
        onWait:      () => this.waitTurn(),
      },
    );

    // ── Initial render ────────────────────────────────────────────────────
    drawActors(this.world, this.renderer);

    // Center camera on player
    const startPos = this.world.getComponent<Position>(this.playerId, 'position')!;
    this.camera.state.x = (startPos.col - MAP_W / 2) * CELL_SIZE;
    this.camera.state.y = (startPos.row - MAP_H / 2) * CELL_SIZE;

    this.hudRenderer.addMessage('Use d-pad to move. Tap ☰ for actions.');
    this.hudRenderer.addMessage('Tap an enemy or yourself to inspect.');
  }

  // ─── UI SETUP ─────────────────────────────────────────────────────────────

  private setupFAB() {
    this.fabGfx = new Graphics();
    this.fabGfx.circle(0, 0, FAB_RADIUS)
      .fill({ color: 0x112244, alpha: 0.92 })
      .stroke({ color: 0x3366bb, alpha: 0.85, width: 2.5 });
    this.fabGfx.eventMode = 'static';
    this.fabGfx.cursor    = 'pointer';
    this.fabGfx.on('pointerdown', (e) => {
      e.stopPropagation();
      if (this.turnManager.isGameOver || !this.turnManager.isPlayerTurn) return;
      if (this.targetingCtrl.isTargeting) return;
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

    this.fabLabel = new Text({
      text: '☰',
      style: new TextStyle({ fontFamily: UI_FONT_FAMILY, fontSize: 20, fill: 0x6699ff, fontWeight: 'bold' }),
    });
    this.fabLabel.anchor.set(0.5);
    this.fabLabel.eventMode = 'none';

    this.uiLayer.addChild(this.fabGfx);
    this.uiLayer.addChild(this.fabLabel);
  }

  private setupActionMenu() {
    this.actionMenu = new ActionMenu();
    this.uiLayer.addChild(this.actionMenu.container);
  }

  private setupDPad() {
    this.dpad = new DPad(
      (dx, dy) => this.movePlayerInDirection(dx, dy),
      () => this.waitTurn(),
    );
    this.uiLayer.addChild(this.dpad.container);
  }

  private setupTooltip() {
    this.tooltip = new TooltipOverlay();
    this.uiLayer.addChild(this.tooltip.container);
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
      color:      elementColor(a.element),
      tooltip:    `DMG ${a.damage}  RNG ${a.range}${a.effects.length ? '  ' + a.effects.join(', ') : ''}`,
      action:     () => this.targetingCtrl.enter(a),
    }));
  }

  // ─── INPUT HANDLERS ───────────────────────────────────────────────────────

  private handleTap(screenX: number, screenY: number) {
    if (this.turnManager.isGameOver) return;
    if (!this.turnManager.isPlayerTurn) return;
    if (this.menuJustOpened) return;

    // Close tooltip on menu-open
    if (this.actionMenu.isOpen) { this.actionMenu.close(); return; }

    const playerStatus = this.world.getComponent<StatusEffect>(this.playerId, 'status');
    if (playerStatus?.effects.has('stunned')) {
      this.hudRenderer.addMessage('You are stunned and lose your turn!');
      this.endPlayerTurn();
      return;
    }

    const [col, row] = this.screenToTile(screenX, screenY);
    if (col < 0 || col >= MAP_W || row < 0 || row >= MAP_H) return;

    const playerPos = this.world.getComponent<Position>(this.playerId, 'position')!;

    // ── Targeting mode ───────────────────────────────────────────────────
    if (this.targetingCtrl.isTargeting) {
      this.tooltip.hide();
      const result = this.targetingCtrl.handleTap(col, row);

      if (result === 'cast') {
        const pending = this.targetingCtrl.getPendingTarget();
        const tc = pending ? pending[0] : col;
        const tr = pending ? pending[1] : row;
        const ability = this.targetingCtrl.state.selectedAbility!;
        this.targetingCtrl.clearAfterCast();
        resolveSpell(
          this.world, this.playerId, ability, tc, tr, this.mapData,
          (msg) => this.hudRenderer.addMessage(msg),
        );
        this.alertEnemies(playerPos.col, playerPos.row, ALERT_RADIUS_SPELL);
        drawActors(this.world, this.renderer);
        this.endPlayerTurn();
      }
      return;
    }

    // ── Free mode ────────────────────────────────────────────────────────

    // Tap own tile → inspect self
    if (col === playerPos.col && row === playerPos.row) {
      const sw = this.app.screen.width, sh = this.app.screen.height;
      this.tooltip.show(this.world, this.playerId, screenX, screenY, sw, sh);
      return;
    }

    // Tap enemy → inspect
    const enemy = getActorAt(this.world, col, row, 'enemy');
    if (enemy !== null) {
      const sw = this.app.screen.width, sh = this.app.screen.height;
      this.tooltip.show(this.world, enemy, screenX, screenY, sw, sh);
      return;
    }

    // Tap floor → pathfind one step
    this.tooltip.hide();
    if (!this.mapData.walkable[row]?.[col]) return;
    const path = this.findPath(playerPos.col, playerPos.row, col, row, this.playerId);
    if (path.length < 2) return;
    this.moveEntity(this.playerId, path[1][0], path[1][1]);
    this.endPlayerTurn();
  }

  private movePlayerInDirection(dx: number, dy: number) {
    if (this.turnManager.isGameOver || !this.turnManager.isPlayerTurn) return;
    if (this.targetingCtrl.isTargeting) return;

    const playerStatus = this.world.getComponent<StatusEffect>(this.playerId, 'status');
    if (playerStatus?.effects.has('stunned')) {
      this.hudRenderer.addMessage('You are stunned and lose your turn!');
      this.endPlayerTurn();
      return;
    }

    const pos = this.world.getComponent<Position>(this.playerId, 'position')!;
    const nc = pos.col + dx;
    const nr = pos.row + dy;
    if (nc < 0 || nc >= MAP_W || nr < 0 || nr >= MAP_H) return;
    if (!this.mapData.walkable[nr]?.[nc]) return;
    if (getActorAt(this.world, nc, nr, 'enemy') !== null) return; // no bump-attack
    this.tooltip.hide();
    this.moveEntity(this.playerId, nc, nr);
    this.endPlayerTurn();
  }

  private waitTurn() {
    if (this.turnManager.isGameOver || !this.turnManager.isPlayerTurn) return;
    if (this.targetingCtrl.isTargeting) return;
    this.hudRenderer.addMessage('You wait.');
    this.endPlayerTurn();
  }

  // ─── TURN FLOW ────────────────────────────────────────────────────────────

  private endPlayerTurn() {
    this.turnManager.endPlayerTurn();

    // Tick environment interactions (burning, poison, fire terrain)
    for (const id of this.world.query('status', 'position')) {
      processInteractions(this.world, id, {
        trigger:    'turn_start',
        addMessage: (msg) => this.hudRenderer.addMessage(msg),
      });
    }

    setTimeout(() => this.runEnemyPhase(), ENEMY_TURN_DELAY_MS);
  }

  private runEnemyPhase() {
    const combatCallbacks = {
      addMessage:   (msg: string) => this.hudRenderer.addMessage(msg),
      onPlayerHit:  () => this.hudRenderer.triggerHitFlash(),
      onPlayerDied: () => this.turnManager.setGameOver(),
      alertEnemies: (col: number, row: number, radius: number) => this.alertEnemies(col, row, radius),
    };

    const playerPos = this.world.getComponent<Position>(this.playerId, 'position')!;

    const survived = processEnemyTurns(this.world, this.playerId, {
      world:    this.world,
      map:      this.mapData,
      playerId: this.playerId,
      playerPos,
      findPath: (fc, fr, tc, tr, mover) => this.findPath(fc, fr, tc, tr, mover),
      moveEntity:          (id, c, r)          => this.moveEntity(id, c, r),
      resolveMelee:        (attackerId, def)   => resolveMelee(this.world, attackerId, def, this.playerId, combatCallbacks),
      resolveRangedAttack: (atk, tgt, dmg)     => resolveRangedAttack(this.world, atk, tgt, dmg, this.mapData, this.playerId, combatCallbacks),
      getActorAt:          (c, r)              => getActorAt(this.world, c, r),
      alertEnemies:        (c, r, radius)      => this.alertEnemies(c, r, radius),
    });

    drawActors(this.world, this.renderer);

    if (survived) {
      this.turnManager.endEnemyPhase();
    } else {
      this.turnManager.setGameOver();
      this.hudRenderer.addMessage('You have died. Refresh to play again.');
    }
  }

  // ─── MOVEMENT ─────────────────────────────────────────────────────────────

  private moveEntity(entityId: EntityId, newCol: number, newRow: number) {
    const pos  = this.world.getComponent<Position>(entityId, 'position')!;
    const rend = this.world.getComponent<any>(entityId, 'renderable')!;

    restoreTerrain(this.renderer, this.mapData, pos.col, pos.row);
    pos.col = newCol;
    pos.row = newRow;

    this.renderer.setCell('gameplay', newCol, newRow, {
      char: rend.char, fg: rend.fg, alpha: rend.alpha ?? 1.0,
    });

    // Keep camera centered on player
    if (entityId === this.playerId) {
      this.camera.state.x      = (newCol - MAP_W / 2) * CELL_SIZE;
      this.camera.state.y      = (newRow - MAP_H / 2) * CELL_SIZE;
      this.camera.state.velocityX = 0;
      this.camera.state.velocityY = 0;
    }
  }

  // ─── HELPERS ──────────────────────────────────────────────────────────────

  private alertEnemies(sourceCol: number, sourceRow: number, radius: number) {
    const anyNew = alertEnemiesNear(this.world, sourceCol, sourceRow, radius);
    if (anyNew) this.hudRenderer.addMessage('The enemies are alerted!');
  }

  private findPath(
    fromCol: number, fromRow: number,
    toCol:   number, toRow:   number,
    moverId: EntityId,
  ): [number, number][] {
    const moverPos = this.world.getComponent<Position>(moverId, 'position')!;
    const passable = (x: number, y: number) => {
      if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W) return false;
      if (!this.mapData.walkable[y][x]) return false;
      if (x === moverPos.col && y === moverPos.row) return true;
      const occ = getActorAt(this.world, x, y);
      return occ === null || occ === moverId;
    };
    const path: [number, number][] = [];
    new ROT.Path.AStar(toCol, toRow, passable).compute(fromCol, fromRow, (x, y) => path.push([x, y]));
    return path;
  }

  private screenToTile(screenX: number, screenY: number): [number, number] {
    const originX = (this.app.screen.width  - MAP_W * CELL_SIZE) / 2 - this.camera.state.x;
    const originY = (this.app.screen.height - MAP_H * CELL_SIZE) / 2 - this.camera.state.y;
    const col = Math.floor((screenX - originX) / CELL_SIZE);
    const row = Math.floor((screenY - originY) / CELL_SIZE);
    return [col, row];
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

    this.hudRenderer.update(
      this.world,
      this.playerId,
      this.turnManager.phase,
      this.targetingCtrl.state.mode,
      this.targetingCtrl.state.selectedAbility,
      this.targetingCtrl.state.pendingTargetCol !== null,
      this.dpad,
      this.fabGfx,
      this.fabLabel,
    );

    this.hudRenderer.drawHealthBars(this.world, this.playerId);
    this.hudRenderer.tickHitFlash();
  }
}
