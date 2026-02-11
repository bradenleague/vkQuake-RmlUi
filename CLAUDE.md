# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

vkQuake + RmlUI: A Vulkan-based Quake engine with a modern HTML/CSS UI layer.

- **vkQuake** (Vulkan engine) — fork of [Novum/vkQuake](https://github.com/Novum/vkQuake), engine source in `Quake/`
- **RmlUI** (HTML/CSS UI framework) — custom fork submodule in `lib/rmlui/`
- **LibreQuake** (BSD game assets) — PAK files downloaded to `id1/` by `make setup`

Subdirectory CLAUDE.md files provide context when working in `src/`, `ui/`, and `lib/rmlui/`.

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
make setup         # First-time: check deps, init submodule, download PAKs
make               # Build everything (release)
make run           # Build and launch (base game)
make run MOD_NAME=mymod  # Build and launch specific mod
make smoke         # Build + launch engine for ~20 frames (catches startup crashes)
make lua-test      # Build + run Lua test suite
make engine        # Rebuild engine only
make format        # Auto-format with clang-format 17 (matching CI)
make format-check  # Check formatting without modifying files
make meson-setup   # Wipe and reconfigure meson
make clean         # Clean build artifacts
make distclean     # Clean build + id1/ assets
```

### Debug Build
```bash
rm -rf build && meson setup build . --buildtype=debug && meson compile -C build
```

### Manual Run
```bash
./build/vkquake -basedir . -game <mod_dir>
```

## Build & Commit Hygiene

- Always verify the build compiles cleanly (`make`) BEFORE committing refactoring changes. Use `make smoke` to also catch runtime startup crashes.
- After file moves, run a full build to catch broken includes/paths before committing
- When restructuring directories, update ALL references (meson.build, Makefile, `#include` paths, docs) in the SAME commit

## Architecture

### Build Topology

```
Makefile (wrapper) → Meson (primary) → vkquake executable
                          ↳ run_command(cmake) → lib/rmlui/ → librmlui.a + librmlui_debugger.a
                          ↳ glslangValidator → spirv-opt → bintoc → embedded shaders
                          ↳ mkpak → bintoc -c → embedded_pak.c
```

- **Meson** (`meson.build`): Builds vkQuake + `src/` integration. Compiles GLSL → SPIR-V → embedded C arrays.
- **CMake**: RmlUI built at meson-setup time as static libraries, linked into final executable.
- Compiler flags: `-Wall -Wno-trigraphs -Werror` (C), `-Wall` (C++), `-DUSE_RMLUI -DQRMLUI_HOT_RELOAD`
- Feature flags via `meson_options.txt` (`use_rmlui`, `use_sdl3`, codec options, `do_userdirs`)

### Key Directories

- `Quake/` — Engine source (C, gnu11). All UI hooks gated with `#ifdef USE_RMLUI`.
- `src/` — RmlUI integration layer (C++17). See `src/CLAUDE.md` for boundary rules and file map.
- `ui/` — RML documents, RCSS stylesheets, fonts, images. See `ui/CLAUDE.md` for constraints and workflow.
- `Shaders/` — GLSL shaders (engine + RmlUI), compiled to SPIR-V at build time.
- `lib/rmlui/` — RmlUI library fork (submodule).

### Rendering Pipeline

UI renders to a **separate texture**, composited by `postprocess.frag` with barrel warp + chromatic aberration. Push constants `ui_offset_x`/`ui_offset_y` enable HUD inertia effects.

### Data Binding

RmlUI is **retained-mode** (documents loaded once, cached, shown/hidden):

- **`GameDataModel`** ("game"): 50+ bindings — direct (`{{ health }}`) and computed (`{{ weapon_label }}`)
- **`NotificationModel`**: Centerprint + 4 rolling notify lines with visibility/expiry
- **`CvarBinding`** ("cvars"): Two-way sync for sliders, checkboxes, dropdowns

### Mod Integration

Mods at project root use RmlUI automatically (always active when compiled with `USE_RMLUI`):
- `{{ game_title }}` auto-derived from active game directory
- `make run MOD_NAME=mymod` selects active mod

## Code Conventions

- C++ classes: PascalCase (`RenderInterface_VK`), private members: `m_` prefix
- C functions: snake_case, structs: `_t` suffix
- Formatting: `.clang-format` — LLVM-based, tabs (width 4), Allman braces, 160 col limit, `SpaceBeforeParens: Always`

## Git Workflow

- Before committing, always ask user if they want to review changes
- When dealing with submodules, check for detached HEAD state before merging/rebasing
- Squash fix-up commits into clean logical commits before pushing
- CI runs on Linux, macOS, Windows, MinGW, and ARM64 (`.github/workflows/`)

## Skills

- `/rmlui` — Use when editing RCSS/RML files (RmlUI has many syntax differences from standard CSS)
- `/qlua` — Use when editing Lua scripts or `<script>` blocks in RML documents
- `/commit` — Build-verify and commit with submodule-aware workflow

## Key Documentation

- `docs/RMLUI_INTEGRATION.md` — Input handling, menu stack, data binding, full API reference
- `docs/DATA_CONTRACT.md` — Engine-to-UI data flow, binding reference
- `.claude/skills/rmlui/SKILL.md` — RmlUI/RCSS syntax rules and gotchas
- `.claude/skills/qlua/SKILL.md` — Lua scripting philosophy and API gotchas
