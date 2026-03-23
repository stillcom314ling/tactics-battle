/**
 * RADIAL WHEEL
 *
 * Nested radial action menu for mobile-first spell/action selection.
 *
 * Structure:
 *   Root wheel → sub-wheels → leaf actions
 *   Tapping center = back (or close if at root)
 *   Tapping outside = close
 *   Tapping a node with children = drill in
 *   Tapping a leaf node = run action() and close
 *
 * Usage:
 *   wheel.open(rootNodes, cx, cy, onClose?)
 *   wheel.close()
 *   The wheel.container must be added to the PIXI stage.
 */

import { Container, Graphics, Text, TextStyle } from 'pixi.js';

export interface WheelNode {
  id: string;
  label: string;       // short text shown below icon (max ~8 chars)
  icon: string;        // single ASCII/unicode char displayed in segment
  color: number;       // segment fill color
  children?: WheelNode[];
  action?: () => void; // leaf: called when tapped
  disabled?: boolean;  // grayed out (e.g. spell on cooldown)
  badge?: string;      // small label (e.g. cooldown number)
}

const INNER_R   = 38;  // center button radius
const OUTER_R   = 140; // outer edge of segments
const BACKDROP_R = OUTER_R + 24; // semi-transparent backdrop radius
const GAP       = 0.06; // radians of gap between segments

export class RadialWheel {
  public container: Container;
  private gfx: Graphics;
  private textLayer: Container;
  private stack: WheelNode[][] = [];
  private cx: number = 0;
  private cy: number = 0;
  private onClose: (() => void) | null = null;

  constructor() {
    this.container = new Container();
    this.container.visible = false;
    this.container.eventMode = 'static';

    this.gfx = new Graphics();
    this.textLayer = new Container();
    this.container.addChild(this.gfx);
    this.container.addChild(this.textLayer);
  }

  /** Open the wheel at screen position (cx, cy). */
  open(rootNodes: WheelNode[], cx: number, cy: number, onClose?: () => void) {
    this.stack = [rootNodes];
    this.cx = cx;
    this.cy = cy;
    this.onClose = onClose ?? null;
    this.container.visible = true;
    this.draw();
  }

  close() {
    this.container.visible = false;
    this.stack = [];
    this.clear();
    this.onClose?.();
    this.onClose = null;
  }

  get isOpen() { return this.container.visible; }

  // ─── Drawing ──────────────────────────────────────────────────────────────

  private clear() {
    this.gfx.clear();
    this.textLayer.removeChildren();
  }

  private draw() {
    this.clear();
    const nodes = this.stack[this.stack.length - 1];
    const n = nodes.length;
    const step = (Math.PI * 2) / n;
    const start = -Math.PI / 2; // top
    const cx = this.cx, cy = this.cy;

    // Backdrop
    this.gfx
      .circle(cx, cy, BACKDROP_R)
      .fill({ color: 0x000000, alpha: 0.72 });

    // Segments
    for (let i = 0; i < n; i++) {
      const node = nodes[i];
      const a0 = start + i * step + GAP;
      const a1 = start + (i + 1) * step - GAP;
      const mid = (a0 + a1) / 2;
      const color = node.disabled ? 0x333333 : node.color;
      const alpha = node.disabled ? 0.55 : 0.88;

      // Segment shape: donut sector
      this.gfx
        .moveTo(cx + Math.cos(a0) * INNER_R, cy + Math.sin(a0) * INNER_R)
        .arc(cx, cy, OUTER_R, a0, a1)
        .arc(cx, cy, INNER_R, a1, a0, true)
        .closePath()
        .fill({ color, alpha })
        .stroke({ color: 0xffffff, alpha: 0.18, width: 1.5 });

      // Icon + label text
      const labelR = (INNER_R + OUTER_R) / 2 + 4;
      const lx = cx + Math.cos(mid) * labelR;
      const ly = cy + Math.sin(mid) * labelR;

      const iconFill = node.disabled ? 0x555555 : 0xffffff;
      this.addText(node.icon, lx, ly - 9, 20, iconFill, true);

      const lblFill = node.disabled ? 0x555555 : 0xdddddd;
      const lbl = node.badge ? `${node.label}\n${node.badge}` : node.label;
      this.addText(lbl, lx, ly + 11, 9, lblFill, false);
    }

    // Center button: back or close
    const isRoot = this.stack.length <= 1;
    const centerColor = isRoot ? 0x223366 : 0x442222;
    const centerIcon  = isRoot ? '✕' : '←';
    const centerFill  = isRoot ? 0x7799ff : 0xff9977;

    this.gfx.circle(cx, cy, INNER_R).fill({ color: centerColor, alpha: 0.92 })
      .stroke({ color: 0xffffff, alpha: 0.2, width: 1 });
    this.addText(centerIcon, cx, cy, 18, centerFill, true);

    // Depth breadcrumbs
    if (this.stack.length > 1) {
      const dots = '● '.repeat(this.stack.length - 1).trim();
      this.addText(dots, cx, cy - BACKDROP_R + 12, 10, 0x6688bb, true);
    }

    // Input handler
    this.container.removeAllListeners();
    this.container.on('pointerdown', (e) => this.onTap(e.global.x, e.global.y, nodes, step, start, n));
  }

  private addText(str: string, x: number, y: number, size: number, fill: number, bold: boolean) {
    const style = new TextStyle({
      fontFamily: '"Courier New", monospace',
      fontSize: size,
      fill,
      fontWeight: bold ? 'bold' : 'normal',
      align: 'center',
    });
    const t = new Text({ text: str, style });
    t.anchor.set(0.5);
    t.x = x;
    t.y = y;
    this.textLayer.addChild(t);
  }

  // ─── Input ────────────────────────────────────────────────────────────────

  private onTap(gx: number, gy: number, nodes: WheelNode[], step: number, start: number, n: number) {
    const lx = gx - this.cx, ly = gy - this.cy;
    const dist = Math.sqrt(lx * lx + ly * ly);

    // Center button
    if (dist < INNER_R) {
      if (this.stack.length <= 1) this.close();
      else { this.stack.pop(); this.draw(); }
      return;
    }

    // Outside backdrop — close
    if (dist > BACKDROP_R) {
      this.close();
      return;
    }

    // Segment
    if (dist >= INNER_R && dist <= OUTER_R) {
      let angle = Math.atan2(ly, lx) - start;
      if (angle < 0) angle += Math.PI * 2;
      const idx = Math.floor(angle / step);
      if (idx >= 0 && idx < n) {
        this.activate(nodes[idx]);
      }
    }
  }

  private activate(node: WheelNode) {
    if (node.disabled) return;
    if (node.children && node.children.length > 0) {
      this.stack.push(node.children);
      this.draw();
    } else if (node.action) {
      node.action();
      this.close();
    }
  }
}
