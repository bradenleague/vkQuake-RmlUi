/*
 * vkQuake RmlUI - Notification Model Implementation
 *
 * Manages centerprint and notify messages, exposing them to RmlUI
 * via bindings on the "game" data model.
 */

#include "notification_model.h"
#include "../types/game_state.h"

#include "engine_bridge.h"

namespace QRmlUI {

// Duration of the fade-out transition (must match RCSS .notify-line transition)
constexpr double NOTIFY_FADE_SECONDS = 0.4;

// Duration of the centerprint flicker-out animation (must match RCSS banner-flicker-out)
constexpr double CENTERPRINT_FLICKER_SECONDS = 0.35;

// Game state lives in game_data_model.cpp
extern GameState g_game_state;

// Static members
NotificationState NotificationModel::s_state;
Rml::DataModelHandle NotificationModel::s_model_handle;
bool NotificationModel::s_initialized = false;
bool NotificationModel::s_centerprint_was_visible = false;
bool NotificationModel::s_centerprint_was_fading = false;
bool NotificationModel::s_notify_was_visible[NUM_NOTIFY_LINES] = {};
bool NotificationModel::s_notify_was_fading[NUM_NOTIFY_LINES] = {};

// Bound strings — RmlUI binds to these pointers
static Rml::String s_centerprint_text;
static Rml::String s_notify_text[NUM_NOTIFY_LINES];

void NotificationModel::RegisterBindings(Rml::DataModelConstructor& constructor)
{
    // Centerprint text
    constructor.Bind("centerprint", &s_centerprint_text);

    // Centerprint visibility — true while displayed OR flickering out
    // During intermission, centerprint never expires (matches legacy behavior)
    constructor.BindFunc("centerprint_visible",
        [](Rml::Variant& variant) {
            variant = !s_state.centerprint.empty() &&
                      (g_game_state.intermission ||
                       realtime < (s_state.centerprint_expire + CENTERPRINT_FLICKER_SECONDS));
        });

    // Centerprint fading — true when past display time but within flicker-out window
    constructor.BindFunc("centerprint_fading",
        [](Rml::Variant& variant) {
            variant = !s_state.centerprint.empty() &&
                      !g_game_state.intermission &&
                      realtime >= s_state.centerprint_expire &&
                      realtime < (s_state.centerprint_expire + CENTERPRINT_FLICKER_SECONDS);
        });

    // Finale text: character-by-character reveal during intermission type 2/3
    // Returns truncated centerprint based on elapsed time * scr_printspeed
    constructor.BindFunc("finale_text",
        [](Rml::Variant& variant) {
            if (s_state.centerprint.empty() || g_game_state.intermission_type < 2) {
                variant = Rml::String("");
                return;
            }
            double printspeed = Cvar_VariableValue("scr_printspeed");
            if (printspeed <= 0.0) printspeed = 8.0;
            double elapsed = realtime - s_state.centerprint_start;
            int chars = static_cast<int>(printspeed * elapsed);
            if (chars <= 0) {
                variant = Rml::String("");
                return;
            }
            int len = static_cast<int>(s_state.centerprint.size());
            if (chars >= len) {
                variant = Rml::String(s_state.centerprint.c_str());
            } else {
                variant = Rml::String(s_state.centerprint.substr(0, chars).c_str());
            }
        });

    // Notify lines — 4 fixed slots
    constructor.Bind("notify_0", &s_notify_text[0]);
    constructor.Bind("notify_1", &s_notify_text[1]);
    constructor.Bind("notify_2", &s_notify_text[2]);
    constructor.Bind("notify_3", &s_notify_text[3]);

    // Notify visibility — true while message is displayed OR fading out
    for (int i = 0; i < NUM_NOTIFY_LINES; i++) {
        Rml::String name = "notify_" + std::to_string(i) + "_visible";
        int slot = i;
        constructor.BindFunc(name,
            [slot](Rml::Variant& variant) {
                double notifytime = Cvar_VariableValue("con_notifytime");
                if (notifytime <= 0.0) notifytime = 3.0;
                const auto& line = s_state.notify[slot];
                double elapsed = realtime - line.time;
                variant = !line.text.empty() &&
                          line.time > 0.0 &&
                          elapsed < (notifytime + NOTIFY_FADE_SECONDS);
            });
    }

    // Notify fading — true when past display time but within fade-out window
    for (int i = 0; i < NUM_NOTIFY_LINES; i++) {
        Rml::String name = "notify_" + std::to_string(i) + "_fading";
        int slot = i;
        constructor.BindFunc(name,
            [slot](Rml::Variant& variant) {
                double notifytime = Cvar_VariableValue("con_notifytime");
                if (notifytime <= 0.0) notifytime = 3.0;
                const auto& line = s_state.notify[slot];
                double elapsed = realtime - line.time;
                variant = !line.text.empty() &&
                          line.time > 0.0 &&
                          elapsed >= notifytime &&
                          elapsed < (notifytime + NOTIFY_FADE_SECONDS);
            });
    }

    s_initialized = true;
    Con_DPrintf("NotificationModel: Bindings registered\n");
}

void NotificationModel::SetModelHandle(Rml::DataModelHandle handle)
{
    s_model_handle = handle;
}

void NotificationModel::Shutdown()
{
    if (!s_initialized) return;

    s_state = NotificationState{};
    s_centerprint_text.clear();
    for (int i = 0; i < NUM_NOTIFY_LINES; i++) {
        s_notify_text[i].clear();
    }
    s_model_handle = Rml::DataModelHandle();
    s_initialized = false;
    s_centerprint_was_visible = false;
    s_centerprint_was_fading = false;
    for (int i = 0; i < NUM_NOTIFY_LINES; i++) {
        s_notify_was_visible[i] = false;
        s_notify_was_fading[i] = false;
    }

    Con_DPrintf("NotificationModel: Shutdown\n");
}

void NotificationModel::Update(double real_time)
{
    if (!s_initialized || !s_model_handle) return;

    // Check centerprint visibility transition
    // During intermission, centerprint never expires (matches legacy behavior)
    bool cp_active = !s_state.centerprint.empty() &&
                     (g_game_state.intermission ||
                      real_time < (s_state.centerprint_expire + CENTERPRINT_FLICKER_SECONDS));
    bool cp_fading = cp_active &&
                     !g_game_state.intermission &&
                     real_time >= s_state.centerprint_expire;

    if (cp_active != s_centerprint_was_visible || cp_fading != s_centerprint_was_fading) {
        s_centerprint_text = cp_active ? s_state.centerprint : "";
        s_model_handle.DirtyVariable("centerprint");
        s_model_handle.DirtyVariable("centerprint_visible");
        s_model_handle.DirtyVariable("centerprint_fading");
        s_centerprint_was_visible = cp_active;
        s_centerprint_was_fading = cp_fading;
    }

    // During finale (intermission type 2/3), dirty finale_text every frame
    // so the character-by-character reveal progresses
    if (g_game_state.intermission_type >= 2 && !s_state.centerprint.empty()) {
        s_model_handle.DirtyVariable("finale_text");
    }

    // Check notify visibility transitions
    double notifytime = Cvar_VariableValue("con_notifytime");
    if (notifytime <= 0.0) notifytime = 3.0;

    for (int i = 0; i < NUM_NOTIFY_LINES; i++) {
        const auto& line = s_state.notify[i];
        double elapsed = real_time - line.time;
        bool active = !line.text.empty() &&
                      line.time > 0.0 &&
                      elapsed < (notifytime + NOTIFY_FADE_SECONDS);
        bool fading = active && elapsed >= notifytime;

        if (active != s_notify_was_visible[i] || fading != s_notify_was_fading[i]) {
            s_notify_text[i] = active ? line.text : "";
            s_model_handle.DirtyVariable("notify_" + std::to_string(i));
            s_model_handle.DirtyVariable("notify_" + std::to_string(i) + "_visible");
            s_model_handle.DirtyVariable("notify_" + std::to_string(i) + "_fading");
            s_notify_was_visible[i] = active;
            s_notify_was_fading[i] = fading;
        }
    }
}

void NotificationModel::CenterPrint(const char* text, double real_time)
{
    if (!s_initialized || !text) return;

    double centertime = Cvar_VariableValue("scr_centertime");
    if (centertime <= 0.0) centertime = 2.0;

    s_state.centerprint = text;
    s_state.centerprint_start = real_time;
    s_state.centerprint_expire = real_time + centertime;

    // Immediately update bound string and dirty
    s_centerprint_text = text;
    s_centerprint_was_visible = true;
    s_centerprint_was_fading = false;

    if (s_model_handle) {
        s_model_handle.DirtyVariable("centerprint");
        s_model_handle.DirtyVariable("centerprint_visible");
        s_model_handle.DirtyVariable("centerprint_fading");
    }
}

void NotificationModel::NotifyPrint(const char* text, double real_time)
{
    if (!s_initialized || !text || !text[0]) return;

    // Write into ring buffer at the head position
    int slot = s_state.notify_head;
    s_state.notify[slot].text = text;
    s_state.notify[slot].time = real_time;

    // Strip trailing newline for display
    auto& str = s_state.notify[slot].text;
    while (!str.empty() && (str.back() == '\n' || str.back() == '\r')) {
        str.pop_back();
    }

    // Update bound string and dirty
    s_notify_text[slot] = s_state.notify[slot].text;
    s_notify_was_visible[slot] = true;
    s_notify_was_fading[slot] = false;

    if (s_model_handle) {
        s_model_handle.DirtyVariable("notify_" + std::to_string(slot));
        s_model_handle.DirtyVariable("notify_" + std::to_string(slot) + "_visible");
        s_model_handle.DirtyVariable("notify_" + std::to_string(slot) + "_fading");
    }

    // Advance ring buffer
    s_state.notify_head = (slot + 1) % NUM_NOTIFY_LINES;
}

} // namespace QRmlUI
