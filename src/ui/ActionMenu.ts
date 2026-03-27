/**
 * ACTION MENU — Tap-to-toggle popup for selecting spells or passives
 *
 * Two sections:
 *   ACTIVES  — spell buttons; tapping one calls action() and closes menu
 *   PASSIVES — placeholder checklist (non-interactive for now)
 *
 * Scrollable: drag up/down on the panel to scroll content.
 *
 * Public API:
 *   menu.open(actives, screenW, screenH, onClose?)
 *   menu.close()
 *   menu.isOpen
 */

import { Container, Graphics, Text, TextStyle } from 'pixi.js';
import {
  MENU_PANEL_W, MENU_ROW_H, MENU_PAD, MENU_HEADER_H, MENU_SCREEN_MARGIN, UI_FONT_FAMILY,
} from '../constants/ui';

export interface ActiveItem {
  id:         string;
  name:       string;
  element:    string;
  charges:    number;
  chargesMax: number;
  color:      number;
  tooltip?:   string;
  action:     () => void;
}

export interface PassiveItem {
  id:           string;
  name:         string;
  description:  string;
  triggerLabel: string;  // e.g. "On kill", "On cast", "Always"
}

const ELEMENT_ICONS: Record<string, string> = {
  fire:      '🔥',
  lightning: '⚡',
  ice:       '❄',
  poison:    '☠',
  arcane:    '✦',
};

export class ActionMenu {
  public container: Container;
  private backdrop:      Graphics;
  private panelBg:       Graphics;
  private scrollContent: Container;
  private scrollMask:    Graphics;
  private headerLayer:   Container;
  private scrollBarGfx:  Graphics | null = null;

  private _isOpen          = false;
  private scrollY          = 0;
  private contentH         = 0;
  private visibleH         = 0;
  private scrollBarTrackH  = 0;

  private dragStartClientY = 0;
  private dragScrollStart  = 0;
  private dragging         = false;

  private panelX     = 0;
  private panelY     = 0;
  private onCloseCb: (() => void) | null = null;

  constructor() {
    this.container = new Container();
    this.container.visible   = false;
    this.container.eventMode = 'passive';

    this.backdrop = new Graphics();
    this.backdrop.eventMode = 'static';
    this.backdrop.on('pointerdown', () => this.close());
    this.container.addChild(this.backdrop);

    this.panelBg = new Graphics();
    this.panelBg.eventMode = 'none';
    this.container.addChild(this.panelBg);

    this.scrollContent = new Container();
    this.scrollContent.eventMode = 'passive';
    this.container.addChild(this.scrollContent);

    this.scrollMask = new Graphics();
    this.scrollMask.eventMode = 'none';
    this.container.addChild(this.scrollMask);
    this.scrollContent.mask = this.scrollMask;

    this.headerLayer = new Container();
    this.headerLayer.eventMode = 'passive';
    this.container.addChild(this.headerLayer);

    window.addEventListener('pointermove', (e) => {
      if (!this.dragging) return;
      const dy = e.clientY - this.dragStartClientY;
      this.setScroll(this.dragScrollStart - dy);
    });
    window.addEventListener('pointerup', () => { this.dragging = false; });

    document.addEventListener('wheel', (e) => {
      if (!this._isOpen) return;
      this.setScroll(this.scrollY + e.deltaY * 0.5);
    }, { passive: true });
  }

  get isOpen() { return this._isOpen; }

  open(actives: ActiveItem[], passives: PassiveItem[], screenW: number, screenH: number, onClose?: () => void) {
    this._isOpen   = true;
    this.scrollY   = 0;
    this.onCloseCb = onClose ?? null;
    this.container.visible = true;

    this.backdrop.clear();
    this.backdrop.rect(0, 0, screenW, screenH).fill({ color: 0x000000, alpha: 0.55 });

    const maxPanelH      = screenH - MENU_SCREEN_MARGIN;
    const sectionLabelH  = 26;
    const totalContentH  =
      sectionLabelH + Math.max(1, actives.length) * MENU_ROW_H + MENU_PAD +
      sectionLabelH + Math.max(1, passives.length) * MENU_ROW_H + MENU_PAD;

    this.contentH = totalContentH;
    this.visibleH = Math.min(totalContentH, maxPanelH - MENU_HEADER_H);
    const panelH  = MENU_HEADER_H + this.visibleH;

    this.panelX = (screenW - MENU_PANEL_W) / 2;
    this.panelY = Math.max(20, (screenH - panelH) / 2);

    this.panelBg.clear();
    this.panelBg.roundRect(this.panelX, this.panelY, MENU_PANEL_W, panelH, 10)
      .fill({ color: 0x0a1520, alpha: 0.97 })
      .stroke({ color: 0x3355aa, alpha: 0.75, width: 2 });

    this.scrollMask.clear();
    this.scrollMask.rect(
      this.panelX, this.panelY + MENU_HEADER_H, MENU_PANEL_W, this.visibleH,
    ).fill({ color: 0xffffff, alpha: 1 });

    this.buildHeader();

    this.scrollContent.removeChildren();
    this.scrollContent.x = this.panelX;
    this.scrollContent.y = this.panelY + MENU_HEADER_H;
    this.scrollContent.eventMode = 'static';

    // Transparent blocker stops scroll-area events reaching the backdrop
    const blocker = new Graphics();
    blocker.rect(0, 0, MENU_PANEL_W, this.contentH).fill({ color: 0xffffff, alpha: 0 });
    blocker.eventMode = 'static';
    blocker.on('pointerdown', (e) => {
      this.dragging          = true;
      this.dragStartClientY  = (e as any).clientY ?? e.global.y;
      this.dragScrollStart   = this.scrollY;
      e.stopPropagation();
    });
    this.scrollContent.addChild(blocker);

    const contentGfx = new Graphics();
    this.scrollContent.addChild(contentGfx);

    let curY = 0;

    this.sectionLabel('ACTIVES', curY, this.scrollContent);
    curY += sectionLabelH;

    if (actives.length === 0) {
      const empty = this.makeText('  No spells available', 13, 0x445566);
      empty.x = MENU_PAD; empty.y = curY + 12;
      this.scrollContent.addChild(empty);
      curY += MENU_ROW_H;
    } else {
      for (const item of actives) {
        this.buildActiveRow(item, curY, contentGfx);
        curY += MENU_ROW_H;
      }
    }

    curY += MENU_PAD;

    this.sectionLabel('PASSIVES', curY, this.scrollContent);
    curY += sectionLabelH;
    if (passives.length === 0) {
      const empty = this.makeText('  No passives', 13, 0x445566);
      empty.x = MENU_PAD; empty.y = curY + 12;
      this.scrollContent.addChild(empty);
      curY += MENU_ROW_H;
    } else {
      for (const item of passives) {
        this.buildPassiveRow(item, curY, contentGfx);
        curY += MENU_ROW_H;
      }
    }

    if (this.contentH > this.visibleH) this.drawScrollBar(panelH);
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
    if (this.scrollBarGfx) {
      this.container.removeChild(this.scrollBarGfx);
      this.scrollBarGfx = null;
    }
    const cb = this.onCloseCb;
    this.onCloseCb = null;
    cb?.();
  }

  // ── Private helpers ────────────────────────────────────────────────────────

  private setScroll(y: number) {
    const maxScroll = Math.max(0, this.contentH - this.visibleH);
    this.scrollY = Math.max(0, Math.min(maxScroll, y));
    this.scrollContent.y = this.panelY + MENU_HEADER_H - this.scrollY;
    this.updateScrollBar();
  }

  private drawScrollBar(_panelH: number) {
    const g = new Graphics();
    this.scrollBarGfx    = g;
    this.scrollBarTrackH = this.visibleH - 8;
    g.x = this.panelX + MENU_PANEL_W - 7;
    g.y = this.panelY + MENU_HEADER_H + 4;
    g.eventMode = 'none';
    this.container.addChild(g);
    this.updateScrollBar();
  }

  private updateScrollBar() {
    const g = this.scrollBarGfx;
    if (!g) return;
    g.clear();
    const ratio      = this.visibleH / this.contentH;
    const thumbH     = Math.max(20, this.scrollBarTrackH * ratio);
    const maxScroll  = Math.max(1, this.contentH - this.visibleH);
    const thumbY     = (this.scrollY / maxScroll) * (this.scrollBarTrackH - thumbH);
    g.roundRect(0, 0, 3, this.scrollBarTrackH, 2).fill({ color: 0x223344, alpha: 0.5 });
    g.roundRect(0, thumbY, 3, thumbH, 2).fill({ color: 0x5577aa, alpha: 0.85 });
  }

  private buildHeader() {
    this.headerLayer.removeChildren();

    const title = this.makeText('ACTIONS', 17, 0xaaccff, true);
    title.x = this.panelX + MENU_PAD;
    title.y = this.panelY + 14;
    title.eventMode = 'none';
    this.headerLayer.addChild(title);

    const div = new Graphics();
    div.rect(this.panelX + MENU_PAD, this.panelY + MENU_HEADER_H - 1, MENU_PANEL_W - MENU_PAD * 2, 1)
      .fill({ color: 0x334488, alpha: 0.6 });
    div.eventMode = 'none';
    this.headerLayer.addChild(div);

    const cg = new Graphics();
    cg.circle(0, 0, 14)
      .fill({ color: 0x1a2a44, alpha: 0.9 })
      .stroke({ color: 0x4466aa, alpha: 0.8, width: 1.5 });
    cg.x = this.panelX + MENU_PANEL_W - MENU_PAD - 14;
    cg.y = this.panelY + MENU_HEADER_H / 2;
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
    gfx.roundRect(MENU_PAD, y, MENU_PANEL_W - MENU_PAD * 2, MENU_ROW_H - 4, 5)
      .fill({ color: disabled ? 0x1a1a2a : 0x0f1e30, alpha: 0.85 })
      .stroke({ color: item.color, alpha: disabled ? 0.15 : 0.45, width: 1 });

    const icon = this.makeText(ELEMENT_ICONS[item.element] ?? '·', 18, disabled ? 0x333344 : item.color, true);
    icon.anchor.set(0.5);
    icon.x = MENU_PAD + 18; icon.y = y + MENU_ROW_H / 2 - 2;
    this.scrollContent.addChild(icon);

    const name = this.makeText(item.name, 14, disabled ? 0x334455 : 0xbbccee, true);
    name.x = MENU_PAD + 36; name.y = y + 8;
    this.scrollContent.addChild(name);

    if (item.tooltip) {
      const tip = this.makeText(item.tooltip, 10, 0x445566);
      tip.x = MENU_PAD + 36; tip.y = y + 27;
      this.scrollContent.addChild(tip);
    }

    const badge = this.makeText(`${item.charges}/${item.chargesMax}`, 11, disabled ? 0x334455 : 0x7799bb);
    badge.anchor.set(1, 0.5);
    badge.x = MENU_PANEL_W - MENU_PAD - 6; badge.y = y + MENU_ROW_H / 2 - 2;
    this.scrollContent.addChild(badge);

    if (!disabled) {
      const hit = new Graphics();
      hit.roundRect(MENU_PAD, y, MENU_PANEL_W - MENU_PAD * 2, MENU_ROW_H - 4, 5)
        .fill({ color: 0xffffff, alpha: 0 });
      hit.eventMode = 'static';
      hit.cursor = 'pointer';
      hit.on('pointerover', () => {
        hit.clear();
        hit.roundRect(MENU_PAD, y, MENU_PANEL_W - MENU_PAD * 2, MENU_ROW_H - 4, 5)
          .fill({ color: item.color, alpha: 0.12 })
          .stroke({ color: item.color, alpha: 0.7, width: 1.5 });
      });
      hit.on('pointerout', () => {
        hit.clear();
        hit.roundRect(MENU_PAD, y, MENU_PANEL_W - MENU_PAD * 2, MENU_ROW_H - 4, 5)
          .fill({ color: 0xffffff, alpha: 0 });
      });
      hit.on('pointerdown', (e) => {
        (e as any).stopPropagation?.();
        if (Math.abs(this.scrollY - this.dragScrollStart) > 4) return;
        this.close();
        item.action();
      });
      this.scrollContent.addChild(hit);
    }
  }

  private buildPassiveRow(item: PassiveItem, y: number, gfx: Graphics) {
    gfx.roundRect(MENU_PAD, y, MENU_PANEL_W - MENU_PAD * 2, MENU_ROW_H - 4, 5)
      .fill({ color: 0x0a1520, alpha: 0.8 })
      .stroke({ color: 0x1e3355, alpha: 0.6, width: 1 });

    const icon = this.makeText('◆', 12, 0x3366aa);
    icon.x = MENU_PAD + 8; icon.y = y + 8;
    this.scrollContent.addChild(icon);

    const lbl = this.makeText(item.name, 13, 0x99bbdd, true);
    lbl.x = MENU_PAD + 26; lbl.y = y + 7;
    this.scrollContent.addChild(lbl);

    const desc = this.makeText(item.description, 10, 0x4a6a88);
    desc.x = MENU_PAD + 26; desc.y = y + 26;
    this.scrollContent.addChild(desc);

    const trig = this.makeText(item.triggerLabel, 9, 0x2a4a66);
    trig.anchor.set(1, 0.5);
    trig.x = MENU_PANEL_W - MENU_PAD - 4; trig.y = y + MENU_ROW_H / 2 - 2;
    this.scrollContent.addChild(trig);
  }

  private sectionLabel(text: string, y: number, parent: Container) {
    const lbl = this.makeText(text, 11, 0x5577aa, true);
    lbl.x = MENU_PAD; lbl.y = y + 5;
    parent.addChild(lbl);
  }

  private makeText(str: string, size: number, fill: number, bold = false): Text {
    const t = new Text({
      text: str,
      style: new TextStyle({ fontFamily: UI_FONT_FAMILY, fontSize: size, fill, fontWeight: bold ? 'bold' : 'normal' }),
    });
    t.eventMode = 'none';
    return t;
  }
}
