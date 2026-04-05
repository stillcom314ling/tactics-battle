#include "menu.h"
#include "raylib.h"
#include "raygui.h"
#include <string.h>

#define LIST_BUF     2048
#define MAX_VISIBLE  6   /* max list items shown at once */

/* Build the semicolon-separated name list (kept for potential fallback). */
static void build_list(const Menu *m, char *buf, int sz)
{
    buf[0] = '\0';
    for (int i = 0; i < m->count; i++) {
        if (i > 0) strncat(buf, ";", (size_t)(sz - (int)strlen(buf) - 1));
        strncat(buf, m->prototypes[i]->name, (size_t)(sz - (int)strlen(buf) - 1));
    }
}

void MenuInit(Menu *m, const Prototype * const *protos, int count)
{
    m->prototypes = protos;
    m->count      = count;
    m->active     = (count > 0) ? 0 : -1;
    m->scroll_idx = 0;
    m->launched   = -1;
    m->prev_touch = 0;
    DragDispInit(&m->drag, 0.0f, 0.0f); /* radius / strength scaled in Draw */
}

int MenuUpdate(Menu *m)
{
    DragDispUpdate(&m->drag);
    int result  = m->launched;
    m->launched = -1;
    return result;
}

/* Draw one displaced list item; returns true if it was clicked. */
static bool draw_list_item(Rectangle r, const char *name, bool selected,
                           int pad, int text_sz)
{
    Color bg  = selected ? (Color){ 50,  85, 140, 255 }
                         : (Color){ 28,  28,  44, 220 };
    Color bdr = selected ? (Color){110, 150, 230, 255 }
                         : (Color){ 55,  55,  85, 255 };
    DrawRectangleRounded(r, 0.18f, 6, bg);
    DrawRectangleRoundedLines(r, 0.18f, 6, bdr);

    DrawText(name,
             (int)r.x + pad / 2,
             (int)(r.y + (r.height - text_sz) * 0.5f),
             text_sz, selected ? WHITE : LIGHTGRAY);

    return IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
           CheckCollisionPointRec(GetMousePosition(), r);
}

void MenuDraw(Menu *m)
{
    ClearBackground((Color){ 20, 20, 32, 255 });

    int sw  = GetScreenWidth();
    int sh  = GetScreenHeight();
    int pad = sw / 20;

    /* Scale drag influence with screen dimensions. */
    m->drag.radius   = sw * 0.30f;
    m->drag.strength = sw * 0.07f;

    int text_sz = sw / 24;
    GuiSetStyle(DEFAULT, TEXT_SIZE, text_sz);

    /* ---- drag cursor indicator ----------------------------------------- */
    /* Outer ring shows influence radius (very faint). */
    DrawCircleLinesV(m->drag.pos, m->drag.radius, (Color){ 80, 110, 220, 22 });
    /* Inner dot shows exact cursor position. */
    DrawCircleV(m->drag.pos, 7.0f, (Color){ 160, 190, 255, 90 });

    /* ---- title ---------------------------------------------------------- */
    GuiSetStyle(LABEL, TEXT_ALIGNMENT, TEXT_ALIGN_CENTER);
    GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, ColorToInt(WHITE));
    Rectangle title_r = { (float)pad, (float)pad,
                          (float)(sw - 2 * pad), (float)(sh / 10) };
    Vector2 tc = { title_r.x + title_r.width * 0.5f,
                   title_r.y + title_r.height * 0.5f };
    Vector2 to = DragDispOffset(&m->drag, tc);
    title_r.x += to.x;
    title_r.y += to.y;
    GuiLabel(title_r, "PROTOTYPE LAUNCHER");

    float y = (float)pad + (float)(sh / 10) + (float)pad;

    /* ---- list (custom draw — each item displaced independently) --------- */
    float list_h = sh * 0.40f;
    float item_h = list_h / (float)MAX_VISIBLE;

    int vis_end = m->scroll_idx + MAX_VISIBLE;
    if (vis_end > m->count) vis_end = m->count;

    for (int i = m->scroll_idx; i < vis_end; i++) {
        float iy = y + (float)(i - m->scroll_idx) * item_h;
        Rectangle ir = {
            (float)pad + 2.0f, iy + 2.0f,
            (float)(sw - 2 * pad) - 4.0f, item_h - 4.0f
        };
        Vector2 ic  = { ir.x + ir.width * 0.5f, ir.y + ir.height * 0.5f };
        Vector2 iof = DragDispOffset(&m->drag, ic);
        ir.x += iof.x;
        ir.y += iof.y;

        if (draw_list_item(ir, m->prototypes[i]->name,
                           i == m->active, pad, text_sz))
            m->active = i;
    }

    /* Simple scroll arrows when list doesn't fit. */
    if (m->count > MAX_VISIBLE) {
        int arr_sz = (int)item_h / 2;
        /* Up */
        if (m->scroll_idx > 0) {
            Rectangle up = { (float)(sw / 2 - arr_sz / 2), y - arr_sz - 2,
                             (float)arr_sz, (float)arr_sz };
            if (GuiButton(up, "^")) m->scroll_idx--;
        }
        /* Down */
        if (vis_end < m->count) {
            Rectangle dn = { (float)(sw / 2 - arr_sz / 2),
                             y + list_h + 2,
                             (float)arr_sz, (float)arr_sz };
            if (GuiButton(dn, "v")) m->scroll_idx++;
        }
    }

    y += list_h + pad;

    /* ---- description ---------------------------------------------------- */
    GuiSetStyle(DEFAULT, TEXT_SIZE, text_sz * 3 / 4);
    GuiSetStyle(LABEL, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);
    float desc_h = sh * 0.18f;
    Rectangle desc_r = { (float)pad, y, (float)(sw - 2 * pad), desc_h };
    Vector2 dc  = { desc_r.x + desc_r.width * 0.5f,
                    desc_r.y + desc_r.height * 0.5f };
    Vector2 dof = DragDispOffset(&m->drag, dc);
    desc_r.x += dof.x * 0.5f; /* half-strength for subtler feel */
    desc_r.y += dof.y * 0.5f;
    DrawRectangleRec(desc_r, (Color){ 30, 30, 48, 255 });
    DrawRectangleLinesEx(desc_r, 1, (Color){ 70, 70, 100, 255 });
    if (m->active >= 0 && m->active < m->count) {
        GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, ColorToInt(LIGHTGRAY));
        Rectangle inner = { desc_r.x + pad / 2, desc_r.y + pad / 2,
                            desc_r.width - pad,  desc_r.height - pad };
        GuiLabel(inner, m->prototypes[m->active]->description);
    }

    y += desc_h + pad;

    /* ---- launch button -------------------------------------------------- */
    GuiSetStyle(DEFAULT, TEXT_SIZE, text_sz);
    float btn_w = sw * 0.6f;
    float btn_h = sh / 10.0f;
    Rectangle btn_r = { (sw - btn_w) / 2.0f,
                        (float)(sh - (int)btn_h - pad),
                        btn_w, btn_h };
    Vector2 bc  = { btn_r.x + btn_r.width * 0.5f,
                    btn_r.y + btn_r.height * 0.5f };
    Vector2 bof = DragDispOffset(&m->drag, bc);
    btn_r.x += bof.x;
    btn_r.y += bof.y;
    bool ok = (m->active >= 0 && m->active < m->count);
    if (!ok) GuiSetState(STATE_DISABLED);
    if (GuiButton(btn_r, "Launch >") && ok) m->launched = m->active;
    if (!ok) GuiSetState(STATE_NORMAL);
}
