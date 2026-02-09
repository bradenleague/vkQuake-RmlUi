/*
 * vkQuake RmlUI - RmlUI System Interface
 *
 * Platform abstraction layer connecting RmlUI to vkQuake's systems.
 */

#ifndef QRMLUI_SYSTEM_INTERFACE_H
#define QRMLUI_SYSTEM_INTERFACE_H

#include <RmlUi/Core/SystemInterface.h>

struct SDL_Cursor;

namespace QRmlUI {

class SystemInterface : public Rml::SystemInterface {
public:
    SystemInterface();
    ~SystemInterface() override;

    // Initialize with vkQuake's time reference
    void Initialize(double* engine_realtime);

    // -- Inherited from Rml::SystemInterface --

    /// Returns elapsed time in seconds since application start
    double GetElapsedTime() override;

    /// Log message through vkQuake's console
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

    /// Set mouse cursor (uses SDL)
    void SetMouseCursor(const Rml::String& cursor_name) override;

    /// Clipboard operations (uses SDL)
    void SetClipboardText(const Rml::String& text) override;
    void GetClipboardText(Rml::String& text) override;

private:
    double* m_engine_realtime;  // Pointer to vkQuake's realtime variable
    double m_start_time;

    // Pre-allocated SDL cursors (created once, freed in destructor)
    SDL_Cursor* m_cursor_default = nullptr;
    SDL_Cursor* m_cursor_move = nullptr;
    SDL_Cursor* m_cursor_pointer = nullptr;
    SDL_Cursor* m_cursor_resize = nullptr;
    SDL_Cursor* m_cursor_cross = nullptr;
    SDL_Cursor* m_cursor_text = nullptr;
    SDL_Cursor* m_cursor_unavailable = nullptr;
};

} // namespace QRmlUI

#endif // QRMLUI_SYSTEM_INTERFACE_H
