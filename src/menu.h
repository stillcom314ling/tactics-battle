#pragma once
#include "proto.h"

typedef struct {
    const Prototype * const *prototypes; /* array of prototype pointers */
    int                      count;
    int                      active;      /* highlighted item (-1 = none) */
    int                      scroll_idx;  /* raygui list scroll offset    */
    int                      launched;    /* index to launch (-1 = none)  */
} Menu;

void MenuInit(Menu *m, const Prototype * const *protos, int count);

/*
 * Process input. Returns the index of the prototype to launch,
 * or -1 if no launch was requested this frame.
 */
int  MenuUpdate(Menu *m);

/* Render the menu. Call between BeginDrawing / EndDrawing. */
void MenuDraw(const Menu *m);
