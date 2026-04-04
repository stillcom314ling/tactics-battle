#pragma once

/*
 * Prototype interface — every demo must implement these four functions.
 *
 * Init    — called once when the prototype is launched from the menu.
 * Update  — called every frame with delta time; handles input and logic.
 * Draw    — called every frame between BeginDrawing / EndDrawing.
 *           Must call ClearBackground itself.
 * Deinit  — called when returning to the menu; free any resources.
 */
typedef struct {
    const char *name;         /* short display name shown in the menu  */
    const char *description;  /* one-line description shown in the menu */
    void (*Init)(void);
    void (*Update)(float dt);
    void (*Draw)(void);
    void (*Deinit)(void);
} Prototype;
