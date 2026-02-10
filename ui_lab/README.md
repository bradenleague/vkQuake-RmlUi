# UI Lab Example Mod

This mod showcases a **Terminal Visor HUD** — a diegetic helmet-display design that demonstrates the full power of the RmlUI data binding system.

## Run

```bash
make run MOD_NAME=ui_lab
```

or

```bash
./build/vkquake -game ui_lab
```

## Terminal Visor HUD

A green-on-dark terminal aesthetic inspired by sci-fi helmet visors and system readouts.

### Design Features

- **Mixed typography** — Space Mono (monospace) for labels/values, Space Grotesk for body text
- **Asymmetric L-shape layout** — status left column, vitals bottom strip, center clear for gameplay
- **Visor chrome** — corner L-brackets + system header bar with level/map name
- **5-tier health-reactive theming** — entire palette shifts based on health (green → amber → orange → red)
- **Powerup overrides** — each powerup transforms the HUD aesthetic (Quad=blue, Pent=red, Ring=ghost, Suit=toxic green), effects stack
- **Visor glitch** — damage triggers a 150ms opacity/transform stutter across the whole HUD
- **Weapon contextual readout** — panel morphs color per ammo type (shells/nails/rockets/cells)
- **Mode-adaptive layouts** — SP shows objectives + sigil tracker, DM shows mini-scoreboard via `data-for`
- **Sigil collection tracker** — 4-slot persistent display in SP mode
- **Terminal-style animations** — notify slide-in, centerprint with "INCOMING TRANSMISSION" frame, weapon switch recalibration flicker

### Health Tiers

| Tier | HP Range | Color |
|------|----------|-------|
| 4 | 100+ | Calm green |
| 3 | 80-99 | Slight amber |
| 2 | 60-79 | Warning amber |
| 1 | 40-59 | Orange-red |
| 0 | <40 | Critical red + pulsing borders |

### Testing

- `god` + `hurt 10` — cycle through health tiers
- `give q` / `give p` / `give r` / `give s` — test powerup visual overrides
- Switch weapons — verify contextual color change and recalibration animation
- `ui_reload_css` — hot-reload RCSS changes live

## What It Overrides

- `ui/rml/menus/main_menu.rml`
- `ui/rml/menus/pause_menu.rml`
- `ui/rml/hud/hud.rml`
- `ui/rcss/lab_menu.rcss`
- `ui/rcss/lab_hud.rcss`

## Mods Menu Discovery

The Mods menu discovers directories containing at least one of:
`pak0.pak`, `progs.dat`, `csprogs.dat`, `maps/`, or `ui/`.
`ui_lab` is discovered via its `ui/` directory.

## Quick Validation Checklist

1. On startup, you should see the custom **UI LAB** main menu.
2. `ESC` in-game should open the custom pause menu.
3. HUD should display the terminal visor aesthetic with green-on-dark panels.
4. Visor chrome (corner brackets + header bar) should frame the viewport.
5. Health tier colors should shift as HP changes.
6. Powerups should override the HUD color scheme.
7. `ui_reload` and `ui_reload_css` should update mod files live.
