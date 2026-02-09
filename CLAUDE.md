# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

vkQuake + RmlUI: A Vulkan-based Quake engine with a modern HTML/CSS UI layer.

- **vkQuake** (Vulkan-based Quake engine) - fork of [Novum/vkQuake](https://github.com/Novum/vkQuake) (branch: `rmlui`)
- **RmlUI** (HTML/CSS UI framework) - `lib/rmlui/` submodule (branch: `rmlui`)
- **LibreQuake** (BSD-licensed game assets) - PAK files downloaded to `id1/`

RmlUI is a custom fork submodule. Mods are standalone directories at the project root (standard Quake convention).

## Build Commands

### Prerequisites

**Arch Linux (primary):**
```bash
sudo pacman -S cmake meson ninja sdl2 vulkan-devel glslang freetype2
```

**macOS:**
```bash
brew install cmake meson ninja sdl2 molten-vk vulkan-headers glslang freetype
```

### Build & Run
```bash
make setup       # First-time: check deps, init rmlui submodule, download PAK files to id1/
make             # Build everything
make run         # Build and launch
make engine      # Rebuild engine (+ embedded RmlUI deps)
make libs        # Compatibility alias for engine build
make assemble    # Verify id1/ exists
make meson-setup # Wipe and re-run meson setup for engine
make clean       # Clean build artifacts
make distclean   # Clean build artifacts + id1/ assets
```

### Manual Run
```bash
./build/vkquake -game <mod_dir>   # any mod directory at project root
```

### Asset Compilation (requires tools in `tools/`, git-ignored)

QuakeC is compiled automatically by `make run` if `fteqcc` is present in `tools/`.

```bash
LIBREQUAKE_SRC=~/src/LibreQuake ./scripts/compile-maps.sh -m
LIBREQUAKE_SRC=~/src/LibreQuake ./scripts/compile-maps.sh -d src/e1
LIBREQUAKE_SRC=~/src/LibreQuake ./scripts/compile-maps.sh -c
```

On Arch Linux: `yay -S ericw-tools` for map tools, or download binaries into `tools/`.

## Build & Commit Hygiene

- Always verify the build compiles cleanly (`make all` or equivalent) BEFORE committing migration or refactoring changes
- After file moves or directory restructuring, run a full build to catch broken includes/paths before creating any commits
- When restructuring directories, update ALL references (CMakeLists.txt, Makefiles, #include paths, documentation) in the SAME commit

## Architecture

### Build Topology

- **Meson** (`meson.build`): Primary build orchestration for vkQuake and `src/` integration sources.
- **CMake** (`lib/rmlui/`): RmlUI is built at meson-setup time via `run_command(cmake ...)` and linked as static libraries (`librmlui.a`, `librmlui_debugger.a`).

The Makefile is a thin wrapper around Meson build/assemble/run commands.

### RmlUI Integration Layer (`src/`)

```
src/
  ui_manager.h/.cpp     Public C API (extern "C") — what vkQuake calls
  types/                Header-only shared types
    ├── input_mode.h        UI_INPUT_INACTIVE / MENU_ACTIVE / OVERLAY
    ├── game_state.h        Synced game state struct
    ├── cvar_schema.h       Console variable metadata
    ├── cvar_provider.h     ICvarProvider interface
    └── command_executor.h  ICommandExecutor interface
  internal/             C++ implementation (RmlUI + Vulkan adapters)
    ├── render_interface_vk   Custom Vulkan renderer (pipelines, buffers, textures)
    ├── system_interface      Time/logging/clipboard bridge to engine
    ├── game_data_model       Sync Quake game state → RmlUI data bindings
    ├── notification_model    Centerprint + notify line bindings with expiry
    ├── cvar_binding          Two-way sync between cvars and UI elements
    ├── menu_event_handler    Handle menu clicks, parse action strings
    ├── quake_cvar_provider   ICvarProvider implementation
    └── quake_command_executor ICommandExecutor implementation
```

### C/C++ Boundary

- RmlUI layer is C++ (`namespace QRmlUI`, C++17)
- vkQuake engine is pure C (gnu11)
- `src/ui_manager.h` provides `extern "C"` API
- All engine-side UI calls are gated with `#ifdef USE_RMLUI`

### Input Modes (`ui_input_mode_t`)

1. `UI_INPUT_INACTIVE` — Game controls active, RmlUI doesn't capture input
2. `UI_INPUT_MENU_ACTIVE` — Menu captures all input
3. `UI_INPUT_OVERLAY` — HUD visible, input passes through to game

### Directory Layout

The project root is the Quake basedir (standard vkQuake convention):
- `id1/` — base game PAKs (downloaded by `make setup`, gitignored)
- `ui/` — RmlUI documents, stylesheets, and fonts
- `src/` — RmlUI integration layer (C++ bridge between engine and UI)
- Mod directories go at root level (e.g. `mymod/`), each with `quake.rc`, QuakeC source, configs

`DO_USERDIRS` is enabled — engine writes configs and saves to `~/.vkquake/` instead of the source tree.

### Mod Integration

Mods enable the RmlUI UI layer via cvars in their `quake.rc`:
- `ui_use_rmlui_menus 1` / `ui_use_rmlui_hud 1` — enable RmlUI menus/HUD

The main menu title is automatically derived from the active game directory name via `{{ game_title }}` data binding.

QuakeC is compiled automatically by `make run` if `fteqcc` is present in `tools/`. The active mod is selected via `MOD_NAME`: `make run MOD_NAME=mymod`.

### UI Assets (`ui/`)

```
ui/
  rml/
    hud/             HUD variants (modern, classic) + simple default + scoreboard, intermission
    menus/           19 menu documents (main_menu, pause_menu, options, multiplayer, etc.)
  rcss/              Stylesheets (base.rcss, hud.rcss, menu.rcss, widgets.rcss, etc.)
  fonts/             Lato, OpenSans, SpaceGrotesk
```

### HUD & Data Binding

The "game" data model syncs Quake state to RML documents each frame:

- **Engine → `UI_SyncGameState()`** populates `GameState` struct (health, armor, ammo, weapons, items, level stats)
- **`GameDataModel`** exposes 50+ bindings: direct (`{{ health }}`) and computed (`{{ weapon_label }}` via `BindFunc` lambdas)
- **`NotificationModel`** adds centerprint + 4 rolling notify lines with visibility/expiry tracking

HUD variants: `hud_modern.rml` (corner-based with bracket frames), `hud_classic.rml` (traditional bottom bar), `hud.rml` (minimal)

### Rendering Pipeline

UI renders to a **separate texture**, composited by `postprocess.frag` with barrel warp + chromatic aberration. Bright white UI elements get GMUNK-style color fringing naturally from the compositing chain. Push constants `ui_offset_x`/`ui_offset_y` enable HUD inertia effects (jump bounce, camera sway).

### Mod Sources

Each mod directory contains QuakeC source (`<mod>/qcsrc/`), game configs, and `quake.rc`.

## RmlUI Constraints

- RmlUI `rgba()` alpha channel uses 0-255, NOT 0-1 like CSS (e.g. `rgba(255, 0, 0, 128)` for 50% opacity)
- RmlUI does NOT support `font-effect: glow` — avoid CSS properties not in the RmlUI spec
- Use `position: absolute` for HUD overlay elements; `position: relative` will cause layout collapse
- When adjusting opacity values for HUD elements over game visuals, start at 0.8+ (low values like 0.1-0.3 are invisible against game scenes)

## UI Animation Principle

**RCSS-first**: All animation visuals (timing, easing, keyframes, opacity values) belong in RCSS, not C++. C++ should only detect game events and toggle classes — the visual response is defined entirely in stylesheets. This keeps animations hot-reloadable (`ui_reload_css`) and maintains clean separation between game logic and UI authoring. Only use C++ `Element::Animate()` when the animation depends on runtime-computed values that can't be known at CSS authoring time.

## Engine Integration Points

When modifying engine code, all UI hooks are behind `#ifdef USE_RMLUI`:

| Point | Engine File | UI Call |
|-------|-------------|---------|
| Init | `host.c` | `UI_Init()` after video init |
| Frame | `gl_screen.c` | `UI_Update()` and `UI_Render()` |
| Input | `in_sdl2.c` | `UI_*Event()` functions |
| Escape | `keys.c` | `UI_WantsMenuInput()`, `UI_HandleEscape()` |
| Shutdown | `host.c` | `UI_Shutdown()` |

## Console Commands

| Command | Purpose |
|---------|---------|
| `ui_menu [path]` | Open RML document |
| `ui_toggle` | Toggle UI visibility |
| `ui_closemenu` | Close all menus |
| `ui_debugger` | Toggle RmlUI visual debugger |
| `ui_reload` | Hot reload all RML + RCSS from disk (clears document cache) |
| `ui_reload_css` | Lightweight RCSS-only reload (preserves DOM and data bindings) |

## Code Conventions

- C++ classes: PascalCase with underscore suffix for implementation (`RenderInterface_VK`)
- C++ private members: `m_` prefix
- C functions: snake_case
- C structs: `_t` suffix

## C/C++ Type Safety

- When declaring `extern "C"` function wrappers, ensure return types EXACTLY match the original function signatures (e.g., `float` vs `double` for Cvar_VariableValue). Type mismatches in extern C declarations cause silent data corruption.

## Git Workflow

- Before committing, always ask user if they want to review changes or have additional modifications
- When dealing with submodules, check for detached HEAD state and reconcile before attempting merges or rebases
- Squash messy fix-up commits into clean logical commits before pushing

## Skills

Use `/rmlui` when working with RCSS/RML files — RmlUI has many syntax differences from standard CSS.

## Key Documentation

- `docs/RMLUI_INTEGRATION.md` — Input handling system, menu stack, data binding, and full API reference
- `.claude/skills/rmlui/SKILL.md` — RmlUI/RCSS syntax rules and gotchas
