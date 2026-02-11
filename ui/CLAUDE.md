# ui/ — RmlUI Documents and Stylesheets

Use `/rmlui` skill when editing RCSS/RML — RmlUI has many syntax differences from standard CSS.

## File Layout

```
rml/
  hud/       hud.rml (modern corner-based), scoreboard.rml, intermission.rml
  menus/     19 menu documents (main_menu, pause_menu, options, multiplayer, etc.)
rcss/        base.rcss, hud.rcss (core + default HUD), centerprint.rcss,
             notify.rcss, chat.rcss, scoreboard.rcss, intermission.rcss,
             menu.rcss, main_menu.rcss, widgets.rcss
fonts/       Lato, OpenSans, SpaceGrotesk
```

## Hot Reload

Edit RCSS/RML, then reload in-game without restarting:

| Command | What it does |
|---------|-------------|
| `ui_reload` | Full reload — RML + RCSS, clears document cache |
| `ui_reload_css` | RCSS-only — preserves DOM and data bindings |
| `ui_debugger` | Toggle RmlUI visual debugger (inspect elements, computed styles) |
| `ui_menu <path>` | Open a specific RML document for testing |

## RmlUI Constraints

- `rgba()` alpha is 0-255, NOT 0-1 (e.g. `rgba(255, 0, 0, 128)` = 50% opacity)
- No `font-effect: glow` — check the RmlUI property index before using CSS properties
- Use `position: absolute` for HUD overlays; `relative` causes layout collapse
- HUD opacity: start at 0.8+ (low values invisible against game scenes)
- `position: fixed` is broken — behaves like `absolute`, not viewport-relative

## Animation Principle

**RCSS-first**: All animation timing, easing, and keyframes belong in RCSS. C++ only toggles classes. This keeps animations hot-reloadable via `ui_reload_css`.

Only use `Element::Animate()` when the animation depends on runtime-computed values that can't be known at CSS authoring time.

## Data Binding Models

Available in RML documents via `{{ variable }}` syntax:

- **"game" model** — 50+ bindings synced from engine each frame: `{{ health }}`, `{{ armor }}`, `{{ weapon_label }}`, `{{ ammo_count }}`, etc.
- **"cvars" model** — Two-way cvar sync for sliders/checkboxes/dropdowns in options menus
- **Notifications** — `{{ centerprint_text }}`, `{{ notify_line_N }}` with visibility/expiry

See `docs/DATA_CONTRACT.md` for the complete binding reference.

## Menu Actions

```html
<button data-action="navigate('options')">Options</button>
<button data-action="command('map e1m1')">Start</button>
<button data-action="close()">Back</button>
<button data-action="quit()">Quit</button>
```

Supported actions: `navigate()`, `command()`, `cvar_changed()`, `cycle_cvar()`, `close()`, `close_all()`, `quit()`, `new_game()`, `load_game()`, `save_game()`, `bind_key()`, `main_menu()`, `connect_to()`, `host_game()`, `load_mod()`.
