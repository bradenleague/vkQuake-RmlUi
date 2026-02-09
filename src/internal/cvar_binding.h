/*
 * vkQuake RmlUI - Cvar Binding Manager
 *
 * Two-way synchronization between RmlUI data bindings and Quake console variables.
 * Cvars are registered with a UI name and automatically synced when menus open
 * or when UI values change.
 *
 * Usage in RML:
 *   <body data-model="cvars">
 *     <input type="range" min="1" max="11" step="0.5"
 *            data-value="mouse_speed"
 *            data-event-change="cvar_changed('mouse_speed')"/>
 *   </body>
 */

#ifndef QRMLUI_CVAR_BINDING_H
#define QRMLUI_CVAR_BINDING_H

#include <RmlUi/Core.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "../types/cvar_provider.h"
#include "../types/cvar_schema.h"
#include "../types/video_mode.h"

namespace QRmlUI {

class CvarBindingManager {
public:
    // Set the cvar provider (call before Initialize, or uses default QuakeCvarProvider)
    static void SetProvider(ICvarProvider* provider);

    // Get the current cvar provider
    static ICvarProvider* GetProvider();

    // Initialize the cvar data model
    static bool Initialize(Rml::Context* context);

    // Shutdown and cleanup
    static void Shutdown();

    // Register a float cvar with optional range and step
    static void RegisterFloat(const char* cvar, const char* ui_name,
                              float min = 0.0f, float max = 1.0f, float step = 0.1f);

    // Register a boolean cvar (0/1)
    static void RegisterBool(const char* cvar, const char* ui_name);

    // Register an integer cvar
    static void RegisterInt(const char* cvar, const char* ui_name,
                            int min = 0, int max = 100);

    // Register an enum cvar (integer with fixed number of values)
    // labels is optional array of display names for each value
    static void RegisterEnum(const char* cvar, const char* ui_name,
                             int num_values, const char** labels = nullptr);

    // Register an enum cvar with explicit value list (used for non-contiguous enums)
    static void RegisterEnumValues(const char* cvar, const char* ui_name,
                                   const std::vector<int>& values, const char** labels = nullptr);

    // Register a string cvar
    static void RegisterString(const char* cvar, const char* ui_name);

    // Sync all cvar values to UI (call when opening menu)
    static void SyncToUI();

    // Sync a specific UI value back to its cvar (call on UI change event)
    static void SyncFromUI(const std::string& ui_name);

    // Sync all UI values back to cvars
    static void SyncAllFromUI();

    // Get binding info for a UI name (nullptr if not found)
    static const CvarBinding* GetBinding(const std::string& ui_name);

    // Mark the data model as dirty (triggers UI update)
    static void MarkDirty();

    // Suppress UI-originated change events during sync to avoid feedback loops.
    static bool ShouldIgnoreUIChange();

    // Called after a UI update tick to clear any temporary suppression.
    static void NotifyUIUpdateComplete();

    // Check if initialized
    static bool IsInitialized();

    // Get float value for UI
    static float GetFloatValue(const std::string& ui_name);

    // Set float value from UI
    static void SetFloatValue(const std::string& ui_name, float value);

    // Get bool value for UI
    static bool GetBoolValue(const std::string& ui_name);

    // Set bool value from UI
    static void SetBoolValue(const std::string& ui_name, bool value);

    // Get int value for UI
    static int GetIntValue(const std::string& ui_name);

    // Set int value from UI
    static void SetIntValue(const std::string& ui_name, int value);

    // Get string value for UI
    static Rml::String GetStringValue(const std::string& ui_name);

    // Set string value from UI
    static void SetStringValue(const std::string& ui_name, const Rml::String& value);

    // Cycle an enum cvar (increment by delta, wrapping)
    static void CycleEnum(const std::string& ui_name, int delta = 1);

    // Receive video mode list from engine and populate data model
    static void SyncVideoModes(const ui_video_mode_t* modes, int count);

    // Keybind data model
    static void SyncKeybinds();
    static void UpdateKeybind(const std::string& action, const std::string& key_name);
    static void SetCapturing(const std::string& action);
    static void ClearCapturing();

private:
    static void RegisterAllBindings();
    static void BindEnumLabel(const char* ui_name);

    // Common helpers to create-or-update a value pointer and bind it to the data model
    static void BindOrUpdateInt(const char* ui_name, int value);
    static void BindOrUpdateFloat(const char* ui_name, float value);
    static void BindOrUpdateString(const char* ui_name, const Rml::String& value);

    static Rml::Context* s_context;
    static Rml::DataModelHandle s_model_handle;
    static std::unordered_map<std::string, CvarBinding> s_bindings;
    static std::unordered_map<std::string, std::unique_ptr<float>> s_float_values;
    static std::unordered_map<std::string, std::unique_ptr<int>> s_int_values;
    static std::unordered_map<std::string, std::unique_ptr<Rml::String>> s_string_values;
    static bool s_initialized;
    static ICvarProvider* s_provider;  // Injected cvar provider
    static bool s_ignore_ui_changes;
    static int s_ignore_ui_changes_frames;
};

} // namespace QRmlUI

// C API for Quake integration
#ifdef __cplusplus
extern "C" {
#endif

// Initialize cvar binding system
int CvarBinding_Init(void);

// Shutdown cvar binding system
void CvarBinding_Shutdown(void);

// Register cvars (call during initialization)
void CvarBinding_RegisterFloat(const char* cvar, const char* ui_name,
                                float min, float max, float step);
void CvarBinding_RegisterBool(const char* cvar, const char* ui_name);
void CvarBinding_RegisterInt(const char* cvar, const char* ui_name, int min, int max);
void CvarBinding_RegisterEnum(const char* cvar, const char* ui_name, int num_values);
void CvarBinding_RegisterString(const char* cvar, const char* ui_name);

// Sync operations
void CvarBinding_SyncToUI(void);
void CvarBinding_SyncFromUI(const char* ui_name);
void CvarBinding_CycleEnum(const char* ui_name, int delta);

#ifdef __cplusplus
}
#endif

#endif // QRMLUI_CVAR_BINDING_H
