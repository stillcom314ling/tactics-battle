/**
 * INPUT HANDLER
 *
 * Centralises all player input: mouse click, touch tap, and keyboard.
 * Extracted from Game.ts (was: private setupInput + keyboard spread throughout).
 *
 * Keyboard support (implemented here — was missing from original codebase):
 *   Arrow keys / WASD / numpad → directional movement
 *   .  or 5                    → wait a turn
 *
 * All game reactions are communicated back through InputCallbacks so this
 * module has zero dependency on PixiJS, ECS, or camera internals.
 *
 * Destroy event listeners when no longer needed by calling destroy().
 */

import { Application } from 'pixi.js';
import { CameraController } from '../rendering/CameraController';

// ─── CALLBACKS ───────────────────────────────────────────────────────────────

export interface InputCallbacks {
  /** A screen-space tap (click or touch) at (x, y) in canvas pixels. */
  onTap: (screenX: number, screenY: number) => void;
  /** Directional input (dx, dy) in the range {-1, 0, 1}. */
  onDirection: (dx: number, dy: number) => void;
  /** Player chose to wait this turn. */
  onWait: () => void;
}

// ─── KEYBOARD MAP ────────────────────────────────────────────────────────────

type DirTuple = [number, number]; // [dx, dy]

/** Maps KeyboardEvent.key to a direction or 'wait'. */
const KEY_MAP: Record<string, DirTuple | 'wait'> = {
  // Cardinal + diagonal — numpad
  Numpad1: [-1,  1], Numpad2: [ 0,  1], Numpad3: [ 1,  1],
  Numpad4: [-1,  0], Numpad5: 'wait',   Numpad6: [ 1,  0],
  Numpad7: [-1, -1], Numpad8: [ 0, -1], Numpad9: [ 1, -1],

  // Arrow keys (cardinal only)
  ArrowLeft:  [-1,  0],
  ArrowRight: [ 1,  0],
  ArrowUp:    [ 0, -1],
  ArrowDown:  [ 0,  1],

  // WASD (cardinal only)
  w: [ 0, -1], W: [ 0, -1],
  a: [-1,  0], A: [-1,  0],
  s: [ 0,  1], S: [ 0,  1],
  d: [ 1,  0], D: [ 1,  0],

  // Wait
  '.': 'wait',
  '5': 'wait',
};

// ─── CLASS ────────────────────────────────────────────────────────────────────

export class InputHandler {
  private readonly cleanups: (() => void)[] = [];

  constructor(
    private readonly app:      Application,
    private readonly camera:   CameraController,
    private readonly isMenuOpen: () => boolean,
    private readonly callbacks: InputCallbacks,
  ) {
    this.bindCanvasInput();
    this.bindKeyboard();
  }

  // ── Binding ────────────────────────────────────────────────────────────────

  private bindCanvasInput(): void {
    const canvas = this.app.canvas as HTMLCanvasElement;

    const toCanvas = (clientX: number, clientY: number): [number, number] => {
      const rect  = canvas.getBoundingClientRect();
      const scale = this.app.screen.width / rect.width;
      return [(clientX - rect.left) * scale, (clientY - rect.top) * scale];
    };

    // ── Mouse click ────────────────────────────────────────────────────────
    const onClick = (e: MouseEvent) => {
      if (this.camera.hasDragged) return;
      if (this.isMenuOpen()) return;
      const [x, y] = toCanvas(e.clientX, e.clientY);
      this.callbacks.onTap(x, y);
    };
    canvas.addEventListener('click', onClick);
    this.cleanups.push(() => canvas.removeEventListener('click', onClick));

    // ── Touch tap ──────────────────────────────────────────────────────────
    let touchStartX = 0, touchStartY = 0;
    const onTouchStart = (e: TouchEvent) => {
      const t = e.touches[0];
      touchStartX = t.clientX;
      touchStartY = t.clientY;
    };
    const onTouchEnd = (e: TouchEvent) => {
      if (this.camera.hasDragged) return;
      if (this.isMenuOpen()) return;
      const t = e.changedTouches[0];
      if (Math.hypot(t.clientX - touchStartX, t.clientY - touchStartY) > 8) return;
      const [x, y] = toCanvas(t.clientX, t.clientY);
      this.callbacks.onTap(x, y);
    };
    canvas.addEventListener('touchstart', onTouchStart, { passive: true });
    canvas.addEventListener('touchend',   onTouchEnd,   { passive: true });
    this.cleanups.push(() => {
      canvas.removeEventListener('touchstart', onTouchStart);
      canvas.removeEventListener('touchend',   onTouchEnd);
    });
  }

  private bindKeyboard(): void {
    const onKeyDown = (e: KeyboardEvent) => {
      // Ignore keyboard while any text input is focused
      const tag = (document.activeElement as HTMLElement)?.tagName;
      if (tag === 'INPUT' || tag === 'TEXTAREA') return;

      const action = KEY_MAP[e.key] ?? KEY_MAP[e.code];
      if (!action) return;

      e.preventDefault(); // stop arrow keys from scrolling the page

      if (action === 'wait') {
        this.callbacks.onWait();
      } else {
        const [dx, dy] = action;
        this.callbacks.onDirection(dx, dy);
      }
    };

    window.addEventListener('keydown', onKeyDown);
    this.cleanups.push(() => window.removeEventListener('keydown', onKeyDown));
  }

  // ── Cleanup ────────────────────────────────────────────────────────────────

  /** Remove all event listeners. Call before destroying the game. */
  destroy(): void {
    for (const fn of this.cleanups) fn();
    this.cleanups.length = 0;
  }
}
