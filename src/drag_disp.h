#pragma once
#include "raylib.h"

/*
 * DragDisp — drag-and-displacement module.
 *
 * Tracks the active cursor or touch position and provides a repulsion
 * displacement vector for any UI element center.  Elements near the
 * input point are pushed away; the effect falls off quadratically with
 * distance and is amplified by the speed of the drag gesture.
 */
typedef struct {
    Vector2 pos;      /* current cursor / first-touch position  */
    Vector2 vel;      /* velocity of the input point  (px / s)  */
    float   radius;   /* influence radius in pixels              */
    float   strength; /* maximum displacement in pixels          */
} DragDisp;

void    DragDispInit(DragDisp *d, float radius, float strength);
void    DragDispUpdate(DragDisp *d);

/* Returns the displacement vector to add to an element centred at 'center'. */
Vector2 DragDispOffset(const DragDisp *d, Vector2 center);
