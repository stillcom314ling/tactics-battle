/**
 * GAME
 * 
 * Top-level orchestrator. Initializes PixiJS, the parallax renderer,
 * the ECS world, map generation, and the game loop.
 * 
 * This is the file Claude Code should start from when understanding
 * the project structure.
 * 
 * Architecture overview:
 *   index.html -> main.ts -> Game.init() -> Game.start()
 *   
 *   Rendering:  ParallaxAsciiRenderer (multiple depth layers of ASCII)
 *               CameraController (pan with mouse/touch, inertia)
 *   
 *   Game logic: World (ECS - entities, components)
 *               TurnManager (turn-based flow via rot.js scheduler)
 *               InteractionSystem (emergent mechanic rules)
 *               MapGenerator (rot.js dungeon generation)
 *               RenderSystem (syncs ECS -> renderer each frame)
 */

import { Application } from 'pixi.js';
import { ParallaxAsciiRenderer } from '../rendering/ParallaxAsciiRenderer';
import { CameraController } from '../rendering/CameraController';
import { World } from '../core/ECS';
import { TurnManager } from '../core/TurnManager';
import { generateMap, populateWorld } from './MapGenerator';
import { renderEntities } from './RenderSystem';

// Map dimensions
const MAP_W = 48;
const MAP_H = 32;
const CELL_SIZE = 16;

export class Game {
  private app!: Application;
  private renderer!: ParallaxAsciiRenderer;
  private camera!: CameraController;
  private world!: World;
  private turnManager!: TurnManager;
  private playerId!: number;

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

    // --- ECS World ---
    this.world = new World();

    // --- Generate Map ---
    const map = generateMap(MAP_W, MAP_H);

    // Populate background layers
    this.renderer.setLayerData('bg_far', map.bgFar);
    this.renderer.setLayerData('bg_mid', map.bgMid);
    this.renderer.setLayerData('foreground', map.foreground);

    // Populate gameplay layer with terrain
    this.renderer.setLayerData('gameplay', map.gameplay);

    // Populate ECS world with entities
    const { playerId } = populateWorld(this.world, map);
    this.playerId = playerId;

    // --- Turn Manager ---
    this.turnManager = new TurnManager(this.world);

    // Render entities on top of terrain
    renderEntities(this.world, this.renderer);

    console.log('[game] initialized. Pan by dragging. Parallax depth is visible on camera movement.');
  }

  start() {
    this.app.ticker.add(() => this.loop());
  }

  private loop() {
    // Update camera from input
    this.camera.update();
    this.renderer.cameraX = this.camera.state.x;
    this.renderer.cameraY = this.camera.state.y;

    // Apply parallax
    this.renderer.updateCamera(this.app.screen.width, this.app.screen.height);
  }
}
