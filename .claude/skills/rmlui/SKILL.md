---
name: rmlui
description: RmlUI/RCSS syntax rules, animation patterns, and project-specific menu/HUD workflows. Use when editing .rcss/.rml or RmlUI integration code.
---

# RmlUI Development Skill

RCSS is based on CSS2 with modifications. Many CSS properties work differently or don't exist.

## Critical Differences from CSS

### Layout & Sizing (Recent Lessons)

RmlUi does **not** behave like a web browser with an implicit HTML "user-agent stylesheet". If you don't explicitly set layout semantics for structural elements, containers can end up effectively shrink-to-content, producing tiny computed sizes and therefore wrong borders, alignment, and hitboxes.

#### Always set structural layout explicitly

- Prefer a shared base stylesheet that sets block defaults, e.g. `div { display: block; }`, `p { display: block; }`, `h1..h4 { display: block; }`, and `body { display: block; width: 100%; height: 100%; }`.
- In this repo, `ui/rcss/base.rcss` is the right place for those defaults. Ensure all real UI documents import it first.

#### Percent sizing depends on parent sizing

- `width: 100%` / `height: 100%` only work if the containing block resolves to a definite size.
- If an overlay/viewport chain is missing a definite size, descendants can collapse (eg. a panel computing to something like `172x19`), which makes borders and click hitboxes appear "broken".

#### Flexbox needs explicit constraints to avoid collapse

- In flex rows, one child can collapse another unless you pin constraints.
  - Use fixed `flex-basis` (eg. `flex: 0 0 220dp;`) for labels so they can't steal/lose space unpredictably.
  - Give interactive controls a definite `min-width` and/or `flex: 1 1 <basis>` so they remain usable.
- For container elements inside flex, explicitly set `display: block` and/or `width: 100%` when you want full-width layout.

### Border - NO STYLE KEYWORD
```css
/* WRONG */ border: 1dp solid #2a4060;
/* CORRECT */ border: 1dp #2a4060;

/* WRONG */ border: none;
/* CORRECT */ border-width: 0;
```

### Position Fixed - BROKEN
`position: fixed` = `absolute`. Does NOT position relative to viewport.
```css
/* WRONG */ .overlay { position: fixed; top: 0; left: 0; right: 0; bottom: 0; }
/* CORRECT */ body { width: 100%; height: 100%; }
            .overlay { width: 100%; height: 100%; }
```

### No Background Images
```css
/* WRONG */ background-image: url('bg.png');
/* CORRECT */ decorator: image(bg.png);
```

### Border Radius - No Percentages
```css
/* WRONG */ border-radius: 50%;
/* CORRECT */ border-radius: 6dp;
```

### Font Family - No Fallbacks
```css
/* WRONG */ font-family: LatoLatin, sans-serif;
/* CORRECT */ font-family: LatoLatin;
```

### Units
Use `dp` not `px`: `16dp`, `100%`, `1.5em`

### Other Behavioral Differences
- `z-index` applies to ALL elements (not just positioned)
- `:hover`, `:active`, `:focus` propagate to parents
- No pseudo-elements (`::before`, `::after`)
- Transitions only trigger on class/pseudo-class changes

## Animations and Transitions

RmlUi supports `@keyframes`, `animation`, `transition`, `transform`, and C++ runtime animation.

For full syntax: See [ANIMATION.md](ANIMATION.md)

### Critical Differences from CSS
- **Tweening functions** are NOT CSS `ease`/`cubic-bezier()` — use `<name>-in`/`-out`/`-in-out` (e.g. `quadratic-out`, `elastic-in-out`)
- **Transitions only trigger on class/pseudo-class changes** — `SetProperty()` won't trigger them
- **Transitions require prior style history** — on a freshly loaded document, transitions won't fire because there's no "before" computed state to animate from. Fix: call `SetClass("class-name", false)` before `Show()` to prime the style system, then defer `SetClass(true)` by a few frames (see pattern below)
- **`@keyframes` animations don't persist final values** — after a non-looping animation completes, the base CSS reasserts (effectively `animation-fill-mode: none`). Do NOT set `opacity: 0` as base and rely on the animation to keep it at `opacity: 1` — the element will vanish when the animation ends
- **`@keyframes` on base elements only fire on `LoadDocument()`** — documents are cached via `Show()`/`Hide()`, so animations on always-present elements won't replay on subsequent `Show()` calls. To retrigger: use a **class toggle** that introduces the `animation:` property (remove class, wait 1 frame, re-add), or use **`data-if`** which creates/destroys elements and replays animations on each creation
- **Keyframe names are case-sensitive**
- **Animations apply to local style** — don't mix inline `style=` and animation on same property
- For menu close animations, do NOT call `Hide()` immediately — wait for `transitionend`/`animationend`

### Project Pattern: Menu Enter/Exit Animation

Use **class-driven transitions** (not `@keyframes`) for repeatable entrance animations.

RCSS — base state is hidden, `.menu-enter` class makes visible:
```css
#game-title {
  opacity: 0;
  transform: translateY(12dp);
  transition: opacity transform 0.4s cubic-out;
}

.menu-enter #game-title {
  opacity: 1;
  transform: translateY(0dp);
}
```

C++ — prime the class before Show, defer the add by `MENU_ENTER_DELAY_FRAMES`:
```cpp
// In UI_PushMenu:
doc->SetClass("menu-enter", false);   // Prime style history
doc->Show();
g_state.pending_menu_enter = doc;
g_state.pending_menu_enter_frames = MENU_ENTER_DELAY_FRAMES;

// In UI_Update, AFTER context->Update():
if (g_state.pending_menu_enter) {
    if (g_state.pending_menu_enter_frames > 0) {
        g_state.pending_menu_enter_frames--;
    } else {
        doc->SetClass("menu-enter", true);  // Transition fires
        g_state.pending_menu_enter = nullptr;
    }
}

// On Hide (all paths):
doc->SetClass("menu-enter", false);
doc->Hide();
```

Why this works:
1. `SetClass(false)` before `Show()` primes the style system with a known "class off" state
2. The deferred `SetClass(true)` fires after the document has been through several `Context::Update()` cycles, so the transition system has a valid "before" state
3. On Hide, removing the class resets for the next Show
4. Works on first load AND subsequent shows

Repo hook points:
- `src/ui_manager.cpp` (`UI_PushMenu`, `UI_ProcessPendingEscape`, `UI_CloseAllMenusImmediate`, `UI_Update`)
- `src/internal/menu_event_handler.cpp` (`RegisterWithDocument`, `ActionNavigate`, `ActionClose`)

### Project Pattern: HUD Event Animations

**RCSS-first principle**: Always define animation visuals (timing, easing, keyframes) in RCSS. C++ should only detect game events and toggle classes — never hardcode animation parameters like durations or opacity values in C++. This keeps all visual behavior hot-reloadable via `ui_reload_css` and maintains separation between game logic and UI authoring.

**Preferred approach — class-driven @keyframes:**

Define the animation in RCSS, scoped to a class that C++ toggles on game events:
```css
@keyframes weapon-flicker {
    0% { opacity: 0.2; }
    20% { opacity: 0; }
    40% { opacity: 0.7; }
    55% { opacity: 0.1; }
    100% { opacity: 1; }
}
.weapon-switched .weapon-readout {
    animation: 0.2s linear weapon-flicker;
}
```

C++ only toggles the class (with 1-frame gap so the style system sees the removal and retriggers):
```cpp
// On game event (weapon change):
doc->SetClass("weapon-switched", false);   // Remove to reset
g_state.weapon_flicker_frames = 1;         // Defer re-add

// Next frame:
doc->SetClass("weapon-switched", true);    // Animation fires
```

**When this works:**
- Element persists in DOM (not created/destroyed by `data-if`)
- Multi-step animations (`@keyframes` with multiple stops)
- Any game-event-driven animation (weapon switch, damage, pickups)
- Ambient loops (pulse, blink) — just use `animation:` directly, no class toggle needed

**When to use transitions instead:**
- Simple A → B state changes driven by class/pseudo-class (armor color, ammo highlight)
- `transition: <property> <duration> <tween>` on the base rule

**When C++ `Element::Animate()` is actually needed:**
- Animating properties that depend on runtime-computed values (e.g., position calculated from game state)
- One-off animations where the target value isn't known at CSS authoring time
- Even then, prefer extracting timing/easing constants to RCSS where possible

**Entry animations on `data-if` elements:**
- Elements created/destroyed by `data-if` replay `@keyframes` on each creation
- Apply `animation:` directly — no class toggle needed (e.g., notify lines, centerprint)

## Unsupported Properties

| Property | Alternative |
|----------|-------------|
| `background-image` | `decorator: image()` |
| `text-shadow` | `font-effect: shadow()` or `glow()` |
| `text-align: justify` | Not supported |
| `line-height: normal` | Use explicit value |
| `content: ""` | Use child elements in RML |

## RCSS-Exclusive Features

### Decorators
```css
decorator: image(icon.png);
decorator: horizontal-gradient(#ff0000 #0000ff);
decorator: tiled-horizontal(btn-l, btn-c, btn-r);  /* 3-slice */
decorator: tiled-box(tl, t, tr, l, c, r, bl, b, br);  /* 9-slice */
```

### Font Effects
```css
font-effect: glow(2dp #ff0000);
font-effect: outline(2dp black);
font-effect: shadow(2dp 2dp #000);
```

### Sprite Sheets
```css
@spritesheet theme {
  src: sprites.png;
  icon-play: 0px 0px 32px 32px;
}
```

## Form Element Selectors

```css
input.text { }           /* <input type="text"/> */
input.range { }          /* <input type="range"/> (slider) */
input.checkbox:checked { }
input.range slidertrack { }
input.range sliderbar { }
select selectvalue { }
select selectbox option { }
```

## Data Binding Quick Reference

```html
{{ variable }}                              <!-- Text interpolation -->
<div data-if="condition">                   <!-- Conditional -->
<div data-for="item : list">                <!-- Loop -->
<input type="range" data-value="volume"/>   <!-- Two-way binding -->
<button data-event-click="action()">        <!-- Event handler -->
```

For full syntax: See [DATA_BINDING.md](DATA_BINDING.md)

## Project Structure

```
ui/rml/menus/     - Menu screens
ui/rml/hud/       - HUD overlays
ui/rcss/*.rcss    - Stylesheets
```

Fonts: `LatoLatin`, `OpenSans` (loaded in `src/ui_manager.cpp`)

## Menu Actions (Project-Specific)

```html
<button data-action="navigate('options')">Options</button>
<button data-action="console('map e1m1')">Start</button>
```

## Debugging

- `ui_menu <path>` - Open RML document
- `ui_debugger` - Toggle visual debugger
- When debugging layout bugs, inspect computed element sizes first. If containers are tiny, fix the layout chain (explicit `display`, explicit parent sizing, and flex constraints) before tweaking borders/spacing.
- For animation bugs, verify the class actually changes and the property is transitionable.
- If a close animation never finishes, check that `animationend`/`transitionend` listener is registered on the animated element.

## References

- [Property Index](https://mikke89.github.io/RmlUiDoc/pages/rcss/property_index.html)
- [Decorators](https://mikke89.github.io/RmlUiDoc/pages/rcss/decorators.html)
- [Data Binding](https://mikke89.github.io/RmlUiDoc/pages/data_bindings.html)
