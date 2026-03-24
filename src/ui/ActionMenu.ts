/**
 * ACTION MENU — Tap-to-open popup for selecting spells or passives
 *
 * Two sections:
 *   Actives  — spell buttons; tapping one calls action() and closes menu
 *   Passives — placeholder checklist (non-interactive for now)
 *
 * Public API:
 *   menu.open(actives, onClose?)
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

const PANEL_W = 290;
const ROW_H   = 44;
const PAD     = 14;

const PLACEHOLDER_PASSIVES = [
  'Speed Boost',
  'Tough Skin',
  'Mana Regen',
  'Arcane Ward',
];

export class ActionMenu {
  public container: Container;
  private backdrop: Graphics;
  private panel: Container;
  private onCloseCb: (() => void) | null = null;
  private _isOpen = false;

  constructor() {
    this.container = new Container();
    this.container.visible = false;
    this.container.eventMode = 'none';

    // Full-screen semi-transparent backdrop — catches taps outside panel
    this.backdrop = new Graphics();
    this.backdrop.eventMode = 'static';
    this.backdrop.cursor = 'default';
    this.backdrop.on('pointerdown', () => this.close());
    this.container.addChild(this.backdrop);

    this.panel = new Container();
    this.panel.eventMode = 'static';
    this.container.addChild(this.panel);
  }

  get isOpen() { return this._isOpen; }

  open(actives: ActiveItem[], screenW: number, screenH: number, onClose?: () => void) {
    this._isOpen = true;
    this.onCloseCb = onClose ?? null;
    this.container.visible = true;

    // Rebuild panel contents
    this.panel.removeChildren();
    const gfx = new Graphics();
    this.panel.addChild(gfx);

    // Full-screen backdrop
    this.backdrop.clear();
    this.backdrop.rect(0, 0, screenW, screenH).fill({ color: 0x000000, alpha: 0.55 });

    // Calculate panel height
    const headerH    = 48;
    const sectionH   = 24;
    const activesH   = Math.max(1, actives.length) * ROW_H;
    const passivesH  = PLACEHOLDER_PASSIVES.length * ROW_H;
    const panelH     = headerH + PAD + sectionH + activesH + PAD + sectionH + passivesH + PAD;

    // Position panel center-screen, slightly toward bottom
    const px = (screenW - PANEL_W) / 2;
    const py = Math.max(20, (screenH - panelH) / 2);

    this.panel.x = px;
    this.panel.y = py;

    // Panel background
    gfx.roundRect(0, 0, PANEL_W, panelH, 10)
      .fill({ color: 0x0a1520, alpha: 0.97 })
      .stroke({ color: 0x3355aa, alpha: 0.75, width: 2 });

    // ── Header ──────────────────────────────────────────────────────────
    const titleStyle = new TextStyle({
      fontFamily: '"Courier New", monospace',
      fontSize: 17,
      fill: 0xaaccff,
      fontWeight: 'bold',
    });
    const title = new Text({ text: 'ACTIONS', style: titleStyle });
    title.x = PAD;
    title.y = 14;
    title.eventMode = 'none';
    this.panel.addChild(title);

    // Close button
    const closeGfx = new Graphics();
    closeGfx.circle(0, 0, 14)
      .fill({ color: 0x1a2a44, alpha: 0.9 })
      .stroke({ color: 0x4466aa, alpha: 0.8, width: 1.5 });
    closeGfx.x = PANEL_W - PAD - 14;
    closeGfx.y = headerH / 2;
    closeGfx.eventMode = 'static';
    closeGfx.cursor = 'pointer';
    closeGfx.on('pointerdown', (e) => { e.stopPropagation(); this.close(); });

    const closeLbl = new Text({ text: '✕', style: new TextStyle({ fontFamily: '"Courier New", monospace', fontSize: 13, fill: 0x6688bb, fontWeight: 'bold' }) });
    closeLbl.anchor.set(0.5);
    closeLbl.eventMode = 'none';
    closeGfx.addChild(closeLbl);
    this.panel.addChild(closeGfx);

    // Divider
    gfx.rect(PAD, headerH - 1, PANEL_W - PAD * 2, 1).fill({ color: 0x334488, alpha: 0.6 });

    let curY = headerH + PAD;

    // ── Actives section ─────────────────────────────────────────────────
    this.sectionLabel('ACTIVES', curY, this.panel);
    curY += sectionH;

    if (actives.length === 0) {
      const empty = new Text({ text: '  No spells available', style: new TextStyle({ fontFamily: '"Courier New", monospace', fontSize: 13, fill: 0x445566 }) });
      empty.x = PAD;
      empty.y = curY + 10;
      empty.eventMode = 'none';
      this.panel.addChild(empty);
      curY += ROW_H;
    } else {
      for (const item of actives) {
        this.buildActiveRow(item, curY, gfx);
        curY += ROW_H;
      }
    }

    curY += PAD;

    // ── Passives section ────────────────────────────────────────────────
    this.sectionLabel('PASSIVES', curY, this.panel);
    curY += sectionH;

    for (const name of PLACEHOLDER_PASSIVES) {
      this.buildPassiveRow(name, curY, gfx);
      curY += ROW_H;
    }
  }

  close() {
    this._isOpen = false;
    this.container.visible = false;
    this.panel.removeChildren();
    this.backdrop.clear();
    this.onCloseCb?.();
    this.onCloseCb = null;
  }

  // ── Private helpers ────────────────────────────────────────────────────────

  private sectionLabel(text: string, y: number, parent: Container) {
    const style = new TextStyle({
      fontFamily: '"Courier New", monospace',
      fontSize: 11,
      fill: 0x5577aa,
      fontWeight: 'bold',
    });
    const lbl = new Text({ text, style });
    lbl.x = PAD;
    lbl.y = y + 5;
    lbl.eventMode = 'none';
    parent.addChild(lbl);
  }

  private buildActiveRow(item: ActiveItem, y: number, gfx: Graphics) {
    const disabled = item.charges <= 0;
    const rowColor = disabled ? 0x1a1a2a : 0x0f1e30;
    const borderColor = disabled ? 0x222233 : item.color;

    // Row background
    gfx.roundRect(PAD, y, PANEL_W - PAD * 2, ROW_H - 4, 5)
      .fill({ color: rowColor, alpha: 0.85 })
      .stroke({ color: borderColor, alpha: disabled ? 0.2 : 0.5, width: 1 });

    // Icon
    const iconStyle = new TextStyle({
      fontFamily: '"Courier New", monospace',
      fontSize: 18,
      fill: disabled ? 0x333344 : item.color,
      fontWeight: 'bold',
    });
    const icon = new Text({ text: this.elementIcon(item.element), style: iconStyle });
    icon.anchor.set(0.5);
    icon.x = PAD + 18;
    icon.y = y + ROW_H / 2 - 2;
    icon.eventMode = 'none';
    this.panel.addChild(icon);

    // Name
    const nameStyle = new TextStyle({
      fontFamily: '"Courier New", monospace',
      fontSize: 14,
      fill: disabled ? 0x334455 : 0xbbccee,
      fontWeight: 'bold',
    });
    const name = new Text({ text: item.name, style: nameStyle });
    name.x = PAD + 36;
    name.y = y + 8;
    name.eventMode = 'none';
    this.panel.addChild(name);

    // Tooltip line
    if (item.tooltip) {
      const tipStyle = new TextStyle({ fontFamily: '"Courier New", monospace', fontSize: 10, fill: 0x445566 });
      const tip = new Text({ text: item.tooltip, style: tipStyle });
      tip.x = PAD + 36;
      tip.y = y + 26;
      tip.eventMode = 'none';
      this.panel.addChild(tip);
    }

    // Charges badge
    const badgeStyle = new TextStyle({
      fontFamily: '"Courier New", monospace',
      fontSize: 11,
      fill: disabled ? 0x334455 : 0x7799bb,
    });
    const badge = new Text({ text: `${item.charges}/${item.chargesMax}`, style: badgeStyle });
    badge.anchor.set(1, 0.5);
    badge.x = PANEL_W - PAD - 4;
    badge.y = y + ROW_H / 2 - 2;
    badge.eventMode = 'none';
    this.panel.addChild(badge);

    // Hit area for interaction
    if (!disabled) {
      const hitArea = new Graphics();
      hitArea.rect(PAD, y, PANEL_W - PAD * 2, ROW_H - 4).fill({ color: 0xffffff, alpha: 0 });
      hitArea.eventMode = 'static';
      hitArea.cursor = 'pointer';
      hitArea.on('pointerover', () => {
        gfx.clear();
        // Can't easily redraw just this row so we do a highlight tint instead
        hitArea.clear();
        hitArea.roundRect(PAD, y, PANEL_W - PAD * 2, ROW_H - 4, 5)
          .fill({ color: item.color, alpha: 0.12 })
          .stroke({ color: item.color, alpha: 0.7, width: 1.5 });
        hitArea.eventMode = 'static';
      });
      hitArea.on('pointerout', () => {
        hitArea.clear();
        hitArea.rect(PAD, y, PANEL_W - PAD * 2, ROW_H - 4).fill({ color: 0xffffff, alpha: 0 });
      });
      hitArea.on('pointerdown', (e) => {
        e.stopPropagation();
        this.close();
        item.action();
      });
      this.panel.addChild(hitArea);
    }
  }

  private buildPassiveRow(name: string, y: number, _gfx: Graphics) {
    // Row background (subtle, non-interactive)
    const bg = new Graphics();
    bg.roundRect(PAD, y, PANEL_W - PAD * 2, ROW_H - 4, 5)
      .fill({ color: 0x080f18, alpha: 0.7 })
      .stroke({ color: 0x1a2233, alpha: 0.5, width: 1 });
    bg.eventMode = 'none';
    this.panel.addChild(bg);

    // Checkbox (placeholder, always unchecked)
    const cbStyle = new TextStyle({ fontFamily: '"Courier New", monospace', fontSize: 14, fill: 0x2a3a50 });
    const cb = new Text({ text: '[ ]', style: cbStyle });
    cb.x = PAD + 8;
    cb.y = y + 13;
    cb.eventMode = 'none';
    this.panel.addChild(cb);

    // Name
    const nameStyle = new TextStyle({ fontFamily: '"Courier New", monospace', fontSize: 13, fill: 0x3a4a66 });
    const lbl = new Text({ text: name, style: nameStyle });
    lbl.x = PAD + 42;
    lbl.y = y + 13;
    lbl.eventMode = 'none';
    this.panel.addChild(lbl);

    // "coming soon" tag
    const tagStyle = new TextStyle({ fontFamily: '"Courier New", monospace', fontSize: 9, fill: 0x223344 });
    const tag = new Text({ text: 'coming soon', style: tagStyle });
    tag.anchor.set(1, 0.5);
    tag.x = PANEL_W - PAD - 4;
    tag.y = y + ROW_H / 2 - 2;
    tag.eventMode = 'none';
    this.panel.addChild(tag);
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
