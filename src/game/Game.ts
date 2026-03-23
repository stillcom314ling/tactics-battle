/**
 * GAME
 *
 * Top-level orchestrator. Initializes PixiJS, the parallax renderer,
 * the ECS world, map generation, and the game loop.
 *
 * Architecture overview:
 *   index.html -> main.ts -> Game.init() -> Game.start()
 *
 *   Rendering:  ParallaxAsciiRenderer (multiple depth layers of ASCII)
 *               CameraController (pan with mouse/touch, inertia)
 *
 *   Game logic: World (ECS - entities, components)
 *               MapGenerator (rot.js dungeon generation)
 *               Inline turn loop: player clicks → enemy AI → repeat
 */

import { Application, Text, TextStyle } from 'pixi.js';
import * as ROT from 'rot-js';
import { ParallaxAsciiRenderer } from '../rendering/ParallaxAsciiRenderer';
import { CameraController } from '../rendering/CameraController';
import { World, Position, Renderable, Health, Faction, Combat } from '../core/ECS';
import { generateMap, populateWorld, GeneratedMap } from './MapGenerator';

// Map dimensions
const MAP_W = 48;
const MAP_H = 32;
const CELL_SIZE = 16;

type GamePhase = 'player' | 'enemy' | 'game_over';

export class Game {
  private app!: Application;
  private renderer!: ParallaxAsciiRenderer;
  private camera!: CameraController;
  private world!: World;
  private playerId!: number;
  private mapData!: GeneratedMap;

  private phase: GamePhase = 'player';
  private turnCount: number = 0;
  private messages: string[] = [];

  private hudText!: Text;
  private msgText!: Text;

  constructor(private container: HTMLElement) {}

  async init() {
    // --- PixiJS ---
    this.app = new Application();
    await this.app.init({
      resizeTo: this.container,
      backgroundColor: 0x0a0a0c,
      antialias: false,
      resolution: window.devicePixelRatio || 1,
      autoDensity: true,
    });
    this.container.appendChild(this.app.canvas);

    // --- Camera ---
    this.camera = new CameraController(this.app.canvas as HTMLElement);

    // --- Parallax Renderer ---
    this.renderer = new ParallaxAsciiRenderer();
    this.app.stage.addChild(this.renderer.root);

    // Add depth layers (far -> near)
    const bgScale = 1.4;
    this.renderer.addLayer({
      id: 'bg_far',
      depth: 0.2,
      cols: Math.ceil(MAP_W * bgScale),
      rows: Math.ceil(MAP_H * bgScale),
      cellSize: CELL_SIZE,
      alpha: 0.4,
      tint: 0x8888aa,
    });
    this.renderer.addLayer({
      id: 'bg_mid',
      depth: 0.5,
      cols: Math.ceil(MAP_W * bgScale),
      rows: Math.ceil(MAP_H * bgScale),
      cellSize: CELL_SIZE,
      alpha: 0.5,
      tint: 0x9999bb,
    });
    this.renderer.addLayer({
      id: 'gameplay',
      depth: 1.0,
      cols: MAP_W,
      rows: MAP_H,
      cellSize: CELL_SIZE,
      alpha: 1.0,
    });
    this.renderer.addLayer({
      id: 'foreground',
      depth: 1.3,
      cols: Math.ceil(MAP_W * 1.2),
      rows: Math.ceil(MAP_H * 1.2),
      cellSize: CELL_SIZE,
      alpha: 0.3,
    });

    // --- ECS World & Map ---
    this.world = new World();
    this.mapData = generateMap(MAP_W, MAP_H);

    this.renderer.setLayerData('bg_far', this.mapData.bgFar);
    this.renderer.setLayerData('bg_mid', this.mapData.bgMid);
    this.renderer.setLayerData('foreground', this.mapData.foreground);
    this.renderer.setLayerData('gameplay', this.mapData.gameplay);

    const { playerId } = populateWorld(this.world, this.mapData);
    this.playerId = playerId;

    // Draw all actors on top of terrain
    this.drawActors();

    // Center camera on player
    const startPos = this.world.getComponent<Position>(this.playerId, 'position')!;
    this.camera.state.x = (startPos.col - MAP_W / 2) * CELL_SIZE;
    this.camera.state.y = (startPos.row - MAP_H / 2) * CELL_SIZE;

    // --- HUD ---
    this.setupHUD();

    // --- Input ---
    this.setupClickInput();

    this.addMessage('Welcome! Click to move, bump enemies to attack.');
    console.log('[game] initialized');
  }

  // ─── HUD ────────────────────────────────────────────────────────────────────

  private setupHUD() {
    const hudStyle = new TextStyle({
      fontFamily: '"Courier New", monospace',
      fontSize: 14,
      fill: 0xaaddff,
      fontWeight: 'bold',
    });
    const msgStyle = new TextStyle({
      fontFamily: '"Courier New", monospace',
      fontSize: 12,
      fill: 0xccccaa,
    });

    this.hudText = new Text({ text: '', style: hudStyle });
    this.hudText.x = 8;
    this.hudText.y = 8;
    this.app.stage.addChild(this.hudText);

    this.msgText = new Text({ text: '', style: msgStyle });
    this.msgText.x = 8;
    this.msgText.y = 30;
    this.app.stage.addChild(this.msgText);
  }

  private updateHUD() {
    const hp = this.world.getComponent<Health>(this.playerId, 'health');
    const hpStr = hp ? `HP: ${Math.max(0, Math.ceil(hp.current))}/${hp.max}` : 'HP: --';
    const phaseStr =
      this.phase === 'player' ? '[YOUR TURN]' :
      this.phase === 'enemy'  ? '[...]' :
                                '[GAME OVER]';
    const enemies = this.world.query('position', 'faction').filter(id => {
      const f = this.world.getComponent<Faction>(id, 'faction')!;
      return f.team === 'enemy';
    }).length;

    this.hudText.text = `${hpStr}   Turn: ${this.turnCount}   Enemies: ${enemies}   ${phaseStr}`;
    this.msgText.text = this.messages.slice(0, 3).join('\n');
  }

  private addMessage(msg: string) {
    this.messages.unshift(msg);
    if (this.messages.length > 6) this.messages.pop();
  }

  // ─── INPUT ──────────────────────────────────────────────────────────────────

  private setupClickInput() {
    const canvas = this.app.canvas as HTMLCanvasElement;
    canvas.addEventListener('click', (e) => {
      if (this.phase !== 'player') return;

      const rect = canvas.getBoundingClientRect();
      // Account for CSS scaling vs canvas resolution
      const scaleX = canvas.width / rect.width;
      const scaleY = canvas.height / rect.height;
      const screenX = (e.clientX - rect.left) * scaleX;
      const screenY = (e.clientY - rect.top) * scaleY;

      // Convert screen px → tile col/row using the gameplay layer's current position
      const layerPixelW = MAP_W * CELL_SIZE;
      const layerPixelH = MAP_H * CELL_SIZE;
      const layerOriginX = (this.app.screen.width - layerPixelW) / 2 - this.camera.state.x;
      const layerOriginY = (this.app.screen.height - layerPixelH) / 2 - this.camera.state.y;

      const col = Math.floor((screenX - layerOriginX) / CELL_SIZE);
      const row = Math.floor((screenY - layerOriginY) / CELL_SIZE);

      if (col < 0 || col >= MAP_W || row < 0 || row >= MAP_H) return;

      this.handlePlayerAction(col, row);
    });
  }

  // ─── TURN FLOW ──────────────────────────────────────────────────────────────

  private handlePlayerAction(targetCol: number, targetRow: number) {
    const playerPos = this.world.getComponent<Position>(this.playerId, 'position')!;

    // Clicking own tile does nothing
    if (targetCol === playerPos.col && targetRow === playerPos.row) return;

    // If an enemy is at target, attack directly (if in range)
    const enemyAt = this.getActorAt(targetCol, targetRow, 'enemy');
    const dx = Math.abs(targetCol - playerPos.col);
    const dy = Math.abs(targetRow - playerPos.row);

    if (enemyAt !== null && dx <= 1 && dy <= 1) {
      this.performAttack(this.playerId, enemyAt);
      this.endPlayerTurn();
      return;
    }

    // Otherwise pathfind
    if (!this.mapData.walkable[targetRow]?.[targetCol]) return;

    const path = this.findPath(playerPos.col, playerPos.row, targetCol, targetRow, this.playerId);
    if (path.length < 2) return;

    const [nextCol, nextRow] = path[1];

    // Bump-attack if next step has an enemy
    const bumpEnemy = this.getActorAt(nextCol, nextRow, 'enemy');
    if (bumpEnemy !== null) {
      this.performAttack(this.playerId, bumpEnemy);
    } else {
      this.moveEntity(this.playerId, nextCol, nextRow);
    }

    this.endPlayerTurn();
  }

  private endPlayerTurn() {
    this.turnCount++;
    this.phase = 'enemy';
    // Brief delay so the player can see their action before enemies move
    setTimeout(() => this.processEnemyTurns(), 120);
  }

  private processEnemyTurns() {
    if (!this.world.hasComponent(this.playerId, 'health')) {
      this.phase = 'game_over';
      return;
    }

    const playerPos = this.world.getComponent<Position>(this.playerId, 'position')!;

    const enemies = this.world.query('position', 'faction', 'health', 'combat').filter(id => {
      const f = this.world.getComponent<Faction>(id, 'faction')!;
      return f.team === 'enemy';
    });

    for (const enemyId of enemies) {
      const ePos = this.world.getComponent<Position>(enemyId, 'position')!;
      const dx = Math.abs(playerPos.col - ePos.col);
      const dy = Math.abs(playerPos.row - ePos.row);

      if (dx <= 1 && dy <= 1) {
        // Adjacent — attack
        this.performAttack(enemyId, this.playerId);
        // Player might have died
        if (!this.world.hasComponent(this.playerId, 'health')) break;
      } else {
        // Move one step toward player
        const path = this.findPath(ePos.col, ePos.row, playerPos.col, playerPos.row, enemyId);
        if (path.length >= 2) {
          const [nextCol, nextRow] = path[1];
          // Don't step onto player tile (attack handled above next turn)
          if (nextCol !== playerPos.col || nextRow !== playerPos.row) {
            this.moveEntity(enemyId, nextCol, nextRow);
          }
        }
      }
    }

    this.phase = this.world.hasComponent(this.playerId, 'health') ? 'player' : 'game_over';
    if (this.phase === 'game_over') {
      this.addMessage('You have died. Refresh to play again.');
    }
  }

  // ─── COMBAT ─────────────────────────────────────────────────────────────────

  private performAttack(attackerId: number, defenderId: number) {
    const atkCombat = this.world.getComponent<Combat>(attackerId, 'combat')!;
    const defCombat = this.world.getComponent<Combat>(defenderId, 'combat');
    const defHealth = this.world.getComponent<Health>(defenderId, 'health');
    if (!defHealth) return;

    const defense = defCombat?.defense ?? 0;
    const damage = Math.max(1, atkCombat.attackPower + Math.floor(Math.random() * 6) - defense);
    defHealth.current -= damage;

    const isPlayer = attackerId === this.playerId;
    this.addMessage(isPlayer
      ? `You hit for ${damage}!`
      : `Enemy hits you for ${damage}!`);

    if (defHealth.current <= 0) {
      if (defenderId === this.playerId) {
        this.addMessage('YOU DIED. Refresh to restart.');
        this.phase = 'game_over';
        this.world.removeComponent(this.playerId, 'health');
      } else {
        this.addMessage('Enemy slain!');
        const pos = this.world.getComponent<Position>(defenderId, 'position')!;
        this.restoreTerrain(pos.col, pos.row);
        this.world.removeEntity(defenderId);
      }
    }
  }

  // ─── MOVEMENT & RENDERING ───────────────────────────────────────────────────

  private moveEntity(entityId: number, newCol: number, newRow: number) {
    const pos = this.world.getComponent<Position>(entityId, 'position')!;
    const rend = this.world.getComponent<Renderable>(entityId, 'renderable')!;

    // Erase from old tile
    this.restoreTerrain(pos.col, pos.row);

    // Update ECS
    pos.col = newCol;
    pos.row = newRow;

    // Draw at new tile
    this.renderer.setCell('gameplay', newCol, newRow, {
      char: rend.char,
      fg: rend.fg,
      alpha: rend.alpha ?? 1.0,
    });
  }

  private restoreTerrain(col: number, row: number) {
    const cell = this.mapData.gameplay[row]?.[col] ?? null;
    this.renderer.setCell('gameplay', col, row, cell);
  }

  private drawActors() {
    const actors = this.world.query('position', 'renderable', 'faction');
    for (const id of actors) {
      const pos = this.world.getComponent<Position>(id, 'position')!;
      const rend = this.world.getComponent<Renderable>(id, 'renderable')!;
      this.renderer.setCell('gameplay', pos.col, pos.row, {
        char: rend.char,
        fg: rend.fg,
        alpha: rend.alpha ?? 1.0,
      });
    }
  }

  // ─── PATHFINDING ────────────────────────────────────────────────────────────

  /**
   * Find a path from (fromCol, fromRow) to (toCol, toRow).
   * The mover entity's own position is always passable.
   * Returns array of [col, row] pairs including start; empty if no path.
   */
  private findPath(
    fromCol: number,
    fromRow: number,
    toCol: number,
    toRow: number,
    moverId: number,
  ): [number, number][] {
    const moverPos = this.world.getComponent<Position>(moverId, 'position')!;

    const passable = (x: number, y: number): boolean => {
      // Out of bounds
      if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W) return false;
      // Wall
      if (!this.mapData.walkable[y][x]) return false;
      // Occupied by another actor (except the mover itself and the target tile)
      if (x === moverPos.col && y === moverPos.row) return true;
      const occupant = this.getActorAt(x, y);
      return occupant === null || occupant === moverId;
    };

    const path: [number, number][] = [];
    const astar = new ROT.Path.AStar(toCol, toRow, (x, y) => passable(x, y));
    astar.compute(fromCol, fromRow, (x, y) => path.push([x, y]));
    return path;
  }

  // ─── HELPERS ────────────────────────────────────────────────────────────────

  /** Returns entity ID of any actor (with faction) at (col, row), optionally filtered by team. */
  private getActorAt(col: number, row: number, team?: string): number | null {
    const actors = this.world.query('position', 'faction');
    for (const id of actors) {
      const pos = this.world.getComponent<Position>(id, 'position')!;
      if (pos.col !== col || pos.row !== row) continue;
      if (team) {
        const faction = this.world.getComponent<Faction>(id, 'faction')!;
        if (faction.team !== team) continue;
      }
      return id;
    }
    return null;
  }

  // ─── MAIN LOOP ──────────────────────────────────────────────────────────────

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
