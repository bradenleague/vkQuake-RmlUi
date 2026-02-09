# vkQuake + RmlUI

<p align="center">
  <img width="600"
       src="https://github.com/user-attachments/assets/489b35a4-bbca-4cb3-b55c-6483a5deafaa" />
</p>

A [vkQuake](https://github.com/Novum/vkQuake) fork with a modern HTML/CSS UI layer powered by [RmlUI](https://github.com/mikke89/RmlUi). Replaces the original Quake menu and HUD systems with hot-reloadable RML/RCSS documents and real-time data bindings. Uses [LibreQuake](https://github.com/lavenderdotpet/LibreQuake) (BSD-licensed) as the base game assets.

## Getting Started

### Prerequisites

**Arch Linux:**
```bash
sudo pacman -S cmake meson ninja sdl2 vulkan-devel glslang freetype2
```

**macOS:**
```bash
brew install cmake meson ninja sdl2 molten-vk vulkan-headers glslang freetype
```

### Clone, Setup, and Run

```bash
git clone --recurse-submodules https://github.com/bradenleague/vkQuake-RmlUi.git
cd vkQuake-RmlUi
make setup   # checks deps, inits rmlui submodule, downloads PAK files to id1/
make run     # builds and launches base game (LibreQuake)
```

To run a specific mod:
```bash
make run MOD_NAME=mymod
```

### Build Targets

| Command | Description |
|---------|-------------|
| `make` | Build everything |
| `make run` | Build and launch (set `MOD_NAME` to select mod) |
| `make engine` | Build the engine (+ embedded RmlUI deps) |
| `make setup` | First-time setup (deps, submodules, PAK files) |
| `make meson-setup` | Re-run meson setup for the engine |
| `make clean` | Remove build artifacts |
| `make distclean` | Remove build artifacts + `id1/` assets |

<p align="center">
  <img width="600"
       src="https://github.com/user-attachments/assets/fccf44b6-feac-4438-a05e-ca6b4172ca7e" />
</p>

## Using with Your Mod

This project follows the standard Quake directory convention — mods are directories at the project root. To create a mod:

1. Create a directory at root (e.g. `mymod/`)
2. Add a `quake.rc` startup script:
   ```
   exec default.cfg
   exec config.cfg
   exec autoexec.cfg
   stuffcmds

   ui_use_rmlui_menus 1
   ui_use_rmlui_hud 1

   startdemos demo1 demo2 demo3
   ui_show_when_ready
   ```
3. Run with `make run MOD_NAME=mymod` or `./build/vkquake -game mymod`

The main menu title is automatically derived from the active game directory name.

## UI Development

UI files (RML/RCSS) are runtime assets — edit them and reload in-engine without rebuilding:

| Command | Purpose |
|---------|---------|
| `ui_reload` | Hot reload all RML + RCSS from disk |
| `ui_reload_css` | Lightweight RCSS-only reload (preserves DOM state) |
| `ui_debugger` | Toggle RmlUI visual debugger |

## Asset Compilation (optional)

Maps and QuakeC can be compiled from source if you have the tooling set up:

```bash
make run                                                        # Compiles QuakeC automatically if fteqcc is present
LIBREQUAKE_SRC=~/src/LibreQuake ./scripts/compile-maps.sh -m   # Optional map compile
```

These require tools in `tools/` (git-ignored). Run the setup script to install them:

```bash
./scripts/setup-tools.sh
```

Or install manually:

- **[ericw-tools](https://github.com/ericwa/ericw-tools/releases)** — `qbsp`, `vis`, `light`, and bundled `*.dylib` files
- **FTEQCC** — QuakeC compiler
  - Linux: [fteqcc64](https://sourceforge.net/projects/fteqw/files/FTEQCC/fteqcc64/download) (place as `tools/fteqcc64`)
  - macOS: build from [fteqw-applesilicon](https://github.com/BryanHaley/fteqw-applesilicon) (place as `tools/fteqcc`)

On Linux, ericw-tools links against system libraries instead. Install with `yay -S ericw-tools` (AUR) or build from source.

> **Note:** PAK files are currently downloaded from a LibreQuake release rather than
> built from source. LibreQuake's `build.py` can generate them with `qpakman`, but
> that toolchain is not yet integrated into the build.

## License

- **Engine integration code:** GPL v2 (see [LICENSE](LICENSE))
- **vkQuake:** GPL v2
- **RmlUI:** MIT License
- **LibreQuake:** BSD License

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.
