#pragma once
#include "proto.h"

typedef struct {
    const Prototype * const *prototypes;
    int                      count;
    int                      active;      /* hovered item index (-1 = none) */
    int                      scroll_idx;  /* reserved for future scrolling  */
    int                      launched;    /* index to launch (-1 = none)    */
    int                      prev_touch;  /* touch count last frame          */
} Menu;

void MenuInit(Menu *m, const Prototype * const *protos, int count);

/*
 * Handle input. Returns the prototype index to launch, or -1.
 * Call once per frame BEFORE MenuDraw.
 */
int MenuUpdate(Menu *m);

/* Render the menu. Call between BeginDrawing / EndDrawing. */
void MenuDraw(const Menu *m);
