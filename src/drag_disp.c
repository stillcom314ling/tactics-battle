#include "drag_disp.h"
#include <math.h>

void DragDispInit(DragDisp *d, float radius, float strength)
{
    d->pos      = (Vector2){ 0.0f, 0.0f };
    d->vel      = (Vector2){ 0.0f, 0.0f };
    d->radius   = radius;
    d->strength = strength;
}

void DragDispUpdate(DragDisp *d)
{
    float   dt      = GetFrameTime();
    Vector2 new_pos;

    int tc = GetTouchPointCount();
    if (tc > 0)
        new_pos = GetTouchPosition(0);
    else
        new_pos = GetMousePosition();

    if (dt > 0.0f) {
        d->vel.x = (new_pos.x - d->pos.x) / dt;
        d->vel.y = (new_pos.y - d->pos.y) / dt;
    }
    d->pos = new_pos;
}

Vector2 DragDispOffset(const DragDisp *d, Vector2 center)
{
    float dx   = center.x - d->pos.x;
    float dy   = center.y - d->pos.y;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist >= d->radius || dist < 0.001f) return (Vector2){ 0.0f, 0.0f };

    /* Quadratic falloff: strongest at the cursor, zero at the boundary. */
    float t   = 1.0f - dist / d->radius;
    float mag = d->strength * t * t;

    /* Fast drags push harder — velocity boost capped at +100 %. */
    float speed     = sqrtf(d->vel.x * d->vel.x + d->vel.y * d->vel.y);
    float vel_boost = fminf(speed / 600.0f, 1.0f);
    mag *= 1.0f + vel_boost;

    return (Vector2){ dx / dist * mag, dy / dist * mag };
}
