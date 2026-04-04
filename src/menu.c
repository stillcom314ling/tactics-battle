#include "menu.h"
#include "raylib.h"
#include <stddef.h>

/* ----------------------------------------------------------------- layout
 * All geometry is computed from screen size so it works on any display. */

static int layout_pad(int sw)     { return sw / 20; }
static int layout_title_fs(int sw){ return sw / 14; }
static int layout_body_fs(int sw) { return sw / 20; }
static int layout_small_fs(int sw){ return sw / 28; }
static int layout_item_h(int sh)  { return sh / 9;  }
static int layout_item_gap(int sh){ return sh / 72; }

static int layout_list_top(int sw, int sh)
{
    (void)sh;
    return layout_pad(sw) + layout_title_fs(sw) + layout_pad(sw);
}

static Rectangle item_rect(int i, int sw, int sh)
{
    int pad    = layout_pad(sw);
    int item_h = layout_item_h(sh);
    int gap    = layout_item_gap(sh);
    int top    = layout_list_top(sw, sh);
    return (Rectangle){
        (float)pad,
        (float)(top + i * (item_h + gap)),
        (float)(sw - 2 * pad),
        (float)item_h
    };
}

static Rectangle launch_rect(int sw, int sh)
{
    int   pad   = layout_pad(sw);
    float btn_w = sw * 0.6f;
    float btn_h = (float)layout_item_h(sh);
    return (Rectangle){
        (sw - btn_w) / 2.0f,
        (float)(sh - (int)btn_h - pad),
        btn_w,
        btn_h
    };
}

/* ---------------------------------------------------------------- menu api */

void MenuInit(Menu *m, const Prototype * const *protos, int count)
{
    m->prototypes   = protos;
    m->count        = count;
    m->active       = -1;
    m->scroll_idx   = 0;
    m->launched     = -1;
    m->prev_touch   = 0;
}

int MenuUpdate(Menu *m)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    /* --- detect a single tap / click (fires once per gesture) --- */
    bool   pressed   = false;
    Vector2 press_pos = {-1.0f, -1.0f};

    int tc = GetTouchPointCount();
    if (tc > 0 && m->prev_touch == 0) {
        /* touch just started this frame */
        pressed   = true;
        press_pos = GetTouchPosition(0);
    } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        pressed   = true;
        press_pos = GetMousePosition();
    }
    m->prev_touch = tc;

    /* track mouse hover for desktop highlight */
    m->active = -1;
    Vector2 mouse = GetMousePosition();
    for (int i = 0; i < m->count; i++) {
        Rectangle r = item_rect(i, sw, sh);
        if (CheckCollisionPointRec(mouse, r)) m->active = i;
    }

    if (pressed) {
        /* prototype items: tap to launch */
        for (int i = 0; i < m->count; i++) {
            if (CheckCollisionPointRec(press_pos, item_rect(i, sw, sh))) {
                m->launched = i;
                break;
            }
        }
        /* launch button also works */
        if (m->active >= 0 &&
            CheckCollisionPointRec(press_pos, launch_rect(sw, sh)))
            m->launched = m->active;
    }

    int result  = m->launched;
    m->launched = -1;
    return result;
}

void MenuDraw(const Menu *m)
{
    ClearBackground((Color){ 15, 15, 25, 255 });

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int pad      = layout_pad(sw);
    int title_fs = layout_title_fs(sw);
    int body_fs  = layout_body_fs(sw);
    int small_fs = layout_small_fs(sw);
    int item_h   = layout_item_h(sh);

    /* ---- title ---- */
    const char *title = "PROTOTYPE LAUNCHER";
    int title_w = MeasureText(title, title_fs);
    DrawText(title, (sw - title_w) / 2, pad, title_fs, WHITE);

    /* ---- prototype items ---- */
    Vector2 mouse = GetMousePosition();

    for (int i = 0; i < m->count; i++) {
        Rectangle row = item_rect(i, sw, sh);
        bool hovered  = CheckCollisionPointRec(mouse, row);

        Color bg     = hovered ? (Color){ 50, 110, 230, 255 }
                               : (Color){ 28,  28,  45, 255 };
        Color border = hovered ? (Color){ 90, 160, 255, 255 }
                               : (Color){ 55,  55,  80, 255 };

        DrawRectangleRec(row, bg);
        DrawRectangleLinesEx(row, 2, border);

        int text_y = (int)row.y + (item_h - body_fs) / 2;
        DrawText(m->prototypes[i]->name,
                 (int)row.x + pad / 2, text_y, body_fs, WHITE);

        if (item_h > body_fs + small_fs + 8) {
            DrawText(m->prototypes[i]->description,
                     (int)row.x + pad / 2,
                     text_y + body_fs + 4,
                     small_fs,
                     (Color){ 180, 180, 200, 180 });
        }
    }

    /* ---- launch button (desktop helper, tap item directly on mobile) ---- */
    Rectangle btn      = launch_rect(sw, sh);
    bool      has_sel  = (m->active >= 0);
    Color btn_bg  = has_sel ? (Color){ 50, 180,  90, 255 }
                            : (Color){ 35,  35,  50, 255 };
    Color btn_bdr = has_sel ? (Color){ 80, 220, 120, 255 }
                            : (Color){ 55,  55,  70, 255 };
    Color btn_fg  = has_sel ? WHITE : (Color){ 80, 80, 80, 255 };

    DrawRectangleRec(btn, btn_bg);
    DrawRectangleLinesEx(btn, 2, btn_bdr);

    const char *label   = has_sel ? "TAP ITEM OR LAUNCH >" : "hover / tap to select";
    int         label_fs = small_fs;
    int         label_w  = MeasureText(label, label_fs);
    DrawText(label,
             (int)btn.x + ((int)btn.width  - label_w)  / 2,
             (int)btn.y + ((int)btn.height - label_fs) / 2,
             label_fs, btn_fg);
}
