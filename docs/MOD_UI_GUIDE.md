# Bundling Custom UI with Your Mod

This guide explains how mods can override, extend, or fully replace the RmlUI menus and HUD.

## How It Works

The engine uses a **layered file search** when loading any UI asset (RML documents, RCSS stylesheets, fonts). When RmlUI requests a file, the `QuakeFileInterface` checks directories in this order:

```
1. <mod_directory>/<path>    ← highest priority
2. <basedir>/<path>          ← fallback
```

This means a mod can override **any** UI file by placing a file at the same relative path inside its own directory. No configuration is needed — the engine finds the mod version automatically.

## Quick Start

### 1. Enable RmlUI in your mod's `quake.rc`

```
// quake.rc — minimum for RmlUI
exec default.cfg
exec config.cfg
exec autoexec.cfg
stuffcmds

ui_use_rmlui_menus 1
ui_use_rmlui_hud 1

startdemos demo1 demo2 demo3
ui_show_when_ready
```

### 2. Override only what you need

Create a `ui/` tree inside your mod directory, mirroring the paths you want to replace:

```
mymod/
├── quake.rc
└── ui/
    └── rcss/
        └── main_menu.rcss       ← custom main menu style
```

Everything you don't override falls through to the base `ui/` directory. You can override a single stylesheet, a single menu, or the entire UI.

### 3. Run

```bash
make run MOD_NAME=mymod
# or
./build/vkquake -game mymod
```

## Mod Directory Layout

A fully customized mod might look like this (all `ui/` entries are optional):

```
mymod/
├── quake.rc                          # Startup script
├── progs.dat                         # Compiled QuakeC (game logic)
├── qcsrc/                            # QuakeC source (optional)
│   └── ...
└── ui/                               # Custom UI overrides
    ├── rml/
    │   ├── menus/
    │   │   ├── main_menu.rml         # Replaces base main menu
    │   │   ├── pause_menu.rml        # Replaces base pause menu
    │   │   └── credits.rml           # New menu (navigate to it from another menu)
    │   └── hud/
    │       └── hud_modern.rml        # Replaces base modern HUD
    ├── rcss/
    │   ├── base.rcss                 # Replaces base styles (colors, fonts, etc.)
    │   ├── menu.rcss                 # Replaces menu layout styles
    │   └── hud.rcss                  # Replaces HUD styles
    └── fonts/
        └── MyCustomFont.ttf          # Replaces a base font (same filename)
```

## Automatic Mod Branding

The `{{ game_title }}` data binding automatically displays your mod's directory name. If your mod directory is `mymod/`, the main menu title shows "MYMOD" — no hardcoding required.

```html
<h1>{{ game_title }}</h1>  <!-- displays "MYMOD" -->
```

## What You Can Override

| Asset Type | Base Path | Override by placing in... |
|------------|-----------|---------------------------|
| Menu documents | `ui/rml/menus/*.rml` | `mymod/ui/rml/menus/*.rml` |
| HUD documents | `ui/rml/hud/*.rml` | `mymod/ui/rml/hud/*.rml` |
| Stylesheets | `ui/rcss/*.rcss` | `mymod/ui/rcss/*.rcss` |
| Fonts | `ui/fonts/*.ttf` | `mymod/ui/fonts/*.ttf` |

### Base Stylesheets

| File | Purpose |
|------|---------|
| `base.rcss` | Reset, typography, color variables, animations |
| `menu.rcss` | Menu panels, navigation, layout |
| `main_menu.rcss` | Main menu specific styles |
| `hud.rcss` | HUD positioning and element styles |
| `widgets.rcss` | Form elements (sliders, checkboxes, dropdowns) |

### HUD Variants

The engine supports three HUD styles, selectable via the `scr_style` cvar:

| Cvar Value | Document | Description |
|------------|----------|-------------|
| 0 | `ui/rml/hud.rml` | Minimal / simple |
| 1 | `ui/rml/hud/hud_classic.rml` | Traditional bottom bar |
| 2 | `ui/rml/hud/hud_modern.rml` | Corner-based with bracket frames |

Override whichever variant(s) your mod uses.

## Writing Custom Menus

### Minimal RML Menu

```html
<rml>
    <head>
        <title>My Menu</title>
        <link type="text/rcss" href="../../rcss/base.rcss" />
        <link type="text/rcss" href="../../rcss/menu.rcss" />
    </head>
    <body data-model="game">
        <h1>{{ game_title }}</h1>

        <button class="btn-primary" onclick="new_game()">
            NEW GAME
        </button>
        <button class="btn" onclick="navigate('options')">
            OPTIONS
        </button>
        <button class="btn" onclick="quit()">
            QUIT
        </button>
    </body>
</rml>
```

### Available Actions

Use these in `onclick` attributes:

| Action | Description |
|--------|-------------|
| `navigate('menu')` | Push a menu onto the stack (`ui/rml/menus/<menu>.rml`) |
| `command('cmd')` | Execute a console command (e.g., `command('map e1m1')`) |
| `close()` | Pop the current menu |
| `close_all()` | Close all menus, return to game |
| `new_game()` | Start a new game |
| `quit()` | Quit the game |
| `load_game('slot')` | Load a saved game |
| `save_game('slot')` | Save the current game |
| `cycle_cvar('name', n)` | Cycle a cvar value by n |

### Data Bindings

All RML documents have access to these bindings via `data-model="game"`:

**Game State** (read-only, updated each frame):
`{{ health }}`, `{{ armor }}`, `{{ ammo }}`, `{{ shells }}`, `{{ nails }}`, `{{ rockets }}`, `{{ cells }}`, `{{ map_name }}`, `{{ level_name }}`, `{{ game_title }}`, `{{ game_time }}`

**Conditional rendering:**
```html
<div data-if="has_quad">QUAD DAMAGE!</div>
<div data-if="health &lt; 25">LOW HEALTH</div>
```

**Cvar bindings** (two-way, via `data-model="cvars"`):
```html
<input type="range" data-value="sensitivity" min="1" max="20" step="0.5" />
```

See [DATA_CONTRACT.md](DATA_CONTRACT.md) for the full list of available bindings.

## RmlUI Constraints

A few things to keep in mind when authoring RML/RCSS:

- **`rgba()` alpha is 0-255, not 0-1** — e.g. `rgba(255, 0, 0, 128)` for 50% red. Hex-alpha also works (`#FF000080`)
- **No `font-effect: glow()`** — use `outline()` or `shadow()` only
- **Logical operators are C-style** — use `&&`, `||`, `!` (not `and`/`or`/`not`)
- **XML escaping in attributes** — `&&` becomes `&amp;&amp;`, `<` becomes `&lt;`
- **Use `position: absolute`** for HUD overlay elements

See the [`/rmlui` skill reference](../.claude/skills/rmlui/SKILL.md) for workflow details and `../.claude/skills/rmlui/*.md` for syntax examples.

## Hot Reload for Development

While the engine is running, you can reload UI assets without restarting:

| Console Command | Effect |
|-----------------|--------|
| `ui_reload` | Full reload — clears document cache, reloads all RML + RCSS |
| `ui_reload_css` | Lightweight — reloads RCSS only, preserves DOM and data bindings |

Workflow: edit your mod's RML/RCSS files, switch to the game, type `ui_reload_css` in the console.

## UI-Related Cvars

Set these in your mod's `quake.rc`:

| Cvar | Default | Description |
|------|---------|-------------|
| `ui_use_rmlui_menus` | 1 | Use RmlUI for menus |
| `ui_use_rmlui_hud` | 0 | Use RmlUI for in-game HUD |
| `ui_use_rmlui` | 1 | Master switch (sets both) |
| `scr_style` | 0 | HUD variant: 0=simple, 1=classic, 2=modern |
| `scr_uiscale` | 1.0 | UI scale factor (0.5–3.0) |

## Example: Style-Only Override

The simplest customization — override just the main menu colors without touching any RML:

```
mymod/
├── quake.rc
└── ui/
    └── rcss/
        └── main_menu.rcss
```

Your `main_menu.rcss` is a complete replacement for the base file, so copy the original and modify it.

## Example: Custom Main Menu

Replace the main menu layout entirely:

```
mymod/
├── quake.rc
└── ui/
    └── rml/
        └── menus/
            └── main_menu.rml
```

Your replacement `main_menu.rml` can link to base stylesheets (they'll resolve from the base `ui/` directory since you didn't override them), or you can bundle your own.
