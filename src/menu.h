#pragma once
#include "proto.h"
#include "drag_disp.h"

typedef struct {
    const Prototype * const *prototypes;
    int                      count;
    int                      active;      /* selected item index (-1 = none) */
    int                      scroll_idx;  /* GuiListView scroll offset       */
    int                      launched;    /* prototype to launch (-1 = none) */
    int                      prev_touch;  /* reserved                        */
    DragDisp                 drag;        /* cursor / touch displacement      */
} Menu;

void MenuInit(Menu *m, const Prototype * const *protos, int count);

/* Handle input. Returns prototype index to launch, or -1. */
int  MenuUpdate(Menu *m);

/* Render the menu. Call between BeginDrawing / EndDrawing. */
void MenuDraw(Menu *m);
