# RmlUI Animation Reference

## @keyframes

```css
@keyframes pulse {
  from { opacity: 1; }
  50%  { opacity: 0.5; }
  to   { opacity: 1; }
}

@keyframes progress {
  0%, 30% { background-color: #d99; }
  50%     { background-color: #9d9; }
  to      { background-color: #f9f; width: 100%; }
}
```

- Percentage selectors: `0%` / `from`, `100%` / `to`, any `N%`
- Keyframe names are **case-sensitive**

## animation Property

```
animation: <duration> <delay>? <tweening-function>?
           [<num-iterations>|infinite]? alternate? paused?
           <keyframes-name>
```

- `<duration>` (required): seconds, e.g. `2s`, `0.3s`
- `<delay>`: default `0s`
- `<tweening-function>`: default `linear-in-out`
- `<num-iterations>` | `infinite`: default `1`
- `alternate`: reverse direction each cycle
- `paused`: don't auto-start
- `<keyframes-name>` (required): references `@keyframes` rule
- **Duration must precede delay**; other values any order

Multiple animations — comma-separated:
```css
animation: 1s elastic-out anim1, 2s infinite alternate anim2;
```

**Warning:** Animations apply to the element's local style. Don't mix inline `style=` and animation on the same property.

**Critical gotchas:**
- **No `animation-fill-mode`** — after a non-looping animation completes, the base CSS reasserts. If your base state has `opacity: 0`, the element will vanish after the animation ends. Don't use `opacity: 0` as a base state with `@keyframes` entrance animations.
- **Animations only fire on `LoadDocument()`** — documents cached via `Show()`/`Hide()` will NOT replay CSS animations on subsequent `Show()` calls. For repeatable entrance effects, use **transitions + class toggling** instead.
- **Use `@keyframes` for**: ambient loops (pulse, blink) and one-shot effects on first load only.
- **Use transitions for**: repeatable state-driven animations (menu enter/exit, panel open/close).

### Animatable Properties

Numbers, lengths, percentages, angles, colors, keywords, transforms, decorators, filters.

**Not animatable:** box shadows.

## transition Property

```
transition: [<property-name>+ | all | none] <duration> <delay>? <tweening-function>?
```

- Property names are **space-separated** (not comma-separated like CSS)
- `all` applies to every animatable property
- Duration must precede delay
- Default tweening: `linear-in-out`

Multiple transitions — comma-separated:
```css
transition: opacity 0.2s cubic-out, transform 0.3s elastic-out;
```

**Critical:** Transitions only trigger on **class or pseudo-class changes**. Programmatic `SetProperty()` calls do NOT trigger transitions.

**First-load gotcha:** On a freshly loaded document, transitions won't fire because there's no prior computed style to transition FROM. Fix: call `SetClass("class-name", false)` before `Show()` to prime the style system, then defer `SetClass(true)` by a few `Context::Update()` cycles so the base state is fully resolved before the transition triggers.

### Example

```css
.panel {
  opacity: 0;
  transform: scale(0.95);
  transition: opacity transform 0.15s quadratic-out;
}
.panel.is-open {
  opacity: 1;
  transform: scale(1.0);
}
```

## Tweening Functions

RmlUI does **NOT** support CSS `ease`, `ease-in-out`, `cubic-bezier()`.

Format: `<name>-in` | `<name>-out` | `<name>-in-out`

| Name | Character |
|------|-----------|
| `linear` | Constant speed |
| `quadratic` | Gentle acceleration |
| `cubic` | Moderate acceleration |
| `quartic` | Strong acceleration |
| `quintic` | Very strong acceleration |
| `sine` | Smooth sine curve |
| `circular` | Circular arc |
| `exponential` | Steep exponential curve |
| `back` | Overshoots target then settles |
| `bounce` | Bounces at endpoint |
| `elastic` | Spring-like oscillation |

Common picks:
- Menus: `quadratic-out` or `cubic-out` (snappy open), `quadratic-in` (close)
- HUD pulses: `sine-in-out` (smooth loop), `elastic-out` (punchy one-shot)
- Hover effects: `back-out` (subtle overshoot)

## Transforms

```
transform: none | <transform-function>+
```

### 2D Functions

| Function | Syntax |
|----------|--------|
| `translate` | `translate(<length-pct>, <length-pct>)` |
| `translateX` | `translateX(<length-pct>)` |
| `translateY` | `translateY(<length-pct>)` |
| `scale` | `scale(<number>)` or `scale(<number>, <number>)` |
| `scaleX` | `scaleX(<number>)` |
| `scaleY` | `scaleY(<number>)` |
| `rotate` | `rotate(<angle>)` |
| `rotateZ` | `rotateZ(<angle>)` (alias for rotate) |
| `skew` | `skew(<angle>, <angle>)` |
| `skewX` | `skewX(<angle>)` |
| `skewY` | `skewY(<angle>)` |
| `matrix` | `matrix(<number> x 6)` |

### 3D Functions

| Function | Syntax |
|----------|--------|
| `translate3d` | `translate3d(<length-pct>, <length-pct>, <length>)` |
| `translateZ` | `translateZ(<length>)` |
| `scale3d` | `scale3d(<number>, <number>, <number>)` |
| `scaleZ` | `scaleZ(<number>)` |
| `rotate3d` | `rotate3d(<number>, <number>, <number>, <angle>)` |
| `rotateX` | `rotateX(<angle>)` |
| `rotateY` | `rotateY(<angle>)` |
| `perspective` | `perspective(<length>)` (function form) |
| `matrix3d` | `matrix3d(<number> x 16)` |

Angle units: `deg`, `rad`

### transform-origin

```
transform-origin: <x> <y> <z>?
```

- Initial: `50% 50% 0px`
- X: `left` | `center` | `right` | `<length-pct>`
- Y: `top` | `center` | `bottom` | `<length-pct>`
- Z: `<length>`

### perspective (property form)

```
perspective: none | <length>
perspective-origin: <x> <y>
```

- `none` = infinite distance (no 3D effect)
- Smaller values = more dramatic 3D

### Limitation

Transforms do **not** affect clipping. Workaround:
```css
.transformed-parent {
  overflow: hidden;
  clip: always;
}
```

## Animation Events

| Event | Fires when |
|-------|------------|
| `animationend` | `@keyframes` animation completes all iterations |
| `transitionend` | Transition finishes |

Essential for sequencing — e.g. hiding a menu after close animation:
```cpp
el->AddEventListener(Rml::EventId::Transitionend, listener);
```

## C++ Runtime Animation API

For game-event-driven animation (damage flash, pickup pulse, score popup) where RCSS keyframes aren't flexible enough.

### Element::Animate()

```cpp
bool Element::Animate(
    const String& property_name,
    const Property& target_value,
    float duration,                  // seconds
    Tween tween = {},                // default: linear-in-out
    int num_iterations = 1,          // -1 = infinite
    bool alternate_direction = true,
    float delay = 0.0f,
    const Property* start_value = nullptr  // nullptr = current value
);
```

Examples:
```cpp
// Opacity flash
el->Animate("opacity", Property(0.2f, Unit::NUMBER), 0.15f,
            Tween{Tween::Sine, Tween::Out}, 1, true);

// Margin nudge
el->Animate("margin-left", Property(0.f, Unit::PX), 0.3f,
            Tween{Tween::Sine, Tween::In}, 10, true, 1.f);

// Color pulse
el->Animate("image-color",
            Property(Colourb(255, 50, 50, 255), Unit::COLOUR),
            0.3f, Tween{}, -1, false);

// Transform
auto p = Transform::MakeProperty(
    {Transforms::Rotate2D{10.f}, Transforms::TranslateX{100.f}});
el->Animate("transform", p, 1.8f,
            Tween{Tween::Elastic, Tween::InOut}, -1, true);
```

### Element::AddAnimationKey()

Adds keyframe stops to an **already-running** animation:

```cpp
el->AddAnimationKey("transform", target_property, 1.3f,
                    Tween{Tween::Elastic, Tween::InOut});
el->AddAnimationKey("margin-left", Property(100.f, Unit::PX), 3.0f,
                    Tween{Tween::Circular, Tween::Out});
```

### Tween Enum

```cpp
Tween{Tween::<Name>, Tween::<Direction>}
```

Names: `Back`, `Bounce`, `Circular`, `Cubic`, `Elastic`, `Exponential`, `Linear`, `Quadratic`, `Quartic`, `Quintic`, `Sine`

Directions: `In`, `Out`, `InOut`

### Property Construction

```cpp
Property(1.0f, Unit::NUMBER)           // unitless number
Property(16.f, Unit::DP)              // density-independent pixels (matches RCSS dp)
Property(16.f, Unit::PX)              // raw pixels (does NOT scale with dp ratio)
Property(50.f, Unit::PERCENT)         // percentage
Property(Colourb(r, g, b, a), Unit::COLOUR)  // color
Transform::MakeProperty({...})         // transform
```

**Important:** Use `Unit::DP` (not `Unit::PX`) for lengths that should match RCSS `dp` values. Using PX will produce wrong distances at non-1.0 dp ratios. For transforms:
```cpp
// Correct — matches RCSS translateY(12dp)
Transform::MakeProperty({Transforms::TranslateY{12.f, Unit::DP}});

// Wrong — won't scale with dp ratio
Transform::MakeProperty({Transforms::TranslateY{12.f, Unit::PX}});
```

## Lottie Plugin

Vector animation via rlottie library. **Not currently enabled in this project's build.**

Requires: `rlottie` library + CMake flag `RMLUI_LOTTIE_PLUGIN=ON`

Usage in RML:
```html
<lottie src="animation.json"></lottie>
```
