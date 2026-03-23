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

import { Application, Text, TextStyle, Graphics, Container } from 'pixi.js';
import * as ROT from 'rot-js';
import { ParallaxAsciiRenderer } from '../rendering/ParallaxAsciiRenderer';
import { CameraController } from '../rendering/CameraController';
import { World, Position, Renderable, Health, Faction, Combat, Ability, Abilities, StatusEffect } from '../core/ECS';
import { generateMap, populateWorld, GeneratedMap } from './MapGenerator';
import { processInteractions } from '../core/InteractionSystem';
import { resolveSpell, getRangeTiles, tickCooldowns } from './SpellSystem';
import { RadialWheel, WheelNode } from '../ui/RadialWheel';

const MAP_W     = 48;
const MAP_H     = 32;
const CELL_SIZE = 16;

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

  private turnCount: number = 0;
  private messages: string[] = [];

  private hudText!: Text;
  private msgText!: Text;
  private modeText!: Text;   // shows current mode / targeting hint
  private fabGfx!: Graphics; // floating action button
  private fabLabel!: Text;
  private uiLayer!: Container; // fixed overlay (not in parallax)

  private wheel!: RadialWheel;

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
    this.drawActors();

    // Center camera on player
    const startPos = this.world.getComponent<Position>(this.playerId, 'position')!;
    this.camera.state.x = (startPos.col - MAP_W / 2) * CELL_SIZE;
    this.camera.state.y = (startPos.row - MAP_H / 2) * CELL_SIZE;

    // Fixed UI overlay (sits above parallax, unaffected by camera)
    this.uiLayer = new Container();
    this.app.stage.addChild(this.uiLayer);

    this.setupHUD();
    this.setupFAB();
    this.setupWheel();
    this.setupInput();

    this.addMessage('Tap ⚡ or tap yourself to open the action wheel.');
    this.addMessage('Tap a floor tile to move.');
  }

  // ─── HUD ───────────────────────────────────────────────────────────────────

  private setupHUD() {
    const hudStyle = new TextStyle({ fontFamily: '"Courier New", monospace', fontSize: 14, fill: 0xaaddff, fontWeight: 'bold' });
    const msgStyle = new TextStyle({ fontFamily: '"Courier New", monospace', fontSize: 12, fill: 0xccccaa });
    const modeStyle = new TextStyle({ fontFamily: '"Courier New", monospace', fontSize: 13, fill: 0xffee88, fontWeight: 'bold' });

    this.hudText  = new Text({ text: '', style: hudStyle });
    this.msgText  = new Text({ text: '', style: msgStyle });
    this.modeText = new Text({ text: '', style: modeStyle });

    this.hudText.x  = 8; this.hudText.y  = 8;
    this.msgText.x  = 8; this.msgText.y  = 30;
    this.modeText.x = 8; this.modeText.y = 0; // positioned in loop

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
    this.modeText.y = sh - 80;
    this.modeText.x = 8;
    if (this.mode === 'targeting' && this.selectedAbility) {
      const a = this.selectedAbility;
      this.modeText.text = `▶ ${a.name} (${a.element}, rng ${a.range})  — tap target or tap yourself to cancel`;
    } else {
      this.modeText.text = '';
    }

    // FAB position (bottom-right, above thumb area)
    if (this.fabGfx) {
      this.fabGfx.x = sw - 56;
      this.fabGfx.y = sh - 60;
      this.fabLabel.x = sw - 56;
      this.fabLabel.y = sh - 60;
    }
  }

  private addMessage(msg: string) {
    this.messages.unshift(msg);
    if (this.messages.length > 6) this.messages.pop();
  }

  // ─── FAB (floating action button) ─────────────────────────────────────────

  private setupFAB() {
    this.fabGfx = new Graphics();
    this.fabGfx.circle(0, 0, 32).fill({ color: 0x223355, alpha: 0.9 }).stroke({ color: 0x4477cc, alpha: 0.8, width: 2 });
    this.fabGfx.eventMode = 'static';
    this.fabGfx.cursor    = 'pointer';
    this.fabGfx.on('pointerdown', (e) => {
      e.stopPropagation();
      this.openWheel();
    });

    const style = new TextStyle({ fontFamily: '"Courier New", monospace', fontSize: 20, fill: 0x88bbff, fontWeight: 'bold' });
    this.fabLabel = new Text({ text: '⚡', style });
    this.fabLabel.anchor.set(0.5);
    this.fabLabel.eventMode = 'none';

    this.uiLayer.addChild(this.fabGfx);
    this.uiLayer.addChild(this.fabLabel);
  }

  // ─── WHEEL ────────────────────────────────────────────────────────────────

  private setupWheel() {
    this.wheel = new RadialWheel();
    this.uiLayer.addChild(this.wheel.container);
  }

  private openWheel() {
    if (this.phase !== 'player' || this.mode !== 'free') return;
    const cx = this.app.screen.width  / 2;
    const cy = this.app.screen.height / 2;
    this.wheel.open(this.buildWheelNodes(), cx, cy);
  }

  private buildWheelNodes(): WheelNode[] {
    const abilities = this.world.getComponent<Abilities>(this.playerId, 'abilities');
    const spellNodes: WheelNode[] = abilities
      ? abilities.list.map(a => ({
          id: a.id,
          label: a.name,
          icon: this.spellIcon(a),
          color: ELEMENT_COLOR[a.element] ?? 0x888888,
          disabled: a.cooldownCurrent > 0,
          badge: a.cooldownCurrent > 0 ? `cd:${a.cooldownCurrent}` : undefined,
          action: () => this.enterTargeting(a),
        }))
      : [];

    const root: WheelNode[] = [
      {
        id: 'wait',
        label: 'Wait',
        icon: '…',
        color: 0x446644,
        action: () => this.endPlayerTurn(),
      },
    ];

    if (spellNodes.length > 0) {
      root.unshift({
        id: 'spells',
        label: 'Spells',
        icon: '✦',
        color: 0x334488,
        children: spellNodes,
      });
    }

    return root;
  }

  private spellIcon(a: Ability): string {
    switch (a.element) {
      case 'fire':      return '🔥';
      case 'lightning': return '⚡';
      case 'ice':       return '❄';
      case 'poison':    return '☠';
      case 'arcane':    return '✦';
      default:          return '·';
    }
  }

  // ─── TARGETING MODE ───────────────────────────────────────────────────────

  private enterTargeting(ability: Ability) {
    this.mode = 'targeting';
    this.selectedAbility = ability;
    this.showRangeHighlight(ability);
    this.addMessage(`Select target for ${ability.name}. Tap yourself to cancel.`);
  }

  private cancelTargeting() {
    this.clearRangeHighlight();
    this.mode = 'free';
    this.selectedAbility = null;
    this.addMessage('Cancelled.');
  }

  private showRangeHighlight(ability: Ability) {
    const pos = this.world.getComponent<Position>(this.playerId, 'position')!;
    const actorSet = new Set(
      this.world.query('position', 'faction').map(id => {
        const p = this.world.getComponent<Position>(id, 'position')!;
        return `${p.col},${p.row}`;
      }),
    );

    const color = ELEMENT_COLOR[ability.element] ?? 0xffffff;
    const allTiles = getRangeTiles(pos.col, pos.row, ability.range, this.mapData);

    this.highlightTiles = allTiles.filter(([c, r]) => this.mapData.walkable[r]?.[c] && !actorSet.has(`${c},${r}`));

    for (const [c, r] of this.highlightTiles) {
      this.renderer.setCell('gameplay', c, r, { char: '·', fg: color, alpha: 0.65 });
    }
  }

  private clearRangeHighlight() {
    for (const [c, r] of this.highlightTiles) this.restoreTerrain(c, r);
    this.drawActors();
    this.highlightTiles = [];
  }

  // ─── INPUT ────────────────────────────────────────────────────────────────

  private setupInput() {
    const canvas = this.app.canvas as HTMLCanvasElement;

    canvas.addEventListener('click', (e) => {
      if (this.camera.hasDragged) return;
      const rect = canvas.getBoundingClientRect();
      this.handleTap(e.clientX - rect.left, e.clientY - rect.top);
    });

    let touchStartX = 0, touchStartY = 0;
    canvas.addEventListener('touchstart', (e) => {
      const t = e.touches[0];
      touchStartX = t.clientX; touchStartY = t.clientY;
    }, { passive: true });

    canvas.addEventListener('touchend', (e) => {
      if (this.camera.hasDragged) return;
      const t = e.changedTouches[0];
      if (Math.hypot(t.clientX - touchStartX, t.clientY - touchStartY) > 8) return;
      const rect = canvas.getBoundingClientRect();
      this.handleTap(t.clientX - rect.left, t.clientY - rect.top);
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
    if (this.wheel.isOpen) return;
    if (this.phase === 'game_over') return;
    if (this.phase !== 'player') return;

    const [col, row] = this.screenToTile(screenX, screenY);
    if (col < 0 || col >= MAP_W || row < 0 || row >= MAP_H) return;

    const playerPos = this.world.getComponent<Position>(this.playerId, 'position')!;

    if (this.mode === 'targeting') {
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
      this.clearRangeHighlight();
      resolveSpell(this.world, this.playerId, this.selectedAbility!, col, row, this.mapData, this.addMessage.bind(this));
      this.mode = 'free';
      this.selectedAbility = null;
      this.drawActors();
      this.endPlayerTurn();
      return;
    }

    // free mode
    // Tap own tile → open wheel
    if (col === playerPos.col && row === playerPos.row) {
      this.openWheel();
      return;
    }

    // Tap enemy directly → do nothing (no bump attack)
    const enemy = this.getActorAt(col, row, 'enemy');
    if (enemy !== null) {
      this.addMessage('Open the wheel to select a spell first.');
      return;
    }

    // Tap floor → pathfind & move one step
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
    // Run environment interactions at end of each full round
    for (const id of this.world.query('status', 'position')) {
      processInteractions(this.world, id, { trigger: 'turn_start' });
    }
    // Tick spell cooldowns
    tickCooldowns(this.world);
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
      const ePos = this.world.getComponent<Position>(enemyId, 'position')!;
      const dx = Math.abs(playerPos.col - ePos.col);
      const dy = Math.abs(playerPos.row - ePos.row);

      if (dx <= 1 && dy <= 1) {
        // Adjacent — melee strike
        this.enemyMelee(enemyId, this.playerId);
        if (!this.world.hasComponent(this.playerId, 'health')) break;
      } else {
        // Move toward player
        const path = this.findPath(ePos.col, ePos.row, playerPos.col, playerPos.row, enemyId);
        if (path.length >= 2) {
          const [nc, nr] = path[1];
          if (nc !== playerPos.col || nr !== playerPos.row) {
            this.moveEntity(enemyId, nc, nr);
          }
        }
      }
    }

    this.phase = this.world.hasComponent(this.playerId, 'health') ? 'player' : 'game_over';
    if (this.phase === 'game_over') this.addMessage('You have died. Refresh to play again.');
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

    if (defHealth.current <= 0) {
      this.addMessage('YOU DIED. Refresh to restart.');
      this.phase = 'game_over';
      this.world.removeComponent(this.playerId, 'health');
    }
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
      const pos  = this.world.getComponent<Position>(id, 'position')!;
      const rend = this.world.getComponent<Renderable>(id, 'renderable')!;
      // Colour tint for status effects
      const status = this.world.getComponent<StatusEffect>(id, 'status');
      let fg = rend.fg;
      if (status) {
        if (status.effects.has('burning'))  fg = 0xff5500;
        if (status.effects.has('shocked'))  fg = 0xffff00;
        if (status.effects.has('slowed'))   fg = 0x88ccff;
        if (status.effects.has('poisoned')) fg = 0x66ff44;
      }
      this.renderer.setCell('gameplay', pos.col, pos.row, { char: rend.char, fg, alpha: rend.alpha ?? 1.0 });
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
  }
}
