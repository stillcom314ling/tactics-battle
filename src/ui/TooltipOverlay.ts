/**
 * TOOLTIP OVERLAY
 *
 * Tap any entity (player or enemy) to inspect it.
 * Shows name, HP bar, and active status effects.
 * Positioned near the tapped tile, clamped to screen bounds.
 */

import { Container, Graphics, Text, TextStyle } from 'pixi.js';
import { World, Health, StatusEffect, Label } from '../core/ECS';

const W          = 210;
const PAD        = 10;
const BAR_W      = 190;
const BAR_H      = 10;
const STATUS_H   = 20;   // extra height when effects are present
const BASE_H     = 64;   // height with no status line

const STATUS_COLORS: Record<string, number> = {
  burning:  0xff5500,
  shocked:  0xffff00,
  slowed:   0x88ccff,
  poisoned: 0x66ff44,
  wet:      0x44aaff,
  stunned:  0xffaaff,
};

export class TooltipOverlay {
  readonly container: Container;

  private bg:         Graphics;
  private nameText:   Text;
  private hpText:     Text;
  private hpBarBg:    Graphics;
  private hpBarFill:  Graphics;
  private statusText: Text;

  constructor() {
    this.container = new Container();
    this.container.visible = false;
    this.container.eventMode = 'none'; // don't intercept input

    this.bg = new Graphics();
    this.container.addChild(this.bg);

    const nameStyle = new TextStyle({
      fontFamily: '"Courier New", monospace',
      fontSize: 16,
      fill: 0xddeeff,
      fontWeight: 'bold',
    });
    const hpStyle = new TextStyle({
      fontFamily: '"Courier New", monospace',
      fontSize: 13,
      fill: 0xaaccee,
    });
    const statusStyle = new TextStyle({
      fontFamily: '"Courier New", monospace',
      fontSize: 13,
      fill: 0xffdd88,
    });

    this.nameText   = new Text({ text: '', style: nameStyle });
    this.hpText     = new Text({ text: '', style: hpStyle });
    this.hpBarBg    = new Graphics();
    this.hpBarFill  = new Graphics();
    this.statusText = new Text({ text: '', style: statusStyle });

    this.nameText.x  = PAD;
    this.nameText.y  = PAD;
    this.hpBarBg.x   = PAD;
    this.hpBarBg.y   = PAD + 22;
    this.hpBarFill.x = PAD;
    this.hpBarFill.y = PAD + 22;
    this.hpText.x    = PAD;
    this.hpText.y    = PAD + 34;
    this.statusText.x = PAD;
    this.statusText.y = PAD + 50;

    this.container.addChild(this.hpBarBg);
    this.container.addChild(this.hpBarFill);
    this.container.addChild(this.nameText);
    this.container.addChild(this.hpText);
    this.container.addChild(this.statusText);
  }

  show(
    world: World,
    entityId: number,
    tapX: number,
    tapY: number,
    screenW: number,
    screenH: number,
  ) {
    const label   = world.getComponent<Label>(entityId, 'label');
    const health  = world.getComponent<Health>(entityId, 'health');
    const status  = world.getComponent<StatusEffect>(entityId, 'status');

    const name    = label?.name ?? '???';
    const hp      = health ? Math.max(0, Math.ceil(health.current)) : 0;
    const maxHp   = health?.max ?? 1;
    const effects = status ? [...status.effects] : [];
    const ratio   = Math.max(0, Math.min(1, hp / maxHp));

    this.nameText.text   = name;
    this.hpText.text     = `HP  ${hp} / ${maxHp}`;
    this.statusText.text = effects.length > 0
      ? effects.map(e => `● ${e}`).join('  ')
      : '';

    // HP bar colour: green → yellow → red based on ratio
    const barColor = ratio > 0.5
      ? lerpColor(0xffcc00, 0x44dd44, (ratio - 0.5) * 2)
      : lerpColor(0xff3333, 0xffcc00, ratio * 2);

    this.hpBarBg.clear().rect(0, 0, BAR_W, BAR_H).fill({ color: 0x112233, alpha: 0.9 });
    this.hpBarFill.clear().rect(0, 0, Math.round(BAR_W * ratio), BAR_H).fill({ color: barColor, alpha: 1 });

    // Status effect dots colour override
    if (effects.length > 0) {
      const domColor = STATUS_COLORS[effects[0]] ?? 0xffdd88;
      (this.statusText.style as TextStyle).fill = domColor;
    }

    // Panel height
    const H = effects.length > 0 ? BASE_H + STATUS_H : BASE_H;

    this.bg.clear()
      .roundRect(0, 0, W, H, 6)
      .fill({ color: 0x0d1825, alpha: 0.93 })
      .stroke({ color: 0x3366aa, width: 1.5, alpha: 0.85 });

    // Position near tap, keep inside screen
    let x = tapX + 20;
    let y = tapY - H - 16;
    if (x + W > screenW - 8) x = tapX - W - 20;
    if (x < 8) x = 8;
    if (y < 8) y = tapY + 20;
    if (y + H > screenH - 8) y = screenH - H - 8;

    this.container.x = x;
    this.container.y = y;
    this.container.visible = true;
  }

  hide() {
    this.container.visible = false;
  }
}

function lerpColor(a: number, b: number, t: number): number {
  const ar = (a >> 16) & 0xff, ag = (a >> 8) & 0xff, ab = a & 0xff;
  const br = (b >> 16) & 0xff, bg = (b >> 8) & 0xff, bb = b & 0xff;
  const r = Math.round(ar + (br - ar) * t);
  const g = Math.round(ag + (bg - ag) * t);
  const bl = Math.round(ab + (bb - ab) * t);
  return (r << 16) | (g << 8) | bl;
}
