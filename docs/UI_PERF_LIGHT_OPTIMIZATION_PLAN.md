# UI Perf Light-Touch Optimization Plan

## Goal
Document a low-risk optimization path for gameplay HUD spikes, with emphasis on a HUD pre-warm approach and clear criteria for whether we should optimize at all.

## Current Findings

- `ui_speeds 3` confirms the gameplay spike offender is `context->Update()` (RmlUI internal update/layout/style pass), not Lua or game simulation.
- Typical gameplay averages are low:
  - `update`: about `0.10-0.28 ms`
  - `render`: about `0.05-0.13 ms`
  - total UI: about `0.14-0.39 ms`
- Worst spikes are intermittent and mostly front-loaded:
  - `update` spikes around `12-16 ms` (culprit `context`)
  - later windows often settle near sub-ms worst values, with occasional `~4-5 ms`
- Main menu cost is substantially lower than gameplay HUD, which is expected because menu is mostly static while HUD is live-updating.
- `ui_use_rmlui 0` results in near-zero UI cost, confirming instrumentation and runtime gating are working.

## What This Means

- This is a spike/invalidation pattern, not a sustained high-average CPU cost problem.
- Lua reticle logic is not the primary offender in observed gameplay spikes.
- The likely expensive work is RmlUI doing style/layout/data-binding recompute bursts when HUD state first becomes active or when state changes cluster.

## Light-Touch Optimization Strategy

## 1) Pre-Warm HUD (Primary Candidate)

Purpose: shift one-time/first-use RmlUI update cost out of active gameplay frames.

Planned behavior:

- When HUD assets are available and Vulkan UI path is initialized, ensure HUD document is loaded early.
- Perform one controlled warm update pass before gameplay-critical moments:
  - parse/instantiate document
  - run update once while hidden/inert
  - keep input mode unchanged
- Do not show visible HUD earlier than intended; pre-warm should only prepare internal RmlUI state.

Expected impact:

- Reduced or eliminated early `context->Update()` spikes immediately after entering gameplay.
- Little to no risk to visual behavior if done as hidden pre-load.

Complexity/risk:

- Low implementation complexity.
- Medium behavior risk only if pre-warm accidentally changes visibility/input mode timing.

## 2) Keep Update Churn Noise Low During Profiling

Purpose: avoid profiler self-noise and false positives while tuning.

- Ensure UI profiling console lines are not forwarded into HUD notify area.
- Keep profiling runs free of extra console spam when possible.

Expected impact:

- Cleaner measurements and better signal quality for real HUD costs.

Complexity/risk:

- Very low.

## 3) Optional Spike Diagnostics (Only If Needed)

Purpose: gather targeted evidence before deeper optimization.

- Add spike-only trace for `context->Update()` when above threshold (for example `> 4 ms`).
- Include lightweight state markers in that trace:
  - menu active
  - intermission
  - scoreboard visible
  - chat active
  - recent notify count

Expected impact:

- Pinpoints which UI state transitions correlate with spikes.

Complexity/risk:

- Low.

## Should We Optimize Now?

Use this gate:

- Optimize now if spikes are user-visible in target scenarios/hardware and recur frequently.
- Defer if:
  - average UI remains sub-ms
  - spikes are mostly startup/warmup and quickly settle
  - no perceptible hitching during normal play

Current data suggests:

- Average cost is already very good.
- A light pre-warm is reasonable if we want to remove early hitch risk with minimal engineering investment.
- No evidence yet that large architectural work is justified.

## Validation Plan

Run this before and after any pre-warm change:

1. `ui_use_rmlui 1`
2. `ui_speeds 3`
3. Start map, capture first 30 seconds
4. Continue another 30 seconds after stable gameplay
5. Compare:
   - count of `update` spikes over `8 ms`
   - worst `context` spike
   - average `update_context_ms`
   - any visible hitch reports

Success criteria:

- Meaningful reduction in first-30s spike count and worst spike magnitude.
- No regressions in menu/HUD correctness or input behavior.

## Non-Goals (for this light pass)

- No deep RmlUI internals rewrite.
- No major HUD redesign.
- No broad data model architecture changes.

## Next Step Recommendation

- Implement only HUD pre-warm first.
- Re-measure with the validation plan above.
- Decide on deeper optimization only if visible hitching remains.
