/*
 * vkQuake RmlUI - RmlUI System Interface Implementation
 */

#include "system_interface.h"
#include <RmlUi/Core/StringUtilities.h>
#include <SDL.h>
#include <cstdio>

#include "engine_bridge.h"

namespace QRmlUI {

SystemInterface::SystemInterface()
    : m_engine_realtime(nullptr)
    , m_start_time(0.0)
{
}

SystemInterface::~SystemInterface()
{
    SDL_FreeCursor(m_cursor_default);
    SDL_FreeCursor(m_cursor_move);
    SDL_FreeCursor(m_cursor_pointer);
    SDL_FreeCursor(m_cursor_resize);
    SDL_FreeCursor(m_cursor_cross);
    SDL_FreeCursor(m_cursor_text);
    SDL_FreeCursor(m_cursor_unavailable);
}

void SystemInterface::Initialize(double* engine_realtime)
{
    m_engine_realtime = engine_realtime;
    if (m_engine_realtime) {
        m_start_time = *m_engine_realtime;
    } else {
        // Fallback to SDL time if engine time not available
        m_start_time = SDL_GetTicks() / 1000.0;
    }
}

double SystemInterface::GetElapsedTime()
{
    if (m_engine_realtime) {
        return *m_engine_realtime - m_start_time;
    }
    // Fallback to SDL
    return (SDL_GetTicks() / 1000.0) - m_start_time;
}

bool SystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message)
{
    const char* type_str = "";
    switch (type) {
        case Rml::Log::LT_ALWAYS:   type_str = "";        break;
        case Rml::Log::LT_ERROR:    type_str = "ERROR: "; break;
        case Rml::Log::LT_WARNING:  type_str = "WARN: ";  break;
        case Rml::Log::LT_INFO:     type_str = "INFO: ";  break;
        case Rml::Log::LT_DEBUG:    type_str = "DEBUG: "; break;
        default: break;
    }

    // Use vkQuake's console for output
    if (type == Rml::Log::LT_DEBUG) {
        Con_DPrintf("[RmlUI] %s%s\n", type_str, message.c_str());
    } else {
        Con_Printf("[RmlUI] %s%s\n", type_str, message.c_str());
    }

    // Return true to continue execution (false would break into debugger)
    return true;
}

void SystemInterface::SetMouseCursor(const Rml::String& cursor_name)
{
    // Lazy-init: create system cursors on first call (SDL video is guaranteed active here)
    if (!m_cursor_default) {
        m_cursor_default = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
        m_cursor_move = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
        m_cursor_pointer = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
        m_cursor_resize = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
        m_cursor_cross = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
        m_cursor_text = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
        m_cursor_unavailable = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NO);
    }

    SDL_Cursor* cursor = nullptr;

    if (cursor_name.empty() || cursor_name == "arrow")
        cursor = m_cursor_default;
    else if (cursor_name == "move")
        cursor = m_cursor_move;
    else if (cursor_name == "pointer" || cursor_name == "hand")
        cursor = m_cursor_pointer;
    else if (cursor_name == "resize" || cursor_name == "ew-resize" ||
             cursor_name == "ns-resize" || cursor_name == "nesw-resize" ||
             cursor_name == "nwse-resize")
        cursor = m_cursor_resize;
    else if (cursor_name == "cross" || cursor_name == "crosshair")
        cursor = m_cursor_cross;
    else if (cursor_name == "text" || cursor_name == "ibeam")
        cursor = m_cursor_text;
    else if (cursor_name == "not-allowed" || cursor_name == "no-drop" ||
             cursor_name == "wait" || cursor_name == "progress")
        cursor = m_cursor_unavailable;
    else if (Rml::StringUtilities::StartsWith(cursor_name, "rmlui-scroll"))
        cursor = m_cursor_move;

    if (cursor)
        SDL_SetCursor(cursor);
}

void SystemInterface::SetClipboardText(const Rml::String& text)
{
    SDL_SetClipboardText(text.c_str());
}

void SystemInterface::GetClipboardText(Rml::String& text)
{
    char* clipboard = SDL_GetClipboardText();
    if (clipboard) {
        text = clipboard;
        SDL_free(clipboard);
    } else {
        text.clear();
    }
}

} // namespace QRmlUI
