/*
 * vkQuake RmlUI - Notification Model
 *
 * Manages centerprint and notify messages for the HUD.
 * Bindings are registered on the "game" data model alongside GameDataModel.
 */

#ifndef QRMLUI_NOTIFICATION_MODEL_H
#define QRMLUI_NOTIFICATION_MODEL_H

#include <RmlUi/Core.h>
#include "../types/notification_state.h"

namespace QRmlUI {

class NotificationModel {
public:
    // Register notification bindings on the game data model constructor.
    // Must be called during GameDataModel::Initialize, before GetModelHandle().
    static void RegisterBindings(Rml::DataModelConstructor& constructor);

    // Store the model handle (call after GetModelHandle in GameDataModel)
    static void SetModelHandle(Rml::DataModelHandle handle);

    // Shutdown and cleanup
    static void Shutdown();

    // Update expiration state. Call each frame from UI_Update.
    // game_time: cl.time from the engine (for centerprint expiry)
    // real_time: realtime from the engine (for notify expiry)
    static void Update(double real_time);

    // Push a centerprint message
    static void CenterPrint(const char* text, double real_time);

    // Push a notify message (console line)
    static void NotifyPrint(const char* text, double real_time);

private:
    static NotificationState s_state;
    static Rml::DataModelHandle s_model_handle;
    static bool s_initialized;

    // Cached visibility state to avoid dirtying every frame
    static bool s_centerprint_was_visible;
    static bool s_centerprint_was_fading;
    static bool s_notify_was_visible[NUM_NOTIFY_LINES];
    static bool s_notify_was_fading[NUM_NOTIFY_LINES];
};

} // namespace QRmlUI

#endif // QRMLUI_NOTIFICATION_MODEL_H
