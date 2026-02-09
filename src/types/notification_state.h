/*
 * vkQuake RmlUI - Notification State
 *
 * Pure data structures for centerprint and notify messages.
 * No framework dependencies — lives in types/ like game_state.h.
 */

#ifndef QRMLUI_NOTIFICATION_STATE_H
#define QRMLUI_NOTIFICATION_STATE_H

#include <string>

namespace QRmlUI {

// Number of visible notify lines (matches Quake's NUM_CON_TIMES)
constexpr int NUM_NOTIFY_LINES = 4;

struct NotifyLine {
    std::string text;
    double time = 0.0; // realtime when the line appeared
};

struct NotificationState {
    // Centerprint: single overwrite buffer
    std::string centerprint;
    double centerprint_expire = 0.0; // realtime when it should disappear
    double centerprint_start = 0.0;  // realtime when it appeared

    // Notify: rolling ring buffer of recent console lines
    NotifyLine notify[NUM_NOTIFY_LINES];
    int notify_head = 0; // next slot to write into
};

} // namespace QRmlUI

#endif // QRMLUI_NOTIFICATION_STATE_H
