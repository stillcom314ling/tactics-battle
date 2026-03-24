/**
 * ACTION MENU — Tap-to-toggle popup for selecting spells or passives
 *
 * Two sections:
 *   Actives  — spell buttons; tapping one calls action() and closes menu
 *   Passives — placeholder checklist (non-interactive for now)
 *
 * Scrollable: drag up/down on the panel to scroll content.
 *
 * Public API:
 *   menu.open(actives, screenW, screenH)
 *   menu.close()
 *   menu.isOpen
 */

import { Container, Graphics, Text, TextStyle } from 'pixi.js';

export interface ActiveItem {
  id: string;
  name: string;
  element: string;
  charges: number;
  chargesMax: number;
  color: number;
  tooltip?: string;
  action: () => void;
}

const PANEL_W   = 290;
const ROW_H     = 48;
const PAD       = 14;
const HEADER_H  = 48;

const PLACEHOLDER_PASSIVES = [
  'Speed Boost',
  'Tough Skin',
  'Mana Regen',
  'Arcane Ward',
];

export class ActionMenu {
  public container: Container;
  private backdrop: Graphics;
  private panelBg: Graphics;
  private scrollContent: Container;  // scrollable inner content
  private scrollMask: Graphics;
  private headerLayer: Container;    // fixed header (not scrolled)

  private _isOpen    = false;
  private scrollY    = 0;
  private contentH   = 0;
  private visibleH   = 0;

  private dragStartY = 0;
  private dragScrollStart = 0;
  private dragging   = false;

  private panelX = 0;
  private panelY = 0;
  private onCloseCb: (() => void) | null = null;

  constructor() {
    this.container = new Container();
    this.container.visible = false;
    this.container.eventMode = 'passive';

    // Backdrop catches taps outside panel
    this.backdrop = new Graphics();
    this.backdrop.eventMode = 'static';
    this.backdrop.on('pointerdown', () => this.close());
    this.container.addChild(this.backdrop);

    // Panel background (drawn each open)
    this.panelBg = new Graphics();
    this.panelBg.eventMode = 'none';
    this.container.addChild(this.panelBg);

    // Scrollable content area
    this.scrollContent = new Container();
    this.scrollContent.eventMode = 'passive';
    this.container.addChild(this.scrollContent);

    // Mask clips the scrollable area
    this.scrollMask = new Graphics();
    this.scrollMask.eventMode = 'none';
    this.container.addChild(this.scrollMask);
    this.scrollContent.mask = this.scrollMask;

    // Fixed header (title + close btn) sits above scrollContent
    this.headerLayer = new Container();
    this.headerLayer.eventMode = 'passive';
    this.container.addChild(this.headerLayer);

    // Drag-to-scroll listeners on the scroll content
    this.scrollContent.on('pointerdown', (e) => {
      this.dragging = true;
      this.dragStartY = e.global.y;
      this.dragScrollStart = this.scrollY;
    });
    window.addEventListener('pointermove', (e) => {
      if (!this.dragging) return;
      const dy = e.clientY - this.dragStartY;
      this.setScroll(this.dragScrollStart - dy);
    });
    window.addEventListener('pointerup', () => { this.dragging = false; });

    // Mouse wheel scroll
    document.addEventListener('wheel', (e) => {
      if (!this._isOpen) return;
      this.setScroll(this.scrollY + e.deltaY * 0.5);
    }, { passive: true });
  }

  get isOpen() { return this._isOpen; }

  open(actives: ActiveItem[], screenW: number, screenH: number, onClose?: () => void) {
    this._isOpen  = true;
    this.scrollY  = 0;
    this.onCloseCb = onClose ?? null;
    this.container.visible = true;

    // Backdrop
    this.backdrop.clear();
    this.backdrop.rect(0, 0, screenW, screenH).fill({ color: 0x000000, alpha: 0.55 });

    // Panel dimensions
    const maxPanelH = screenH - 80;
    const sectionLabelH = 26;
    const totalContentH =
      sectionLabelH + Math.max(1, actives.length) * ROW_H +
      PAD +
      sectionLabelH + PLACEHOLDER_PASSIVES.length * ROW_H +
      PAD;

    this.contentH  = totalContentH;
    this.visibleH  = Math.min(totalContentH, maxPanelH - HEADER_H);
    const panelH   = HEADER_H + this.visibleH;

    this.panelX = (screenW - PANEL_W) / 2;
    this.panelY = Math.max(20, (screenH - panelH) / 2);

    // Panel background
    this.panelBg.clear();
    this.panelBg.roundRect(this.panelX, this.panelY, PANEL_W, panelH, 10)
      .fill({ color: 0x0a1520, alpha: 0.97 })
      .stroke({ color: 0x3355aa, alpha: 0.75, width: 2 });

    // Scroll mask (clips content to panel below header)
    this.scrollMask.clear();
    this.scrollMask.rect(
      this.panelX, this.panelY + HEADER_H,
      PANEL_W, this.visibleH,
    ).fill({ color: 0xffffff, alpha: 1 });

    // Header
    this.buildHeader();

    // Scroll content
    this.scrollContent.removeChildren();
    this.scrollContent.x = this.panelX;
    this.scrollContent.y = this.panelY + HEADER_H;
    // make scroll area interactive
    this.scrollContent.eventMode = 'static';

    // Touch-through blocker so scroll area stops events reaching backdrop
    const blocker = new Graphics();
    blocker.rect(0, 0, PANEL_W, this.contentH).fill({ color: 0xffffff, alpha: 0 });
    blocker.eventMode = 'static';
    blocker.on('pointerdown', (e) => {
      this.dragging = true;
      this.dragStartY = (e as any).global?.y ?? (e as any).clientY ?? 0;
      this.dragScrollStart = this.scrollY;
      e.stopPropagation();
    });
    this.scrollContent.addChild(blocker);

    const contentGfx = new Graphics();
    this.scrollContent.addChild(contentGfx);

    let curY = 0;

    // Actives section
    this.sectionLabel('ACTIVES', curY, this.scrollContent);
    curY += sectionLabelH;

    if (actives.length === 0) {
      const empty = this.makeText('  No spells available', 13, 0x445566);
      empty.x = PAD; empty.y = curY + 12;
      this.scrollContent.addChild(empty);
      curY += ROW_H;
    } else {
      for (const item of actives) {
        this.buildActiveRow(item, curY, contentGfx);
        curY += ROW_H;
      }
    }

    curY += PAD;

    // Passives section
    this.sectionLabel('PASSIVES', curY, this.scrollContent);
    curY += sectionLabelH;

    for (const name of PLACEHOLDER_PASSIVES) {
      this.buildPassiveRow(name, curY, contentGfx);
      curY += ROW_H;
    }

    // Scroll indicator if content overflows
    if (this.contentH > this.visibleH) {
      this.drawScrollBar(panelH);
    }
  }

  close() {
    this._isOpen = false;
    this.dragging = false;
    this.container.visible = false;
    this.scrollContent.removeChildren();
    this.headerLayer.removeChildren();
    this.panelBg.clear();
    this.backdrop.clear();
    this.scrollMask.clear();
    const cb = this.onCloseCb;
    this.onCloseCb = null;
    cb?.();
  }

  // ── Private helpers ────────────────────────────────────────────────────────

  private setScroll(y: number) {
    const maxScroll = Math.max(0, this.contentH - this.visibleH);
    this.scrollY = Math.max(0, Math.min(maxScroll, y));
    this.scrollContent.y = this.panelY + HEADER_H - this.scrollY;
    this.updateScrollBar();
  }

  private scrollBarGfx: Graphics | null = null;
  private scrollBarTrackH = 0;

  private drawScrollBar(panelH: number) {
    const g = new Graphics();
    this.scrollBarGfx = g;
    this.scrollBarTrackH = this.visibleH - 8;
    g.x = this.panelX + PANEL_W - 7;
    g.y = this.panelY + HEADER_H + 4;
    g.eventMode = 'none';
    this.container.addChild(g);
    this.updateScrollBar();
  }

  private updateScrollBar() {
    const g = this.scrollBarGfx;
    if (!g) return;
    g.clear();
    const ratio    = this.visibleH / this.contentH;
    const thumbH   = Math.max(20, this.scrollBarTrackH * ratio);
    const maxScroll = Math.max(1, this.contentH - this.visibleH);
    const thumbY   = (this.scrollY / maxScroll) * (this.scrollBarTrackH - thumbH);
    // Track
    g.roundRect(0, 0, 3, this.scrollBarTrackH, 2).fill({ color: 0x223344, alpha: 0.5 });
    // Thumb
    g.roundRect(0, thumbY, 3, thumbH, 2).fill({ color: 0x5577aa, alpha: 0.85 });
  }

  private buildHeader() {
    this.headerLayer.removeChildren();

    const titleStyle = new TextStyle({
      fontFamily: '"Courier New", monospace',
      fontSize: 17, fill: 0xaaccff, fontWeight: 'bold',
    });
    const title = new Text({ text: 'ACTIONS', style: titleStyle });
    title.x = this.panelX + PAD;
    title.y = this.panelY + 14;
    title.eventMode = 'none';
    this.headerLayer.addChild(title);

    // Divider
    const div = new Graphics();
    div.rect(this.panelX + PAD, this.panelY + HEADER_H - 1, PANEL_W - PAD * 2, 1)
      .fill({ color: 0x334488, alpha: 0.6 });
    div.eventMode = 'none';
    this.headerLayer.addChild(div);

    // Close button
    const cg = new Graphics();
    cg.circle(0, 0, 14)
      .fill({ color: 0x1a2a44, alpha: 0.9 })
      .stroke({ color: 0x4466aa, alpha: 0.8, width: 1.5 });
    cg.x = this.panelX + PANEL_W - PAD - 14;
    cg.y = this.panelY + HEADER_H / 2;
    cg.eventMode = 'static';
    cg.cursor = 'pointer';
    cg.on('pointerdown', (e) => { (e as any).stopPropagation?.(); this.close(); });
    const cl = this.makeText('✕', 13, 0x6688bb, true);
    cl.anchor.set(0.5);
    cg.addChild(cl);
    this.headerLayer.addChild(cg);
  }

  private buildActiveRow(item: ActiveItem, y: number, gfx: Graphics) {
    const disabled = item.charges <= 0;
    gfx.roundRect(PAD, y, PANEL_W - PAD * 2, ROW_H - 4, 5)
      .fill({ color: disabled ? 0x1a1a2a : 0x0f1e30, alpha: 0.85 })
      .stroke({ color: item.color, alpha: disabled ? 0.15 : 0.45, width: 1 });

    // Icon
    const icon = this.makeText(this.elementIcon(item.element), 18, disabled ? 0x333344 : item.color, true);
    icon.anchor.set(0.5);
    icon.x = PAD + 18; icon.y = y + ROW_H / 2 - 2;
    this.scrollContent.addChild(icon);

    // Name
    const name = this.makeText(item.name, 14, disabled ? 0x334455 : 0xbbccee, true);
    name.x = PAD + 36; name.y = y + 8;
    this.scrollContent.addChild(name);

    // Tooltip line
    if (item.tooltip) {
      const tip = this.makeText(item.tooltip, 10, 0x445566);
      tip.x = PAD + 36; tip.y = y + 27;
      this.scrollContent.addChild(tip);
    }

    // Charges badge
    const badge = this.makeText(`${item.charges}/${item.chargesMax}`, 11, disabled ? 0x334455 : 0x7799bb);
    badge.anchor.set(1, 0.5);
    badge.x = PANEL_W - PAD - 6; badge.y = y + ROW_H / 2 - 2;
    this.scrollContent.addChild(badge);

    if (!disabled) {
      const hit = new Graphics();
      hit.roundRect(PAD, y, PANEL_W - PAD * 2, ROW_H - 4, 5)
        .fill({ color: 0xffffff, alpha: 0 });
      hit.eventMode = 'static';
      hit.cursor = 'pointer';
      hit.on('pointerover', () => {
        hit.clear();
        hit.roundRect(PAD, y, PANEL_W - PAD * 2, ROW_H - 4, 5)
          .fill({ color: item.color, alpha: 0.12 })
          .stroke({ color: item.color, alpha: 0.7, width: 1.5 });
      });
      hit.on('pointerout', () => {
        hit.clear();
        hit.roundRect(PAD, y, PANEL_W - PAD * 2, ROW_H - 4, 5)
          .fill({ color: 0xffffff, alpha: 0 });
      });
      hit.on('pointerdown', (e) => {
        (e as any).stopPropagation?.();
        if (Math.abs(this.scrollY - this.dragScrollStart) > 4) return; // was a scroll, not a tap
        this.close();
        item.action();
      });
      this.scrollContent.addChild(hit);
    }
  }

  private buildPassiveRow(name: string, y: number, gfx: Graphics) {
    gfx.roundRect(PAD, y, PANEL_W - PAD * 2, ROW_H - 4, 5)
      .fill({ color: 0x080f18, alpha: 0.7 })
      .stroke({ color: 0x1a2233, alpha: 0.5, width: 1 });

    const cb = this.makeText('[ ]', 14, 0x2a3a50);
    cb.x = PAD + 8; cb.y = y + 14;
    this.scrollContent.addChild(cb);

    const lbl = this.makeText(name, 13, 0x3a4a66);
    lbl.x = PAD + 44; lbl.y = y + 14;
    this.scrollContent.addChild(lbl);

    const tag = this.makeText('coming soon', 9, 0x223344);
    tag.anchor.set(1, 0.5);
    tag.x = PANEL_W - PAD - 4; tag.y = y + ROW_H / 2 - 2;
    this.scrollContent.addChild(tag);
  }

  private sectionLabel(text: string, y: number, parent: Container) {
    const lbl = this.makeText(text, 11, 0x5577aa, true);
    lbl.x = PAD; lbl.y = y + 5;
    parent.addChild(lbl);
  }

  private makeText(str: string, size: number, fill: number, bold = false): Text {
    const t = new Text({
      text: str,
      style: new TextStyle({
        fontFamily: '"Courier New", monospace',
        fontSize: size, fill,
        fontWeight: bold ? 'bold' : 'normal',
      }),
    });
    t.eventMode = 'none';
    return t;
  }

  private elementIcon(element: string): string {
    switch (element) {
      case 'fire':      return '🔥';
      case 'lightning': return '⚡';
      case 'ice':       return '❄';
      case 'poison':    return '☠';
      case 'arcane':    return '✦';
      default:          return '·';
    }
  }
}
