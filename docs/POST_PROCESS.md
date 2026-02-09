# Post-Process Pipeline

The post-process pass composites the game world and RmlUI layers into the final swapchain image, applying barrel warp, chromatic aberration, HUD inertia, and color grading. The game renders clean while the UI gets a warped-glass treatment with per-channel color fringing.

## Rendering Architecture

```
Game render pass ──► color_buffers[0] (game texture)
                                                       ──► Post-process pass ──► Swapchain
RmlUI render pass ──► color_buffers[1] (UI texture)
```

The UI renders to a separate Vulkan color buffer with a transparent background. Both buffers are bound as sampler2D inputs (descriptor sets 0 and 1) to a fullscreen triangle post-process pass.

## Shader Overview

### Vertex Shader (`Shaders/postprocess.vert`)

Draws a single fullscreen triangle (3 vertices, no vertex buffer) covering clip space. UVs are derived directly from vertex position.

### Fragment Shader (`Shaders/postprocess.frag`)

Four stages in order:

1. **Game sampling** — Sampled at straight UVs, no distortion applied.

2. **UI distortion** — The UI is scaled down slightly when warped (`ui_scale = 1.0 + |warp| * 0.5`) to create a "curved glass" feel, then barrel-distorted. When chromatic aberration is enabled, R/G/B channels are sampled at different warp strengths:
   - Red: `warp + chroma`
   - Green: `warp` (center channel)
   - Blue: `warp - chroma`

   This splits bright UI elements into visible color fringing at the edges.

3. **Composite** — Premultiplied alpha blend with opacity scale:
   `opacity = clamp(ui_opacity, 0.1, 1.0)`,
   `game * (1 - ui.a * opacity) + ui.rgb * opacity`

4. **Color grading** — Contrast multiply, then gamma power curve.

## Push Constants

All effect parameters are passed per-frame via Vulkan push constants from `GL_EndRenderingTask()` in `Quake/gl_vidsdl.c`.

| Offset | Field | Source | Description |
|--------|-------|--------|-------------|
| 0 | `gamma` | `vid_gamma` cvar | Gamma power curve |
| 4 | `contrast` | `vid_contrast` cvar | Contrast multiplier (clamped 1.0–2.0) |
| 8 | `warp_strength` | `r_ui_warp` cvar | Barrel distortion strength (default `-0.1`) |
| 12 | `chromatic_strength` | `r_ui_chromatic` cvar | Chromatic aberration (default `0.003`, scaled by `1080/height`) |
| 16 | `ui_offset_x` | `v_hud_offset_x` | Horizontal HUD sway (UV-space) |
| 20 | `ui_offset_y` | `v_hud_offset_y` | Vertical HUD bounce (UV-space) |
| 24 | `ui_opacity` | `scr_sbaralpha` cvar | UI layer opacity input (shader clamps effective range to 0.1–1.0) |

## Console Variables

| Cvar | Default | Flags | Description |
|------|---------|-------|-------------|
| `r_ui_warp` | `-0.1` | `CVAR_NONE` | Barrel distortion applied to the UI layer. Negative values = pincushion (inward curve). |
| `r_ui_chromatic` | `0.003` | `CVAR_NONE` | Chromatic aberration intensity. Scaled by `1080/height` so the effect is resolution-independent. Set to `0` to disable. |
| `vid_gamma` | (engine default) | `CVAR_ARCHIVE` | Standard gamma correction. |
| `vid_contrast` | `1.4` | `CVAR_ARCHIVE` | Contrast multiplier, clamped to [1.0, 2.0]. |
| `scr_sbaralpha` | `0.75` | `CVAR_ARCHIVE` | HUD opacity. Controls both the classic status bar and the RmlUI layer via the post-process pass. |

## HUD Inertia

The HUD layer shifts slightly in response to player movement, computed per-frame in `V_UpdateHudInertia()` (`Quake/view.c`). Two independent axes:

### Vertical Bounce
- Triggered on jump (transition from on-ground to airborne with positive Z velocity)
- Fires a downward velocity impulse (`-0.2`)
- Spring constant `omega = 18` — tight enough to match the jump arc

### Horizontal Sway
- Driven by yaw (camera turn) delta each frame
- Scaled to UV impulse: `yaw_delta * -0.002` (HUD lags behind the turn)
- Spring constant `omega = 14` — slightly looser than vertical

Both axes use **critically-damped springs** (`accel = -omega^2 * offset - 2 * omega * velocity`) so the HUD returns smoothly to center without oscillation. Tiny residuals below threshold are zeroed to prevent float drift.

## Source Files

| File | Role |
|------|------|
| `Shaders/postprocess.vert` | Fullscreen triangle vertex shader |
| `Shaders/postprocess.frag` | Compositing + effects fragment shader |
| `Quake/gl_vidsdl.c` | Vulkan resource setup, descriptor sets, push constant dispatch |
| `Quake/view.c` | `V_UpdateHudInertia()` — spring physics for HUD offset |
| `Quake/view.h` | Exports `v_hud_offset_x`, `v_hud_offset_y` |
