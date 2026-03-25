/**
 * UI CONSTANTS
 *
 * Pixel dimensions, padding, and font sizes for all UI widgets.
 * Adjust here if you need to refit the UI to a different screen/DPI target.
 */

// ── Action Menu (ActionMenu.ts) ───────────────────────────────────────────────

export const MENU_PANEL_W   = 290;  // panel width in px
export const MENU_ROW_H     = 48;   // height of one spell / passive row
export const MENU_PAD       = 14;   // inner horizontal padding
export const MENU_HEADER_H  = 48;   // fixed header height (title + close btn)
/** Maximum panel height relative to screen: panel = screenH - this. */
export const MENU_SCREEN_MARGIN = 80;

// ── Action FAB (the ⚡ / ☰ button in Game.ts) ──────────────────────────────

export const FAB_RADIUS   = 34;   // circle radius px
export const FAB_OFFSET_X = 60;   // distance from right edge of screen
export const FAB_OFFSET_Y = 70;   // distance from bottom edge of screen

// ── D-Pad (DPad.ts) ──────────────────────────────────────────────────────────

export const DPAD_BTN_SIZE = 38;   // button square size px
export const DPAD_BTN_GAP  = 5;    // gap between buttons px
/** Offset from left edge of screen. */
export const DPAD_OFFSET_X = 20;
/** Offset from bottom edge of screen (container.y = screenH - this). */
export const DPAD_BOTTOM_OFFSET = 138;

// ── Tooltip Overlay (TooltipOverlay.ts) ──────────────────────────────────────

export const TOOLTIP_W       = 210;
export const TOOLTIP_PAD     = 10;
export const TOOLTIP_BAR_W   = 190;
export const TOOLTIP_BAR_H   = 10;
/** Extra height added when status effects are present. */
export const TOOLTIP_STATUS_H = 20;
/** Panel height with no active status effects. */
export const TOOLTIP_BASE_H   = 64;
/** Horizontal offset from tap point before clamping to screen bounds. */
export const TOOLTIP_OFFSET_X = 20;
/** Vertical offset above tap point before clamping. */
export const TOOLTIP_OFFSET_Y = 16;

// ── HUD text (Game.ts / HUDRenderer.ts) ──────────────────────────────────────

export const HUD_FONT_SIZE  = 18;
export const MSG_FONT_SIZE  = 15;
export const MODE_FONT_SIZE = 16;
export const HUD_X          = 10;
export const HUD_Y          = 10;
export const MSG_Y          = 36;
/** Distance from bottom edge of screen for the mode hint text. */
export const MODE_BOTTOM_OFFSET = 100;
/** Max number of messages kept in the rolling log. */
export const MESSAGE_LOG_MAX = 6;
/** Number of messages visible at once in the HUD. */
export const MESSAGE_LOG_VISIBLE = 3;

// ── Font family shared by all UI text ────────────────────────────────────────

export const UI_FONT_FAMILY = '"Courier New", monospace';
