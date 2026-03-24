/**
 * DPAD — Virtual 8-directional movement pad
 *
 * 3×3 grid layout (center is empty):
 *   [↖] [↑] [↗]
 *   [←]  ·  [→]
 *   [↙] [↓] [↘]
 *
 * Fires onDirection(dx, dy) on press. Supports hold-repeat.
 */

import { Container, Graphics, Text, TextStyle } from 'pixi.js';

const BTN  = 38;  // button size px
const GAP  = 5;   // gap between buttons
const STEP = BTN + GAP;

// [col, row] → [dx, dy, arrow label]
const BUTTONS: [number, number, number, number, string][] = [
  [0, 0, -1, -1, '↖'],
  [1, 0,  0, -1, '↑'],
  [2, 0,  1, -1, '↗'],
  [0, 1, -1,  0, '←'],
  // [1,1] center — skipped
  [2, 1,  1,  0, '→'],
  [0, 2, -1,  1, '↙'],
  [1, 2,  0,  1, '↓'],
  [2, 2,  1,  1, '↘'],
];

export class DPad {
  public container: Container;
  private btns: { gfx: Graphics; col: number; row: number }[] = [];

  constructor(private onDirection: (dx: number, dy: number) => void) {
    this.container = new Container();
    this.container.eventMode = 'none';

    const labelStyle = new TextStyle({
      fontFamily: '"Courier New", monospace',
      fontSize: 16,
      fill: 0x99bbdd,
      fontWeight: 'bold',
    });

    for (const [gc, gr, dx, dy, arrow] of BUTTONS) {
      const gfx = new Graphics();
      gfx.roundRect(0, 0, BTN, BTN, 6)
        .fill({ color: 0x0d1a2e, alpha: 0.88 })
        .stroke({ color: 0x334466, alpha: 0.85, width: 1.5 });

      gfx.x = gc * STEP;
      gfx.y = gr * STEP;
      gfx.eventMode = 'static';
      gfx.cursor = 'pointer';

      // Press effect
      gfx.on('pointerdown', (e) => {
        e.stopPropagation();
        gfx.clear();
        gfx.roundRect(0, 0, BTN, BTN, 6)
          .fill({ color: 0x1a3060, alpha: 0.95 })
          .stroke({ color: 0x6699cc, alpha: 1, width: 2 });
        this.onDirection(dx, dy);
      });

      gfx.on('pointerup', () => this.resetBtn(gfx));
      gfx.on('pointerupoutside', () => this.resetBtn(gfx));

      const lbl = new Text({ text: arrow, style: labelStyle });
      lbl.anchor.set(0.5);
      lbl.x = BTN / 2;
      lbl.y = BTN / 2;
      lbl.eventMode = 'none';
      gfx.addChild(lbl);

      this.container.addChild(gfx);
      this.btns.push({ gfx, col: gc, row: gr });
    }
  }

  private resetBtn(gfx: Graphics) {
    gfx.clear();
    gfx.roundRect(0, 0, BTN, BTN, 6)
      .fill({ color: 0x0d1a2e, alpha: 0.88 })
      .stroke({ color: 0x334466, alpha: 0.85, width: 1.5 });
  }

  resize(x: number, y: number) {
    this.container.x = x;
    this.container.y = y;
  }
}
