# Prototype Launcher

A multi-prototype Raylib launcher. The main menu (built with raygui) shows a scrollable list of prototypes. Clicking **Launch** runs the selected prototype in a full-screen loop; pressing **ESC** returns to the menu.

## Tech Stack

- **C** (C99)
- **Raylib 5.5** — rendering, input, audio
- **raygui 4.0** — immediate-mode UI (header-only, single-file)
- **Emscripten** — compiles to WebAssembly for the browser
- **GNU Make** — build system

## Project Structure

```
tactics-battle/
├── CLAUDE.md
├── Makefile
├── src/
│   ├── proto.h          # Prototype interface (shared typedef)
│   ├── main.c           # Entry point: window init, state machine, prototype registry
│   ├── menu.h / menu.c  # Scrollable raygui launcher menu
│   ├── raygui_impl.c    # RAYGUI_IMPLEMENTATION — the only file that defines it
│   └── shell.html       # Emscripten HTML shell
├── prototypes/
│   └── balls/
│       ├── balls.h      # extern const Prototype BallsProto;
│       └── balls.c      # Physics demo
└── vendor/
    └── raygui.h         # Downloaded by CI; not committed to git
```

## Commands

```bash
# CI downloads raylib and raygui automatically.
# For local builds, download them first:
#   wget https://github.com/raysan5/raylib/releases/download/5.5/raylib-5.5_webassembly.zip && unzip it
#   wget -O vendor/raygui.h https://raw.githubusercontent.com/raysan5/raygui/4.0/src/raygui.h

make              # Build → dist/index.html
make clean        # Remove dist/
make list-protos  # Show auto-discovered prototype .c files
```

## How to Add a Prototype

1. Create `prototypes/<name>/` folder
2. Create `prototypes/<name>/<name>.h`:
   ```c
   #pragma once
   #include "../../src/proto.h"
   extern const Prototype YourProto;
   ```
3. Create `prototypes/<name>/<name>.c` — implement `YourInit`, `YourUpdate`, `YourDraw`, `YourDeinit`, then define:
   ```c
   const Prototype YourProto = {
       .name        = "Name shown in menu",
       .description = "One-line description.",
       .Init        = YourInit,
       .Update      = YourUpdate,
       .Draw        = YourDraw,
       .Deinit      = YourDeinit,
   };
   ```
4. In `src/main.c`:
   - `#include "../prototypes/<name>/<name>.h"` at the top
   - Add `&YourProto` to the `PROTOTYPES[]` array
5. The Makefile auto-discovers `prototypes/*/*.c` — no Makefile changes needed.

## Prototype Interface

```c
// src/proto.h
typedef struct {
    const char *name;         // short display name
    const char *description;  // one-line description shown in menu
    void (*Init)(void);       // called once on launch
    void (*Update)(float dt); // input + logic, called every frame
    void (*Draw)(void);       // rendering, called between BeginDrawing/EndDrawing
                              // must call ClearBackground itself
    void (*Deinit)(void);     // cleanup on return to menu
} Prototype;
```

`Update` and `Draw` are called as separate passes every frame. The main loop owns `BeginDrawing`/`EndDrawing`; prototypes must NOT call them.

## State Machine (src/main.c)

```
STATE_MENU ──(Launch selected)──► STATE_RUNNING
               calls Init()
STATE_RUNNING ──(ESC pressed)──► STATE_MENU
               calls Deinit()
```

---

## Style Guide

This project uses **C99**. Follow these rules consistently.

### Naming

| Kind | Convention | Example |
|---|---|---|
| Variables, functions | `snake_case` | `ball_count`, `add_ball()` |
| Struct typedefs | `PascalCase` | `typedef struct { ... } TouchSlot;` |
| Constants / macros | `UPPER_SNAKE` | `MAX_BALLS`, `GRAVITY` |
| File-scope statics | prefix `s_` | `static int s_prev_count;` |
| Prototype exports | `PascalCase` | `BallsProto`, `BallsInit()` |

### File Layout

Each `.c` file follows this order:
1. `#include` its own header first
2. System / third-party includes (`raylib.h`, `math.h`, …)
3. `#define` constants local to this file
4. `typedef` / `struct` definitions local to this file
5. `static` variables (module state)
6. `static` helper functions
7. Public functions (matching the header)

### State and Globals

- All mutable module state lives in **file-scope `static` variables** — never bare globals.
- The prototype's state is reset in `Init()`, not at declaration time (the prototype may be launched more than once).
- `main.c` may hold global app state (`app_state`, `menu`, `active_idx`) because it owns the program lifetime.

### Memory

- **Prefer stack and fixed-size arrays.** Most demos fit in a few KB of stack.
- No `malloc`/`free` unless the prototype genuinely needs dynamic allocation. Document why.
- Sizes are compile-time `#define` constants, not magic numbers.

### Headers

- Always use `#pragma once`.
- Expose only what other translation units actually need. Keep helpers `static`.
- Avoid including `raygui.h` in headers — only in `.c` files.

### raygui Usage

- `vendor/raygui.h` is included by `src/raygui_impl.c` with `#define RAYGUI_IMPLEMENTATION`. **No other file defines this.**
- All other files that need raygui just `#include "raygui.h"` — no define.
- Scale UI with screen dimensions (`GetScreenWidth()`, `GetScreenHeight()`), not hard-coded pixels.
- Call `GuiSetStyle(DEFAULT, TEXT_SIZE, ...)` at the start of each Draw call that uses GUI so styles don't leak between frames.

### Emscripten / Platform

- `PLATFORM_WEB` is defined by the Makefile for WASM builds.
- The main loop uses `emscripten_set_main_loop(frame, 0, 1)` on web, plain `while (!WindowShouldClose())` on desktop.
- Touch input is handled via `GetTouchPointCount()` / `GetTouchPointId()` / `GetTouchPosition()`, with a mouse fallback when touch count is 0.

### Error Handling

- Raylib manages GPU/audio resources — no need to check return values of `InitWindow`, `LoadTexture`, etc. for prototypes.
- Use `TraceLog(LOG_WARNING, "...")` for non-fatal issues.

### Comments

Write comments only where the logic isn't self-evident. Avoid restating what the code already says.

---

## Deployment

Push to `main` → GitHub Actions builds and deploys to GitHub Pages automatically.
Enable in repo: Settings → Pages → Source: GitHub Actions.
