/**
 * CAMERA CONTROLLER
 * 
 * Handles panning via mouse drag and touch drag.
 * Supports inertia (momentum after release) for a smooth feel.
 * Outputs cameraX/cameraY that the ParallaxAsciiRenderer uses.
 */

export interface CameraState {
  x: number;
  y: number;
  velocityX: number;
  velocityY: number;
}

const DRAG_THRESHOLD = 6; // pixels before we consider it a drag vs a tap/click

export class CameraController {
  public state: CameraState = { x: 0, y: 0, velocityX: 0, velocityY: 0 };
  /** True if the last pointer-down moved enough to count as a drag (not a tap). */
  public hasDragged = false;

  private isDragging = false;
  private startPointerX = 0;
  private startPointerY = 0;
  private lastPointerX = 0;
  private lastPointerY = 0;
  private friction = 0.92;

  constructor(private element: HTMLElement) {
    this.bindEvents();
  }

  private bindEvents() {
    // Mouse
    this.element.addEventListener('mousedown', (e) => this.onPointerDown(e.clientX, e.clientY));
    window.addEventListener('mousemove', (e) => this.onPointerMove(e.clientX, e.clientY));
    window.addEventListener('mouseup', () => this.onPointerUp());

    // Touch
    this.element.addEventListener('touchstart', (e) => {
      e.preventDefault();
      const t = e.touches[0];
      this.onPointerDown(t.clientX, t.clientY);
    }, { passive: false });

    window.addEventListener('touchmove', (e) => {
      const t = e.touches[0];
      this.onPointerMove(t.clientX, t.clientY);
    }, { passive: false });

    window.addEventListener('touchend', () => this.onPointerUp());
  }

  private onPointerDown(x: number, y: number) {
    this.isDragging = true;
    this.hasDragged = false;
    this.startPointerX = x;
    this.startPointerY = y;
    this.lastPointerX = x;
    this.lastPointerY = y;
    this.state.velocityX = 0;
    this.state.velocityY = 0;
  }

  private onPointerMove(x: number, y: number) {
    if (!this.isDragging) return;

    const dx = x - this.lastPointerX;
    const dy = y - this.lastPointerY;

    // Only pan once we've moved past the drag threshold
    const totalDx = x - this.startPointerX;
    const totalDy = y - this.startPointerY;
    if (!this.hasDragged && Math.hypot(totalDx, totalDy) < DRAG_THRESHOLD) return;

    this.hasDragged = true;
    // Move camera opposite to drag direction (drag right = camera moves left = world moves right)
    this.state.x -= dx;
    this.state.y -= dy;
    this.state.velocityX = -dx;
    this.state.velocityY = -dy;

    this.lastPointerX = x;
    this.lastPointerY = y;
  }

  private onPointerUp() {
    this.isDragging = false;
  }

  /**
   * Call once per frame. Applies inertia when not dragging.
   */
  update() {
    if (!this.isDragging) {
      this.state.x += this.state.velocityX;
      this.state.y += this.state.velocityY;
      this.state.velocityX *= this.friction;
      this.state.velocityY *= this.friction;

      // Stop tiny movements
      if (Math.abs(this.state.velocityX) < 0.1) this.state.velocityX = 0;
      if (Math.abs(this.state.velocityY) < 0.1) this.state.velocityY = 0;
    }
  }
}
