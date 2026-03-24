/**
 * DPAD — Virtual 8-directional movement pad + center wait button
 *
 * 3×3 grid layout:
 *   [↖] [↑] [↗]
 *   [←] [·] [→]   ← center = Wait
 *   [↙] [↓] [↘]
 *
 * Fires onDirection(dx, dy) for movement, onWait() for center button.
 */

import { Container, Graphics, Text, TextStyle } from 'pixi.js';

const BTN  = 38;  // button size px
const GAP  = 5;   // gap between buttons
const STEP = BTN + GAP;

// [gridCol, gridRow, dx, dy, label]
const DIR_BUTTONS: [number, number, number, number, string][] = [
  [0, 0, -1, -1, '↖'],
  [1, 0,  0, -1, '↑'],
  [2, 0,  1, -1, '↗'],
  [0, 1, -1,  0, '←'],
  [2, 1,  1,  0, '→'],
  [0, 2, -1,  1, '↙'],
  [1, 2,  0,  1, '↓'],
  [2, 2,  1,  1, '↘'],
];

export class DPad {
  public container: Container;

  constructor(
    private onDirection: (dx: number, dy: number) => void,
    private onWait: () => void,
  ) {
    // 'passive' lets pointer events reach children without the container itself capturing them
    this.container = new Container();
    this.container.eventMode = 'passive';

    this.buildBtn(1, 1, '…', 0x223344, 0x4488aa, () => this.onWait());

    for (const [gc, gr, dx, dy, arrow] of DIR_BUTTONS) {
      this.buildBtn(gc, gr, arrow, 0x0d1a2e, 0x334466, () => this.onDirection(dx, dy));
    }
  }

  private buildBtn(
    gc: number, gr: number,
    label: string,
    fillColor: number, strokeColor: number,
    callback: () => void,
  ) {
    const gfx = new Graphics();
    gfx.roundRect(0, 0, BTN, BTN, 6)
      .fill({ color: fillColor, alpha: 0.88 })
      .stroke({ color: strokeColor, alpha: 0.85, width: 1.5 });

    gfx.x = gc * STEP;
    gfx.y = gr * STEP;
    gfx.eventMode = 'static';
    gfx.cursor = 'pointer';

    gfx.on('pointerdown', (e) => {
      e.stopPropagation();
      gfx.clear();
      gfx.roundRect(0, 0, BTN, BTN, 6)
        .fill({ color: 0x1a3060, alpha: 0.95 })
        .stroke({ color: 0x6699cc, alpha: 1, width: 2 });
      callback();
    });

    const reset = () => {
      gfx.clear();
      gfx.roundRect(0, 0, BTN, BTN, 6)
        .fill({ color: fillColor, alpha: 0.88 })
        .stroke({ color: strokeColor, alpha: 0.85, width: 1.5 });
    };
    gfx.on('pointerup', reset);
    gfx.on('pointerupoutside', reset);

    const lblStyle = new TextStyle({
      fontFamily: '"Courier New", monospace',
      fontSize: label.length === 1 && label > '\u00ff' ? 14 : 16,
      fill: fillColor === 0x223344 ? 0x66aacc : 0x99bbdd,
      fontWeight: 'bold',
    });
    const lbl = new Text({ text: label, style: lblStyle });
    lbl.anchor.set(0.5);
    lbl.x = BTN / 2;
    lbl.y = BTN / 2;
    lbl.eventMode = 'none';
    gfx.addChild(lbl);

    this.container.addChild(gfx);
  }

  resize(x: number, y: number) {
    this.container.x = x;
    this.container.y = y;
  }
}
