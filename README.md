# vkQuake + RmlUI

<p align="center">
  <img width="600"
       src="https://github.com/user-attachments/assets/489b35a4-bbca-4cb3-b55c-6483a5deafaa" />
</p>

A [vkQuake](https://github.com/Novum/vkQuake) fork that replaces Quake's menu and HUD systems with a modern HTML/CSS UI layer powered by [RmlUI](https://github.com/mikke89/RmlUi). UI documents are hot-reloadable, scriptable with Lua, and fully overridable by mods. Ships with [LibreQuake](https://github.com/lavenderdotpet/LibreQuake) (BSD-licensed) as the base game content.

## Features

**Declarative UI** — Menus and HUD written in RML (HTML dialect) and RCSS (CSS dialect), not C code
- 50+ real-time data bindings connect engine state (health, weapons, armor, powerups) directly to UI elements
- Two-way cvar sync for settings menus — sliders, toggles, and dropdowns stay in sync with the console
- Hot reload with `ui_reload` and `ui_reload_css` — iterate on UI without restarting the engine
- Built-in RmlUI debugger for inspecting the DOM and styles in real time

**Lua Scripting** — Add interactive behavior to any UI document without touching C++
- Engine bridge API exposes game state, cvars, console commands, and per-frame callbacks
- Full DOM manipulation — create elements, query selectors, set inline styles, toggle classes
- Lua-backed data models that sync with RML data bindings
- Scripts hot-reload alongside documents

**Custom Reticle Elements** — Procedural crosshairs built from `<reticle-dot>`, `<reticle-line>`, `<reticle-ring>`, and `<reticle-arc>` primitives
- 7 animatable RCSS properties (radius, gap, length, width, stroke, start/end angle)
- Weapon-reactive animations driven by RCSS transitions or Lua controllers

**Post-Process Pipeline** — UI composited as a separate render layer with configurable effects
- Barrel warp, chromatic aberration, helmet display echo
- HUD inertia — spring-damped bounce and sway responding to player movement
- Normal or additive blending modes

**Mod-Friendly** — Mods override menus, HUD, styles, fonts, and scripts via standard Quake directory precedence
- Fonts auto-discovered from `<mod>/ui/fonts/` at startup and on mod switch
- Menu title auto-derived from mod directory name
- Example mod included (`ui_lab`) with a terminal visor HUD

**Vulkan Enhancements** — Beyond upstream vkQuake
- Batched texture uploads, GPU timestamp instrumentation
- `VK_KHR_synchronization2` and `VK_KHR_dynamic_rendering` support

<p align="center">
  <img width="600"
       src="https://github.com/user-attachments/assets/fccf44b6-feac-4438-a05e-ca6b4172ca7e" />
</p>

## Getting Started

### Prerequisites

**Arch Linux:**
```bash
sudo pacman -S cmake meson ninja sdl2 vulkan-devel glslang freetype2 lua
```

**Ubuntu / Debian:**
```bash
sudo apt install build-essential cmake meson ninja-build \
  libvulkan-dev vulkan-sdk libsdl2-dev libfreetype-dev liblua5.3-dev \
  libvorbis-dev libopus-dev libopusfile-dev libflac-dev libmad0-dev
```

**macOS:**
```bash
brew install cmake meson ninja sdl2 molten-vk vulkan-headers glslang freetype lua
```

### Clone, Build, and Run

```bash
git clone --recurse-submodules https://github.com/bradenleague/vkQuake-RmlUi.git
cd vkQuake-RmlUi
make setup   # check deps, init submodules, download LibreQuake PAK files
make run     # build and launch
```

To run with a mod:
```bash
make run MOD_NAME=ui_lab
```

### Build Targets

| Command | Description |
|---------|-------------|
| `make` | Build everything (release) |
| `make run` | Build and launch (`MOD_NAME=` to select mod) |
| `make smoke` | Build and run for ~20 frames (CI smoke test) |
| `make engine` | Rebuild engine only |
| `make setup` | First-time setup (deps, submodules, PAK files) |
| `make meson-setup` | Wipe and reconfigure meson |
| `make clean` | Remove build artifacts |
| `make distclean` | Remove build + downloaded assets |

## Modding

Mods are directories at the project root, following the standard Quake convention. To create one:

1. Create a directory (e.g. `mymod/`)
2. Add UI overrides under `mymod/ui/` — RML documents, RCSS stylesheets, Lua scripts, and fonts
3. Run with `make run MOD_NAME=mymod` or `./build/vkquake -game mymod`

The engine searches `<mod>/ui/` first, falling back to the base `ui/` directory. Fonts in `<mod>/ui/fonts/` are loaded automatically.

For a working example, see the `ui_lab/` mod and its [README](ui_lab/README.md).

## UI Development

UI files are runtime assets — edit and reload without rebuilding:

| Console Command | Purpose |
|---------|---------|
| `ui_reload` | Hot reload all RML + RCSS from disk |
| `ui_reload_css` | RCSS-only reload (preserves DOM state) |
| `ui_debugger` | Toggle the RmlUI visual debugger |

## Documentation

| Guide | Contents |
|-------|----------|
| [RMLUI_INTEGRATION](docs/RMLUI_INTEGRATION.md) | Input handling, menu stack, data binding, event dispatch |
| [DATA_CONTRACT](docs/DATA_CONTRACT.md) | Engine-to-UI data flow, all 50+ binding definitions |
| [LUA_SCRIPTING](docs/LUA_SCRIPTING.md) | Lua API reference, engine bridge, example scripts |
| [MOD_UI_GUIDE](docs/MOD_UI_GUIDE.md) | How to bundle custom UI with a mod |
| [CVAR_BINDINGS](docs/CVAR_BINDINGS.md) | Two-way cvar sync reference |
| [POST_PROCESS](docs/POST_PROCESS.md) | Post-process pipeline, HUD inertia physics |

## License

- **Engine + integration code:** GPL v2 (see [LICENSE](LICENSE))
- **RmlUI:** MIT License
- **LibreQuake:** BSD License

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.
