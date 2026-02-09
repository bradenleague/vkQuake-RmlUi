/*
 * vkQuake RmlUI - Input Mode Type
 *
 * Defines the input capture states for the UI system.
 * C-compatible enum used across the C/C++ boundary.
 */

#ifndef QRMLUI_INPUT_MODE_H
#define QRMLUI_INPUT_MODE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Input mode - controls how RmlUI interacts with vkQuake's input system */
typedef enum {
    UI_INPUT_INACTIVE,      /* Not handling input - game/Quake menu works normally */
    UI_INPUT_MENU_ACTIVE,   /* Menu captures all input (except escape handled specially) */
    UI_INPUT_OVERLAY        /* HUD mode - visible but passes input through to game */
} ui_input_mode_t;

#ifdef __cplusplus
}
#endif

#endif // QRMLUI_INPUT_MODE_H
