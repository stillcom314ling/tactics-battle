#include "menu.h"
#include "raylib.h"
#include "raygui.h"
#include <string.h>

/* Max characters for the semicolon-separated list string */
#define LIST_BUF_SIZE 1024

/* Build the semicolon-separated item list expected by GuiListView */
static void build_list_text(const Menu *m, char *buf, int buf_size)
{
    buf[0] = '\0';
    for (int i = 0; i < m->count; i++) {
        if (i > 0) strncat(buf, ";", (size_t)(buf_size - (int)strlen(buf) - 1));
        strncat(buf, m->prototypes[i]->name, (size_t)(buf_size - (int)strlen(buf) - 1));
    }
}

void MenuInit(Menu *m, const Prototype * const *protos, int count)
{
    m->prototypes = protos;
    m->count      = count;
    m->active     = (count > 0) ? 0 : -1;
    m->scroll_idx = 0;
    m->launched   = -1;
}

int MenuUpdate(Menu *m)
{
    int result  = m->launched;
    m->launched = -1;
    return result;
}

void MenuDraw(const Menu *m)
{
    ClearBackground((Color){ 18, 18, 24, 255 });

    int sw  = GetScreenWidth();
    int sh  = GetScreenHeight();
    int pad = sw / 24;

    /* Scale UI text relative to screen width */
    int text_size = sw / 22;
    GuiSetStyle(DEFAULT, TEXT_SIZE, text_size);

    /* ---- Title ---- */
    int title_h = sh / 10;
    Rectangle title_rect = { (float)pad, (float)pad, (float)(sw - 2*pad), (float)title_h };
    GuiSetStyle(LABEL, TEXT_ALIGNMENT, TEXT_ALIGN_CENTER);
    GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, ColorToInt(WHITE));
    GuiLabel(title_rect, "PROTOTYPE LAUNCHER");

    float y = (float)(pad + title_h + pad);

    /* ---- Prototype list ---- */
    float list_h = sh * 0.38f;
    Rectangle list_rect = { (float)pad, y, (float)(sw - 2*pad), list_h };

    char list_text[LIST_BUF_SIZE];
    build_list_text(m, list_text, LIST_BUF_SIZE);

    Menu *mut = (Menu *)m; /* GuiListView needs non-const in/out params */
    GuiListView(list_rect, list_text, &mut->scroll_idx, &mut->active);

    y += list_h + (float)pad;

    /* ---- Description panel ---- */
    float desc_h = sh * 0.22f;
    Rectangle desc_rect = { (float)pad, y, (float)(sw - 2*pad), desc_h };

    GuiSetStyle(DEFAULT, TEXT_SIZE, text_size * 3 / 4);
    GuiSetStyle(LABEL, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);

    DrawRectangleRec(desc_rect, (Color){ 30, 30, 42, 255 });
    DrawRectangleLinesEx(desc_rect, 1, (Color){ 80, 80, 100, 255 });

    if (m->active >= 0 && m->active < m->count) {
        Rectangle inner = {
            desc_rect.x + (float)(pad/2),
            desc_rect.y + (float)(pad/2),
            desc_rect.width  - (float)pad,
            desc_rect.height - (float)pad
        };
        GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, ColorToInt(LIGHTGRAY));
        GuiLabel(inner, m->prototypes[m->active]->description);
    } else {
        GuiSetStyle(LABEL, TEXT_ALIGNMENT, TEXT_ALIGN_CENTER);
        GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, ColorToInt(GRAY));
        GuiLabel(desc_rect, "Select a prototype");
    }

    y += desc_h + (float)pad;

    /* ---- Launch button ---- */
    GuiSetStyle(DEFAULT, TEXT_SIZE, text_size);

    float btn_w = sw * 0.55f;
    float btn_h = sh / 10.0f;
    Rectangle btn_rect = {
        (sw - btn_w) / 2.0f,
        (float)(sh - (int)btn_h - pad),
        btn_w,
        btn_h
    };

    bool can_launch = (m->active >= 0 && m->active < m->count);
    if (!can_launch) GuiSetState(STATE_DISABLED);

    if (GuiButton(btn_rect, "Launch >") && can_launch)
        mut->launched = m->active;

    if (!can_launch) GuiSetState(STATE_NORMAL);
}
