/*
 * vkQuake RmlUI - Video Mode Type
 *
 * Shared type for passing video mode data from the engine to the UI layer.
 * Used by both the C API (ui_manager.h) and the C++ cvar binding system.
 */

#ifndef QRMLUI_TYPES_VIDEO_MODE_H
#define QRMLUI_TYPES_VIDEO_MODE_H

typedef struct {
    int width;
    int height;
    int is_current;
} ui_video_mode_t;

#endif /* QRMLUI_TYPES_VIDEO_MODE_H */
