#include "menu.h"
#include "raylib.h"
#include "raygui.h"
#include <string.h>

#define LIST_BUF 2048

/* Build the semicolon-separated name list GuiListView expects. */
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
    DragDispInit(&m->drag, 220.0f, 40.0f);
}

int MenuUpdate(Menu *m)
{
    DragDispUpdate(&m->drag);
    int result  = m->launched;
    m->launched = -1;
    return result;
}

void MenuDraw(Menu *m)
{
    ClearBackground((Color){ 20, 20, 32, 255 });

    int sw  = GetScreenWidth();
    int sh  = GetScreenHeight();
    int pad = sw / 20;

    int text_sz = sw / 22;
    GuiSetStyle(DEFAULT, TEXT_SIZE, text_sz);

    /* Scale drag influence with screen size so it feels consistent. */
    m->drag.radius   = sw * 0.22f;
    m->drag.strength = sw * 0.04f;

    /* ---- title ---- */
    GuiSetStyle(LABEL, TEXT_ALIGNMENT, TEXT_ALIGN_CENTER);
    GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, ColorToInt(WHITE));
    Rectangle title_r = { (float)pad, (float)pad,
                          (float)(sw - 2*pad), (float)(sh/10) };
    Vector2 title_center = { title_r.x + title_r.width * 0.5f,
                             title_r.y + title_r.height * 0.5f };
    Vector2 title_off = DragDispOffset(&m->drag, title_center);
    title_r.x += title_off.x;
    title_r.y += title_off.y;
    GuiLabel(title_r, "PROTOTYPE LAUNCHER");

    float y = (float)pad + (float)(sh / 10) + (float)pad;

    /* ---- list ---- */
    char list_buf[LIST_BUF];
    build_list(m, list_buf, LIST_BUF);

    float list_h = sh * 0.40f;
    Rectangle list_r = { (float)pad, y, (float)(sw - 2*pad), list_h };
    GuiListView(list_r, list_buf, &m->scroll_idx, &m->active);

    y += list_h + pad;

    /* ---- description ---- */
    GuiSetStyle(DEFAULT, TEXT_SIZE, text_sz * 3 / 4);
    GuiSetStyle(LABEL, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);
    float desc_h = sh * 0.18f;
    Rectangle desc_r = { (float)pad, y, (float)(sw - 2*pad), desc_h };
    Vector2 desc_center = { desc_r.x + desc_r.width * 0.5f,
                            desc_r.y + desc_r.height * 0.5f };
    Vector2 desc_off = DragDispOffset(&m->drag, desc_center);
    /* Description box displaces at half strength for a subtler effect. */
    desc_r.x += desc_off.x * 0.5f;
    desc_r.y += desc_off.y * 0.5f;
    DrawRectangleRec(desc_r, (Color){ 30, 30, 48, 255 });
    DrawRectangleLinesEx(desc_r, 1, (Color){ 70, 70, 100, 255 });
    if (m->active >= 0 && m->active < m->count) {
        GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, ColorToInt(LIGHTGRAY));
        Rectangle inner = { desc_r.x + pad/2, desc_r.y + pad/2,
                            desc_r.width - pad, desc_r.height - pad };
        GuiLabel(inner, m->prototypes[m->active]->description);
    }

    y += desc_h + pad;

    /* ---- launch button ---- */
    GuiSetStyle(DEFAULT, TEXT_SIZE, text_sz);
    float btn_w = sw * 0.6f;
    float btn_h = sh / 10.0f;
    Rectangle btn_r = { (sw - btn_w) / 2.0f,
                        (float)(sh - (int)btn_h - pad),
                        btn_w, btn_h };
    Vector2 btn_center = { btn_r.x + btn_r.width * 0.5f,
                           btn_r.y + btn_r.height * 0.5f };
    Vector2 btn_off = DragDispOffset(&m->drag, btn_center);
    btn_r.x += btn_off.x;
    btn_r.y += btn_off.y;
    bool ok = (m->active >= 0 && m->active < m->count);
    if (!ok) GuiSetState(STATE_DISABLED);
    if (GuiButton(btn_r, "Launch >") && ok) m->launched = m->active;
    if (!ok) GuiSetState(STATE_NORMAL);
}
