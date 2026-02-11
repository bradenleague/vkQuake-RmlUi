# Cvar Binding Reference

Console variable (cvar) bindings connect Quake engine state to the RmlUI menus. This document maps bound cvars: engine registration, UI binding name, type, and where they are used in templates (or whether they are currently unused).

For the binding system architecture, see [RmlUI Integration Guide](RMLUI_INTEGRATION.md#data-binding-system).

## Architecture

### Data Flow

```
Engine cvar_t ──► QuakeCvarProvider::GetFloat() ──► CvarBindingManager backing store ──► RmlUI data model "cvars"
                  (Cvar_VariableValue, double→float)     s_float_values / s_int_values       {{ name }}, data-value, data-if
```

**Read path** (engine → UI): `UI_PushMenu()` calls `SyncToUI()`, which iterates all bindings, reads each cvar via `Cvar_VariableValue()`, and writes into backing stores. `MarkDirty()` triggers RmlUI to re-evaluate template expressions.

**Write path** (UI → engine): User interaction fires `cvar_changed('name')` (sliders) or `cycle_cvar('name', 1)` (toggles/enums). `MenuEventHandler` calls `SyncFromUI()` or `CycleEnum()`, which updates the backing store and calls `Cvar_SetValue()`.

**Feedback loop prevention**: `SyncToUI()` sets a 2-frame ignore window so that UI-triggered changes don't re-fire `cvar_changed` events.

### Type Safety

All `extern "C"` declarations correctly match engine signatures:

| Function | Engine Return Type | C++ Declaration | Files |
|----------|-------------------|-----------------|-------|
| `Cvar_VariableValue()` | `double` | `double` | `quake_cvar_provider.cpp:11`, `ui_manager.cpp:32`, `notification_model.cpp:12` |
| `Cvar_SetValue()` | `void` (takes `float`) | `void` (takes `float`) | `quake_cvar_provider.cpp:9` |
| `Cvar_VariableString()` | `const char*` | `const char*` | `quake_cvar_provider.cpp:13` |

One implicit narrowing: `QuakeCvarProvider::GetFloat()` returns `float` from a `double` source. Harmless for Quake's value ranges but worth noting.

### Source Files

| File | Role |
|------|------|
| `src/internal/cvar_binding.h/.cpp` | Binding registration, sync, cycle logic |
| `src/internal/quake_cvar_provider.h/.cpp` | `ICvarProvider` impl, extern "C" wrappers |
| `src/internal/menu_event_handler.cpp` | Dispatches `cvar_changed()` and `cycle_cvar()` actions |
| `src/types/cvar_schema.h` | Cvar metadata (type, range, step, enum values) |
| `src/types/cvar_provider.h` | `ICvarProvider` interface |

## Float Bindings (Sliders)

Registered via `RegisterFloat(engine_name, ui_name, min, max, step)`. Used in RML as `<input type="range" data-value="name" ... onchange="cvar_changed('name')"/>`.

| Engine Cvar | UI Name | Default | Range | Step | RML File | Notes |
|-------------|---------|---------|-------|------|----------|-------|
| `fov` | `fov` | 90 | 50–130 | 5 | `options_graphics.rml` | |
| `gamma` | `gamma` | 0.9 | 0.5–2.0 | 0.05 | `options_graphics.rml` | |
| `contrast` | `contrast` | 1.4 | 0.5–2.0 | 0.05 | `options_graphics.rml` | |
| `host_maxfps` | `max_fps` | 200 | 30–1000 | 10 | `options_graphics.rml` | |
| `volume` | `volume` | 0.7 | 0–1 | 0.05 | `options_sound.rml` | |
| `bgmvolume` | `bgmvolume` | 1 | 0–1 | 0.05 | `options_sound.rml` | |
| `sensitivity` | `sensitivity` | 3 | 1–20 | 0.5 | `options_game.rml` | |
| `scr_sbaralpha` | `hud_opacity` | 0.75 | 0–1 | 0.05 | `options_game.rml` | Hooks into post-process UI opacity |
| `scr_uiscale` | `ui_scale` | 1 | 0.5–3.0 | 0.25 | `options_game.rml` | Registered in engine (`gl_screen.c`) |
| `cl_bob` | `view_bob` | 0.02 | 0–0.05 | 0.005 | `options_game.rml` | |
| `cl_rollangle` | `view_roll` | 2.0 | 0–5.0 | 0.5 | `options_game.rml` | |

## Bool Bindings (Toggles)

Registered via `RegisterBool(engine_name, ui_name)`. Toggled in RML with `onclick="cycle_cvar('name', 1)"` and displayed with `data-if="name"`.

| Engine Cvar | UI Name | Default | RML File | Notes |
|-------------|---------|---------|----------|-------|
| `vid_fullscreen` | `fullscreen` | 0 | (unused in RML templates) | Video menu uses command path (`vid_nextfullscreen`) + computed label |
| `vid_vsync` | `vsync` | 0 | (unused in RML templates) | Video menu uses command path (`vid_nextvsync`) + computed label |
| `vid_palettize` | `palettize` | 0 | `options_graphics.rml` | |
| `r_dynamic` | `dynamic_lights` | 1 | `options_graphics.rml` | |
| `r_waterwarp` | `underwater_fx` | 1 | `options_graphics.rml` | |
| `r_lerpmodels` | `model_interpolation` | 1 | `options_graphics.rml` | |
| `m_pitch` | `invert_mouse` | 0.022 | `options_game.rml` | Sign-based: reads `m_pitch < 0 ? 1 : 0`, flips sign on toggle |
| `cl_alwaysrun` | `always_run` | 1 | `options_game.rml` | |
| `m_filter` | `m_filter` | 0 | `options_game.rml` | |
| `r_drawviewmodel` | `show_gun` | 1 | `options_game.rml` | `CVAR_NONE` — not archived |
| `cl_startdemos` | `startup_demos` | 1 | `options_game.rml` | |
| `bgm_extmusic` | `bgm_extmusic` | 1 | `options_sound.rml` | |
| `snd_waterfx` | `snd_waterfx` | 1 | `options_sound.rml` | |

## Enum Bindings (Cycle Buttons)

Registered via `RegisterEnum(engine_name, ui_name, values, labels)`. Cycled with `onclick="cycle_cvar('name', 1)"`, label shown via `{{ name_label }}`.

| Engine Cvar | UI Name | Default | Values → Labels | RML File | Notes |
|-------------|---------|---------|-----------------|----------|-------|
| `gl_picmip` | `texture_quality` | 0 | 0=High, 1=Medium, 2=Low | `options_graphics.rml` | `CVAR_NONE` |
| `vid_filter` | `texture_filter` | 0 | 0=Smooth, 1=Classic | `options_graphics.rml` | |
| `vid_anisotropic` | `aniso` | 0 | 1/2/4/8/16 = Off–16x | `options_graphics.rml` | |
| `vid_fsaa` | `msaa` | 0 | 0/2/4/8 = Off–8x | `options_graphics.rml` | |
| `vid_fsaamode` | `aa_mode` | 0 | 0=Off, 1=FXAA, 2=TAA | `options_graphics.rml` | |
| `r_scale` | `render_scale` | 1 | 1/2/4 = Native/2x/4x | `options_graphics.rml` | |
| `r_particles` | `particles` | 1 | 0=Off, 1=Classic, 2=Enhanced | `options_graphics.rml` | |
| `r_enhancedmodels` | `enhanced_models` | 1 | 0=Off, 1=On | `options_graphics.rml` | |
| `snd_mixspeed` | `sound_quality` | 44100 | 11025/22050/44100/48000 | `options_sound.rml` | `CVAR_NONE` |
| `ambient_level` | `ambient` | 0.3 | 0=Off, 1=On | `options_sound.rml` | |
| `crosshair` | `crosshair` | 1 | 0=Off, 1=Cross, 2=Dot, 3=Circle, 4=Chevron | `options_game.rml` | |
| `scr_showfps` | `show_fps` | 0 | 0=Off, 1=On | `options_game.rml` | |
| `sv_aim` | `sv_aim` | 1 | 0=Off, 1=On | `options_game.rml` | `CVAR_NONE` |
| `v_gunkick` | `gun_kick` | 1 | 0=Off, 1=Classic, 2=Smooth | `options_game.rml` | |
| `autofastload` | `auto_load` | 0 | 0=Off, 1=On | `options_game.rml` | |
| `skill` | `skill` | 1 | 0–3 = Easy/Normal/Hard/Nightmare | `options_game.rml` | `CVAR_NONE` |
| `_cl_color` | `cl_color_top` | 0 | 0–13 (color names) | `player_setup.rml` | Packed/unpacked (top nibble) |
| `_cl_color` | `cl_color_bottom` | 0 | 0–13 (color names) | `player_setup.rml` | Packed/unpacked (bottom nibble) |
| `deathmatch` | `deathmatch` | 0 | 0=Cooperative, 1=Deathmatch | `create_game.rml` | |
| `teamplay` | `teamplay` | 0 | 0=Off, 1=No Friendly Fire, 2=Friendly Fire | `create_game.rml` | |
| `fraglimit` | `fraglimit` | 0 | 0/10/20/30/40/50/60/70/80/90/100 | `create_game.rml` | |
| `timelimit` | `timelimit` | 0 | 0/5/10/15/20/25/30/45/60 | `create_game.rml` | |

## Direct Reads (Outside Binding System)

These cvars are read via raw `Cvar_VariableValue()` calls in C++ code, not through `CvarBindingManager`.

| Cvar | Read Location | Purpose |
|------|---------------|---------|
| `scr_uiscale` | `ui_manager.cpp` — `UpdateDpRatio()` | UI dp_ratio scaling; defaults to 1.0 if <0.5 |
| `con_notifytime` | `notification_model.cpp` | Notify line expiry duration; defaults to 3.0 if <=0 |
| `scr_centertime` | `notification_model.cpp` | Centerprint display duration; defaults to 2.0 if <=0 |
| `scr_printspeed` | `notification_model.cpp` | Centerprint character reveal speed (chars/sec); defaults to 8 if <=0 |

## Non-Archived Cvars

These cvars use `CVAR_NONE` and won't persist to config. Changes made in the options menu are lost on restart unless the engine has another save mechanism.

- `gl_picmip` (texture quality)
- `skill`
- `sv_aim`
- `snd_mixspeed` (sound quality)
- `r_drawviewmodel` (show gun)

## Known Issues

No critical cvar-binding correctness issues are currently known in this layer.

Current limitation:
- `fullscreen` and `vsync` are still registered bindings, but current UI uses `vid_nextfullscreen`/`vid_nextvsync` command actions in `options_video.rml` rather than `cycle_cvar()` on those specific UI names.
