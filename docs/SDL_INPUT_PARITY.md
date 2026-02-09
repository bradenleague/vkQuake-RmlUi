# SDL2/SDL3 Input Parity Checklist

All `#ifdef USE_RMLUI` UI integration calls in the SDL input backends should stay mirrored across both `in_sdl2.c` and `in_sdl3.c`. Use this checklist when modifying either file.

## Input guard pattern

Use `UI_WantsInput()` (defined in `ui_manager.h`) for input consumption checks. This helper combines `UI_WantsMenuInput() || UI_IsMenuVisible()` into a single call and keeps both backends consistent.

## Current parity status

- Most routing is mirrored (focus/resize/text/key/mouse/filter guards).
- SDL2 currently has fuller key-rebind capture coverage:
  - Escape cancels capture in keydown path.
  - Mouse buttons and mouse wheel can be captured as bind targets.
- SDL3 currently captures keydown for rebinding, but does not yet mirror SDL2's mouse-button/wheel capture and Escape-cancel behavior.

## Required UI calls per event type

| Event | SDL2 | SDL3 | UI Call |
|-------|------|------|---------|
| Window focus gained | `SDL_WINDOWEVENT_FOCUS_GAINED` | `SDL_EVENT_WINDOW_FOCUS_GAINED` | `UI_WantsInput()` guard, `IN_EndIgnoringMouseEvents()`, `UI_MouseMove()` |
| Window resize | `SDL_WINDOWEVENT_SIZE_CHANGED` | `SDL_EVENT_WINDOW_RESIZED` | `UI_Resize()` |
| Text input | `SDL_TEXTINPUT` | `SDL_EVENT_TEXT_INPUT` | `UI_WantsInput()` guard, `UI_CharEvent()` per character |
| Key down/up | `SDL_KEYDOWN`/`SDL_KEYUP` | `SDL_EVENT_KEY_DOWN`/`SDL_EVENT_KEY_UP` | `UI_IsCapturingKey()` check, then `UI_WantsInput()` guard (except escape), `UI_KeyEvent()` |
| Mouse button | `SDL_MOUSEBUTTONDOWN`/`UP` | `SDL_EVENT_MOUSE_BUTTON_DOWN`/`UP` | `UI_WantsInput()` guard, `UI_MouseButton()` (SDL2 also supports capture-to-bind here) |
| Mouse wheel | `SDL_MOUSEWHEEL` | `SDL_EVENT_MOUSE_WHEEL` | `UI_WantsInput()` guard, `UI_MouseScroll()` (SDL2 also supports capture-to-bind here) |
| Mouse motion | `SDL_MOUSEMOTION` | `SDL_EVENT_MOUSE_MOTION` | Always call `UI_MouseMove()`, then `UI_WantsInput()` guard to block game input |
| Mouse filter | `IN_FilterMouseEvents` | `IN_FilterMouseEvents` | `UI_WantsInput()` to bypass filter when menus need events |

## Verification

After modifying either input backend, grep both files for the same set of `UI_` calls:

```bash
grep -n 'UI_' Quake/in_sdl2.c Quake/in_sdl3.c
```

Confirm the same UI functions appear in both files at matching event types, and explicitly check keybind-capture behavior parity.
