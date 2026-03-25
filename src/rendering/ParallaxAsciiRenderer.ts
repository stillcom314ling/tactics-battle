/**
 * PARALLAX ASCII RENDERER
 *
 * Core visual system. Renders ASCII characters on multiple depth layers.
 * When the camera pans, each layer moves at a different speed based on its
 * depth, creating a 3D parallax effect.
 *
 * DEPTH SEMANTICS
 * ───────────────
 * depth < 1  → slower than camera (feels far away / background)
 * depth = 1  → 1:1 with camera   (gameplay layer)
 * depth > 1  → faster than camera (feels close / foreground)
 *
 * Each cell holds a single ASCII character with fg color, optional bg, and alpha.
 */

import { Container, Text, TextStyle } from 'pixi.js';
import { ASCII_FONT_FAMILY } from '../constants/rendering';

export interface AsciiCell {
  char: string;
  fg: number;       // hex color e.g. 0xffffff
  bg?: number;      // optional background tint
  alpha?: number;   // 0–1, defaults to 1
}

export interface LayerConfig {
  id: string;
  /** Depth factor: 0 = infinitely far, 1 = gameplay, >1 = foreground */
  depth: number;
  cols: number;
  rows: number;
  /** Character size in pixels */
  cellSize: number;
  /** Base alpha for the whole layer */
  alpha: number;
  /** Color tint applied to all chars (atmospheric depth simulation) */
  tint?: number;
}

export interface LayerState {
  config: LayerConfig;
  container: Container;
  cells: (Text | null)[][];
  data: (AsciiCell | null)[][];
}

export class ParallaxAsciiRenderer {
  public root: Container;
  public layers: Map<string, LayerState> = new Map();

  public cameraX: number = 0;
  public cameraY: number = 0;

  private defaultStyle: TextStyle;

  constructor() {
    this.root = new Container();
    this.defaultStyle = new TextStyle({
      fontFamily: ASCII_FONT_FAMILY,
      fontSize: 16,
      fill: 0xffffff,
      fontWeight: 'bold',
    });
  }

  /** Add a depth layer to the renderer. Layers are sorted by depth automatically. */
  addLayer(config: LayerConfig): LayerState {
    const container = new Container();
    container.alpha = config.alpha;
    this.root.addChild(container);

    const cells: (Text | null)[][] = [];
    const data:  (AsciiCell | null)[][] = [];

    for (let row = 0; row < config.rows; row++) {
      cells[row] = [];
      data[row]  = [];
      for (let col = 0; col < config.cols; col++) {
        cells[row][col] = null;
        data[row][col]  = null;
      }
    }

    const state: LayerState = { config, container, cells, data };
    this.layers.set(config.id, state);

    // Keep children sorted by depth so far layers render behind near layers
    const sorted = [...this.layers.values()].sort((a, b) => a.config.depth - b.config.depth);
    for (let i = 0; i < sorted.length; i++) {
      this.root.setChildIndex(sorted[i].container, i);
    }

    return state;
  }

  /** Set a single cell's content. Pass null to clear. */
  setCell(layerId: string, col: number, row: number, cell: AsciiCell | null) {
    const layer = this.layers.get(layerId);
    if (!layer) return;
    if (col < 0 || col >= layer.config.cols || row < 0 || row >= layer.config.rows) return;

    layer.data[row][col] = cell;

    // Remove existing text object
    if (layer.cells[row][col]) {
      layer.container.removeChild(layer.cells[row][col]!);
      layer.cells[row][col]!.destroy();
      layer.cells[row][col] = null;
    }

    if (!cell) return;

    const style = new TextStyle({
      fontFamily: this.defaultStyle.fontFamily,
      fontSize: layer.config.cellSize,
      fill: cell.fg,
      fontWeight: 'bold',
    });

    const text = new Text({ text: cell.char, style });
    text.x     = col * layer.config.cellSize;
    text.y     = row * layer.config.cellSize;
    text.alpha = cell.alpha ?? 1;

    if (layer.config.tint !== undefined) {
      text.tint = layer.config.tint;
    }

    layer.cells[row][col] = text;
    layer.container.addChild(text);
  }

  /** Bulk-set an entire layer from a 2D array. */
  setLayerData(layerId: string, data: (AsciiCell | null)[][]) {
    const layer = this.layers.get(layerId);
    if (!layer) return;
    for (let row = 0; row < Math.min(data.length, layer.config.rows); row++) {
      for (let col = 0; col < Math.min(data[row].length, layer.config.cols); col++) {
        this.setCell(layerId, col, row, data[row][col]);
      }
    }
  }

  /** Clear all cells on a layer. */
  clearLayer(layerId: string) {
    const layer = this.layers.get(layerId);
    if (!layer) return;
    for (let row = 0; row < layer.config.rows; row++) {
      for (let col = 0; col < layer.config.cols; col++) {
        if (layer.cells[row][col]) {
          layer.container.removeChild(layer.cells[row][col]!);
          layer.cells[row][col]!.destroy();
          layer.cells[row][col] = null;
        }
        layer.data[row][col] = null;
      }
    }
  }

  /**
   * Update camera position and apply parallax offsets to each layer.
   * Call this every frame after setting cameraX/cameraY.
   */
  updateCamera(screenWidth: number, screenHeight: number) {
    for (const layer of this.layers.values()) {
      // depth=1 → moves 1:1 with camera; depth<1 → slower; depth>1 → faster
      const offsetX = -this.cameraX * layer.config.depth;
      const offsetY = -this.cameraY * layer.config.depth;

      const layerPixelW = layer.config.cols * layer.config.cellSize;
      const layerPixelH = layer.config.rows * layer.config.cellSize;

      layer.container.x = (screenWidth  - layerPixelW) / 2 + offsetX;
      layer.container.y = (screenHeight - layerPixelH) / 2 + offsetY;
    }
  }

  /** Get a layer for direct manipulation. */
  getLayer(layerId: string): LayerState | undefined {
    return this.layers.get(layerId);
  }

  /**
   * Returns the current screen-space origin and cell size for a layer.
   * Useful for drawing UI overlays aligned to tile positions.
   * Call after updateCamera() each frame.
   */
  getLayerScreenOffset(layerId: string): { x: number; y: number; cellSize: number } {
    const layer = this.layers.get(layerId);
    if (!layer) return { x: 0, y: 0, cellSize: 16 };
    return {
      x:        layer.container.x,
      y:        layer.container.y,
      cellSize: layer.config.cellSize,
    };
  }
}
