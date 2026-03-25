/**
 * TOOLTIP OVERLAY
 *
 * Tap any entity (player or enemy) to inspect it.
 * Shows name, HP bar, and active status effects.
 * Positioned near the tapped tile, clamped to screen bounds.
 */

import { Container, Graphics, Text, TextStyle } from 'pixi.js';
import { World, Health, StatusEffect, Label } from '../core/ECS';
import { lerpColor, hpColor } from '../utils/colors';
import { STATUS_COLORS } from '../constants/rendering';
import {
  TOOLTIP_W, TOOLTIP_PAD, TOOLTIP_BAR_W, TOOLTIP_BAR_H,
  TOOLTIP_STATUS_H, TOOLTIP_BASE_H, TOOLTIP_OFFSET_X, TOOLTIP_OFFSET_Y,
  UI_FONT_FAMILY,
} from '../constants/ui';

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
    this.container.visible   = false;
    this.container.eventMode = 'none';

    this.bg = new Graphics();
    this.container.addChild(this.bg);

    const nameStyle = new TextStyle({
      fontFamily: UI_FONT_FAMILY, fontSize: 16, fill: 0xddeeff, fontWeight: 'bold',
    });
    const hpStyle = new TextStyle({
      fontFamily: UI_FONT_FAMILY, fontSize: 13, fill: 0xaaccee,
    });
    const statusStyle = new TextStyle({
      fontFamily: UI_FONT_FAMILY, fontSize: 13, fill: 0xffdd88,
    });

    this.nameText   = new Text({ text: '', style: nameStyle });
    this.hpText     = new Text({ text: '', style: hpStyle });
    this.hpBarBg    = new Graphics();
    this.hpBarFill  = new Graphics();
    this.statusText = new Text({ text: '', style: statusStyle });

    this.nameText.x   = TOOLTIP_PAD;
    this.nameText.y   = TOOLTIP_PAD;
    this.hpBarBg.x    = TOOLTIP_PAD;
    this.hpBarBg.y    = TOOLTIP_PAD + 22;
    this.hpBarFill.x  = TOOLTIP_PAD;
    this.hpBarFill.y  = TOOLTIP_PAD + 22;
    this.hpText.x     = TOOLTIP_PAD;
    this.hpText.y     = TOOLTIP_PAD + 34;
    this.statusText.x = TOOLTIP_PAD;
    this.statusText.y = TOOLTIP_PAD + 50;

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
    const label  = world.getComponent<Label>(entityId, 'label');
    const health = world.getComponent<Health>(entityId, 'health');
    const status = world.getComponent<StatusEffect>(entityId, 'status');

    const name    = label?.name ?? '???';
    const hp      = health ? Math.max(0, Math.ceil(health.current)) : 0;
    const maxHp   = health?.max ?? 1;
    const effects = status ? [...status.effects.keys()] : [];
    const ratio   = Math.max(0, Math.min(1, hp / maxHp));

    this.nameText.text   = name;
    this.hpText.text     = `HP  ${hp} / ${maxHp}`;
    this.statusText.text = effects.length > 0
      ? effects.map(e => `● ${e}`).join('  ')
      : '';

    const barColor = hpColor(ratio);
    this.hpBarBg.clear().rect(0, 0, TOOLTIP_BAR_W, TOOLTIP_BAR_H).fill({ color: 0x112233, alpha: 0.9 });
    this.hpBarFill.clear().rect(0, 0, Math.round(TOOLTIP_BAR_W * ratio), TOOLTIP_BAR_H).fill({ color: barColor, alpha: 1 });

    // Status effect dot colour (dominant effect)
    if (effects.length > 0) {
      const domColor = STATUS_COLORS[effects[0]] ?? 0xffdd88;
      (this.statusText.style as TextStyle).fill = domColor;
    }

    const H = effects.length > 0 ? TOOLTIP_BASE_H + TOOLTIP_STATUS_H : TOOLTIP_BASE_H;
    this.bg.clear()
      .roundRect(0, 0, TOOLTIP_W, H, 6)
      .fill({ color: 0x0d1825, alpha: 0.93 })
      .stroke({ color: 0x3366aa, width: 1.5, alpha: 0.85 });

    // Position near tap, clamped to screen
    let x = tapX + TOOLTIP_OFFSET_X;
    let y = tapY - H - TOOLTIP_OFFSET_Y;
    if (x + TOOLTIP_W > screenW - 8) x = tapX - TOOLTIP_W - TOOLTIP_OFFSET_X;
    if (x < 8) x = 8;
    if (y < 8) y = tapY + TOOLTIP_OFFSET_X;
    if (y + H > screenH - 8) y = screenH - H - 8;

    this.container.x = x;
    this.container.y = y;
    this.container.visible = true;
  }

  hide() {
    this.container.visible = false;
  }
}

// Re-export for any file that still needs it without importing utils/colors
export { lerpColor };
