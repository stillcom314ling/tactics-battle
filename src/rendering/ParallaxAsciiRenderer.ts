import { Container, Text, TextStyle } from 'pixi.js';

/**
 * PARALLAX ASCII RENDERER
 * 
 * This is the core visual system. It renders ASCII characters on multiple
 * depth layers. When the camera pans, each layer moves at a different speed
 * based on its depth, creating a 3D parallax effect.
 * 
 * Layer 0 (far background): moves slowest, smaller/darker chars
 * Layer 1 (mid background): atmospheric detail
 * Layer 2 (gameplay layer): where units and terrain live, 1:1 with camera
 * Layer 3 (foreground):     moves faster than camera, creates depth pop
 * 
 * Each cell in each layer holds a single ASCII character with:
 *   - char: the glyph to display
 *   - fg: foreground color
 *   - bg: background color (optional)
 *   - scale: size multiplier (deeper layers = smaller)
 */

export interface AsciiCell {
  char: string;
  fg: number;       // hex color e.g. 0xffffff
  bg?: number;      // optional background
  alpha?: number;    // 0-1
}

export interface LayerConfig {
  id: string;
  /** Depth factor: 0 = infinitely far (no movement), 1 = gameplay layer, >1 = foreground */
  depth: number;
  /** Grid dimensions for this layer */
  cols: number;
  rows: number;
  /** Character size in pixels */
  cellSize: number;
  /** Base alpha for the whole layer */
  alpha: number;
  /** Color tint applied to all chars (simulates atmospheric depth) */
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

  // Camera position in world space (gameplay layer coordinates)
  public cameraX: number = 0;
  public cameraY: number = 0;

  private defaultStyle: TextStyle;

  constructor() {
    this.root = new Container();
    this.defaultStyle = new TextStyle({
      fontFamily: '"Courier New", "Consolas", "Monaco", monospace',
      fontSize: 16,
      fill: 0xffffff,
      fontWeight: 'bold',
    });
  }

  /**
   * Add a depth layer to the renderer.
   */
  addLayer(config: LayerConfig): LayerState {
    const container = new Container();
    container.alpha = config.alpha;
    this.root.addChild(container);

    const cells: (Text | null)[][] = [];
    const data: (AsciiCell | null)[][] = [];

    for (let row = 0; row < config.rows; row++) {
      cells[row] = [];
      data[row] = [];
      for (let col = 0; col < config.cols; col++) {
        cells[row][col] = null;
        data[row][col] = null;
      }
    }

    const state: LayerState = { config, container, cells, data };
    this.layers.set(config.id, state);

    // Sort children by depth so far layers render behind near layers
    const sorted = [...this.layers.values()].sort(
      (a, b) => a.config.depth - b.config.depth
    );
    for (let i = 0; i < sorted.length; i++) {
      this.root.setChildIndex(sorted[i].container, i);
    }

    return state;
  }

  /**
   * Set a cell's content on a specific layer.
   */
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
    text.x = col * layer.config.cellSize;
    text.y = row * layer.config.cellSize;
    text.alpha = cell.alpha ?? 1;

    if (layer.config.tint !== undefined) {
      text.tint = layer.config.tint;
    }

    layer.cells[row][col] = text;
    layer.container.addChild(text);
  }

  /**
   * Bulk-set an entire layer from a 2D array.
   */
  setLayerData(layerId: string, data: (AsciiCell | null)[][]) {
    const layer = this.layers.get(layerId);
    if (!layer) return;

    for (let row = 0; row < Math.min(data.length, layer.config.rows); row++) {
      for (let col = 0; col < Math.min(data[row].length, layer.config.cols); col++) {
        this.setCell(layerId, col, row, data[row][col]);
      }
    }
  }

  /**
   * Clear all cells on a layer.
   */
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
   * Call this every frame.
   */
  updateCamera(screenWidth: number, screenHeight: number) {
    for (const layer of this.layers.values()) {
      // Parallax: layer offset = camera * depth factor
      // depth=1 means the layer moves 1:1 with camera (gameplay layer)
      // depth<1 means it moves slower (background, feels far away)
      // depth>1 means it moves faster (foreground, feels close)
      const offsetX = -this.cameraX * layer.config.depth;
      const offsetY = -this.cameraY * layer.config.depth;

      // Center the layer on screen, then apply parallax offset
      const layerPixelWidth = layer.config.cols * layer.config.cellSize;
      const layerPixelHeight = layer.config.rows * layer.config.cellSize;

      layer.container.x = (screenWidth - layerPixelWidth) / 2 + offsetX;
      layer.container.y = (screenHeight - layerPixelHeight) / 2 + offsetY;
    }
  }

  /**
   * Get the layer state for direct manipulation.
   */
  getLayer(layerId: string): LayerState | undefined {
    return this.layers.get(layerId);
  }
}
