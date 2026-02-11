# UI Speeds Profiling (`ui_speeds`)

This document describes how the `ui_speeds` profiler works, what each field means, and how to interpret results.

## Overview

`ui_speeds` measures CPU-side RmlUI cost per frame and prints it to the console.

- Cvar: `ui_speeds`
- Defined and printed from: `Quake/gl_screen.c`
- Stats source: `UI_GetPerfStats()` from `src/ui_manager.cpp`

`ui_speeds` is focused on UI only (RmlUI integration path), not core Quake simulation.

## Modes

- `ui_speeds 0`
  - Disabled.
- `ui_speeds 1`
  - Per-frame line.
  - Format:
    - `ui X ms (begin A update B render C end D) dc N tri M`
- `ui_speeds 2`
  - 1-second aggregate line.
  - Includes averages, worst frame in window, draw/triangle avg/max, and worst update culprit.
  - Format:
    - `ui(avg1s) ... worst ... (update ... render ..., culprit <phase> <ms>) dc avg/max tri avg/max`
- `ui_speeds 3`
  - Same as mode 2 plus 1-second average `update` sub-phase breakdown:
    - `ui(update avg1s) dp ... model ... lua ... hud ... notify ... context ... post ...`

## Timing Data Path

Per rendered frame in `SCR_DrawGUI`:

1. `UI_BeginFrame()`
2. `UI_Update()`
3. `UI_Render()`
4. `UI_EndFrame()`
5. `UI_GetPerfStats()`
6. Print according to `ui_speeds` mode

Draw calls and indices come from the RmlUI Vulkan render interface:

- Reset at frame start in `RenderInterface_VK::BeginFrame()`
- Increment per `RenderGeometry()` draw

## Field Definitions

## Top-level frame fields

- `begin`
  - CPU time inside `UI_BeginFrame()`.
- `update`
  - CPU time inside `UI_Update()`.
- `render`
  - CPU time inside `UI_Render()`.
- `end`
  - CPU time inside `UI_EndFrame()`.
- `total`
  - `begin + update + render + end`.
- `dc`
  - Draw calls (`avg/max` in mode 2/3 window).
- `tri`
  - Triangles (`avg/max` in mode 2/3 window), derived from index count / 3.

## Worst-frame fields (mode 2/3)

- `worst ... update X render Y`
  - Maximum `total` frame in the 1-second window and that frame's update/render values.
- `culprit <phase> <ms>`
  - Within the worst-update frame, which update sub-phase had the largest measured time.

## Update sub-phases (mode 3)

- `dp`
  - `UpdateDpRatio()`
- `model`
  - `GameDataModel::Update()`
- `lua`
  - `LuaBridge::Update()` (0 if Lua not compiled)
- `hud`
  - HUD-side C++ logic in `UI_Update()` (class toggles, timers, etc.)
- `notify`
  - `NotificationModel::Update()`
- `context`
  - `g_state.context->Update()` (RmlUI core update/style/layout/data-binding work)
- `post`
  - Final `UI_Update()` tail work after `context->Update()` (for example, binding completion hooks)

## What `ui_speeds` Includes vs Excludes

Included:

- RmlUI integration CPU work in `UI_BeginFrame/Update/Render/EndFrame`
- RmlUI draw-call and index/triangle pressure

Excluded:

- Core Quake game simulation/physics/server frame logic
- GPU time (no Vulkan timestamps here)

## Runtime Behavior Notes

- With `ui_use_rmlui 0`, profiling values return to zero because runtime UI work is gated off.
- In mode 2/3, one line can briefly reflect prior window state when toggling cvars mid-window.
- Profiling output is filtered from RmlUI notify forwarding to reduce self-induced HUD churn during profiling.

## Interpretation Tips

- High average `update` with low `render` usually points to UI invalidation/layout/data-binding churn.
- `culprit context` indicates `Rml::Context::Update()` dominates spikes.
- If averages are sub-ms but occasional spikes are high, this is usually a burst/warmup/invalidation issue, not sustained per-frame overload.

## Suggested Profiling Workflow

1. `ui_use_rmlui 1`
2. `ui_speeds 3`
3. Capture 30-60s around the scenario of interest
4. Look for:
   - recurring culprit phase
   - spike frequency and worst value
   - average `update_context` trend

Use this to decide whether optimization is necessary and where to target it first.
