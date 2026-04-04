/*
 * Single translation unit that compiles the raygui implementation.
 * NO other file should define RAYGUI_IMPLEMENTATION.
 * raylib.h must be included before raygui.h.
 */
#include "raylib.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
