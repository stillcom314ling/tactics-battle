/**
 * RADIAL WHEEL — Hold-and-drag marking menu
 *
 * Press FAB → wheel opens centered on press point.
 * Drag outward into inner ring → hover a root item (angle determines which).
 * Drag further into outer ring → hover a child of that root, in the same angular
 *   sector as the parent (sector subdivided among children).
 * Release → activate the deepest hovered item.
 * Release in center dead zone → cancel.
 *
 * Public API (all input driven externally):
 *   wheel.open(nodes, cx, cy, onClose?)
 *   wheel.update(gx, gy)   ← call on every pointermove
 *   wheel.commit()         ← call on pointerup
 *   wheel.close()
 *   wheel.isOpen
 */

import { Container, Graphics, Text, TextStyle } from 'pixi.js';

export interface WheelNode {
  id: string;
  label: string;     // short display text (max ~8 chars)
  icon: string;      // single glyph shown in segment centre
  color: number;     // segment fill colour
  children?: WheelNode[];
  action?: () => void;
  disabled?: boolean;
  badge?: string;    // small overlay (e.g. charge count)
  tooltip?: string;  // description shown when hovered (multiline ok)
}

// Ring radii
const INNER_R = 44;   // dead-zone / centre button
const MID_R   = 118;  // inner ring (roots) outer edge
const OUTER_R = 198;  // outer ring (children) outer edge
const BD_R    = OUTER_R + 32; // semi-transparent backdrop

const GAP     = 0.045; // radians of gap between each segment
const START   = -Math.PI / 2; // "up" = first segment

export class RadialWheel {
  public container: Container;
  private gfx: Graphics;
  private textLayer: Container;

  private nodes: WheelNode[] = [];
  private cx = 0;
  private cy = 0;
  private _isOpen = false;
  private onCloseCb: (() => void) | null = null;

  private hoveredRootIdx  = -1;
  private hoveredChildIdx = -1;

  constructor() {
    this.container = new Container();
    this.container.visible = false;
    // All input handled externally — this container is render-only.
    this.container.eventMode = 'none';

    this.gfx = new Graphics();
    this.textLayer = new Container();
    this.container.addChild(this.gfx);
    this.container.addChild(this.textLayer);
  }

  // ─── Public API ──────────────────────────────────────────────────────────

  open(nodes: WheelNode[], cx: number, cy: number, onClose?: () => void) {
    this.nodes = nodes;
    this.cx = cx;
    this.cy = cy;
    this._isOpen = true;
    this.onCloseCb = onClose ?? null;
    this.hoveredRootIdx  = -1;
    this.hoveredChildIdx = -1;
    this.container.visible = true;
    this.draw();
  }

  /** Call on every pointermove while the wheel is active. */
  update(gx: number, gy: number) {
    if (!this._isOpen) return;

    const dx = gx - this.cx;
    const dy = gy - this.cy;
    const dist = Math.sqrt(dx * dx + dy * dy);

    if (dist < INNER_R) {
      this.hoveredRootIdx  = -1;
      this.hoveredChildIdx = -1;
    } else {
      const n = this.nodes.length;
      const fullStep = (Math.PI * 2) / n;

      // Normalise angle to [0, 2π) with START as origin
      let norm = Math.atan2(dy, dx) - START;
      if (norm < 0) norm += Math.PI * 2;

      this.hoveredRootIdx = Math.floor(norm / fullStep) % n;

      const root = this.nodes[this.hoveredRootIdx];
      if (dist >= MID_R && root?.children?.length) {
        const nc = root.children.length;
        const rootStart  = this.hoveredRootIdx * fullStep;
        // Offset within this root's sector, clamped to [0, fullStep)
        const withinRoot = ((norm - rootStart) % fullStep + fullStep) % fullStep;
        const childStep  = fullStep / nc;
        this.hoveredChildIdx = Math.floor(withinRoot / childStep) % nc;
      } else {
        this.hoveredChildIdx = -1;
      }
    }

    this.draw();
  }

  /** Call on pointerup. Activates the deepest hovered item then closes. */
  commit() {
    if (!this._isOpen) return;

    if (this.hoveredRootIdx >= 0) {
      const root = this.nodes[this.hoveredRootIdx];
      if (root && !root.disabled) {
        if (root.children?.length && this.hoveredChildIdx >= 0) {
          const child = root.children[this.hoveredChildIdx];
          if (child && !child.disabled) child.action?.();
        } else if (!root.children?.length) {
          root.action?.();
        }
      }
    }

    this.close();
  }

  close() {
    this._isOpen = false;
    this.container.visible = false;
    this.gfx.clear();
    this.textLayer.removeChildren();
    this.onCloseCb?.();
    this.onCloseCb = null;
  }

  get isOpen() { return this._isOpen; }

  // ─── Drawing ─────────────────────────────────────────────────────────────

  private draw() {
    this.gfx.clear();
    this.textLayer.removeChildren();

    const { cx, cy } = this;
    const n = this.nodes.length;
    const fullStep = (Math.PI * 2) / n;

    // Backdrop
    this.gfx.circle(cx, cy, BD_R).fill({ color: 0x000000, alpha: 0.74 });

    // ── Inner ring: root items ──────────────────────────────────────────
    for (let i = 0; i < n; i++) {
      const node      = this.nodes[i];
      const a0        = START + i * fullStep + GAP;
      const a1        = START + (i + 1) * fullStep - GAP;
      const mid       = (a0 + a1) / 2;
      const isHov     = i === this.hoveredRootIdx;
      const color     = node.disabled ? 0x333333 : node.color;
      const fillAlpha = node.disabled ? 0.35 : isHov ? 0.92 : 0.52;

      this.donutSector(cx, cy, INNER_R, MID_R, a0, a1, color, fillAlpha, isHov);

      const lr = (INNER_R + MID_R) / 2 + 2;
      const lx = cx + Math.cos(mid) * lr;
      const ly = cy + Math.sin(mid) * lr;

      const iconFill = node.disabled ? 0x444444 : isHov ? 0xffffff : 0xbbbbbb;
      this.txt(node.icon, lx, ly - 8, 18, iconFill, true);

      const lblFill = node.disabled ? 0x444444 : isHov ? 0xffffff : 0x888888;
      const lbl = node.badge ? `${node.label}\n${node.badge}` : node.label;
      this.txt(lbl, lx, ly + 9, 8, lblFill, false);

      // Arrow hint on segments with children
      if (node.children?.length && !node.disabled) {
        const ar = MID_R - 9;
        this.txt('▸', cx + Math.cos(mid) * ar, cy + Math.sin(mid) * ar, 9,
          isHov ? 0xffffff : 0x555555, true);
      }
    }

    // ── Outer ring: children of hovered root ───────────────────────────
    if (this.hoveredRootIdx >= 0) {
      const root = this.nodes[this.hoveredRootIdx];
      if (root?.children?.length) {
        const nc        = root.children.length;
        const rootStart = START + this.hoveredRootIdx * fullStep;
        const childStep = fullStep / nc;

        for (let j = 0; j < nc; j++) {
          const child     = root.children[j];
          const a0        = rootStart + j * childStep + GAP * 0.5;
          const a1        = rootStart + (j + 1) * childStep - GAP * 0.5;
          const mid       = (a0 + a1) / 2;
          const isHov     = j === this.hoveredChildIdx;
          const color     = child.disabled ? 0x333333 : child.color;
          const fillAlpha = child.disabled ? 0.30 : isHov ? 0.92 : 0.45;

          this.donutSector(cx, cy, MID_R, OUTER_R, a0, a1, color, fillAlpha, isHov);

          const lr = (MID_R + OUTER_R) / 2 + 2;
          const lx = cx + Math.cos(mid) * lr;
          const ly = cy + Math.sin(mid) * lr;

          const iconFill = child.disabled ? 0x444444 : isHov ? 0xffffff : 0x999999;
          this.txt(child.icon, lx, ly - 7, 16, iconFill, true);

          const lblFill = child.disabled ? 0x444444 : isHov ? 0xffffff : 0x777777;
          const lbl = child.badge ? `${child.label}\n${child.badge}` : child.label;
          this.txt(lbl, lx, ly + 8, 8, lblFill, false);
        }
      }
    }

    // ── Spell description tooltip ──────────────────────────────────────
    // Show tooltip for the currently hovered spell child (or root if leaf)
    let tooltipText: string | undefined;
    let tooltipColor = 0xffffff;
    if (this.hoveredRootIdx >= 0) {
      const hovRoot = this.nodes[this.hoveredRootIdx];
      if (hovRoot?.children?.length && this.hoveredChildIdx >= 0) {
        const hovChild = hovRoot.children[this.hoveredChildIdx];
        tooltipText = hovChild?.tooltip;
        tooltipColor = hovChild?.color ?? 0xffffff;
      } else if (!hovRoot?.children?.length) {
        tooltipText = hovRoot?.tooltip;
        tooltipColor = hovRoot?.color ?? 0xffffff;
      }
    }
    if (tooltipText) {
      const tipY = cy + OUTER_R + 18;
      const tipW = 220;
      const tipH = 52;
      this.gfx
        .roundRect(cx - tipW / 2, tipY, tipW, tipH, 6)
        .fill({ color: 0x0d1825, alpha: 0.93 })
        .stroke({ color: tooltipColor, alpha: 0.55, width: 1.5 });
      this.txt(tooltipText, cx, tipY + tipH / 2, 11, 0xddeeff, false);
    }

    // ── Centre button ──────────────────────────────────────────────────
    this.gfx
      .circle(cx, cy, INNER_R - 5)
      .fill({ color: 0x0d1a2e, alpha: 0.92 })
      .stroke({ color: 0x3355aa, alpha: 0.7, width: 1.5 });
    this.txt('✕', cx, cy, 15, 0x4466bb, true);
  }

  private donutSector(
    cx: number, cy: number,
    r0: number, r1: number,
    a0: number, a1: number,
    color: number, alpha: number,
    glow: boolean,
  ) {
    this.gfx
      .moveTo(cx + Math.cos(a0) * r0, cy + Math.sin(a0) * r0)
      .arc(cx, cy, r1, a0, a1)
      .arc(cx, cy, r0, a1, a0, true)
      .closePath()
      .fill({ color, alpha })
      .stroke({ color: 0xffffff, alpha: glow ? 0.55 : 0.10, width: glow ? 2 : 1 });
  }

  private txt(str: string, x: number, y: number, size: number, fill: number, bold: boolean) {
    const t = new Text({
      text: str,
      style: new TextStyle({
        fontFamily: '"Courier New", monospace',
        fontSize: size,
        fill,
        fontWeight: bold ? 'bold' : 'normal',
        align: 'center',
      }),
    });
    t.anchor.set(0.5);
    t.x = x;
    t.y = y;
    this.textLayer.addChild(t);
  }
}
