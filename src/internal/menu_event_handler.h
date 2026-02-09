/*
 * vkQuake RmlUI - Menu Event Handler
 *
 * Handles events from RmlUI menu documents and executes Quake commands.
 * Provides a custom event listener that intercepts onclick/onchange events
 * and parses action strings.
 *
 * Supported actions (use in RML onclick/data-event-click attributes):
 *   navigate('menu_name')       - Push a menu document onto the stack
 *   command('console_cmd')      - Execute a Quake console command
 *   cvar_changed('ui_name')     - Sync UI value to its bound cvar
 *   cycle_cvar('ui_name', 1)    - Increment/decrement an enum cvar
 *   close()                     - Close current menu (pop from stack)
 *   close_all()                 - Close all menus and return to game
 *   quit()                      - Quit the game
 *   new_game()                  - Start new game (skill selection first)
 *   load_game('slot')           - Load saved game from slot
 *   save_game('slot')           - Save current game to slot
 *   bind_key('action')          - Enter key capture mode for binding
 *   connect_to('element_id')    - Read address from text input and connect
 *   host_game('element_id')     - Read map name from text input and host
 *   load_mod('mod_name')        - Load a mod by directory name
 */

#ifndef QRMLUI_MENU_EVENT_HANDLER_H
#define QRMLUI_MENU_EVENT_HANDLER_H

#include <RmlUi/Core.h>
#include <string>
#include <functional>
#include <memory>
#include <vector>
#include "../types/command_executor.h"

namespace QRmlUI {

// Callback for key capture mode
using KeyCaptureCallback = std::function<void(int key, const char* key_name)>;

class MenuEventHandler : public Rml::EventListener {
public:
    // Get singleton instance
    static MenuEventHandler& Instance();

    // Set the command executor (call before Initialize, or uses default QuakeCommandExecutor)
    static void SetExecutor(ICommandExecutor* executor);

    // Get the current command executor
    static ICommandExecutor* GetExecutor();

    // Initialize the event handler with RmlUI context
    static bool Initialize(Rml::Context* context);

    // Shutdown and cleanup
    static void Shutdown();

    // Register event listener with a document
    static void RegisterWithDocument(Rml::ElementDocument* document);

    // Process an action string (can be called directly for testing)
    static void ProcessAction(const std::string& action);

    // Set callback for key capture (for key binding menu)
    static void SetKeyCaptureCallback(KeyCaptureCallback callback);

    // Clear key capture callback
    static void ClearKeyCaptureCallback();

    // Check if waiting for key capture
    static bool IsCapturingKey();

    // Called when a key is captured
    static void OnKeyCaptured(int key, const char* key_name);

    // Cancel key capture mode (e.g., when Escape pressed during capture)
    static void CancelKeyCapture();

    // Rml::EventListener interface
    void ProcessEvent(Rml::Event& event) override;

private:
    MenuEventHandler() = default;
    ~MenuEventHandler() = default;

    // Parse and execute action string
    void ExecuteAction(const std::string& action);

    // Action handlers
    void ActionNavigate(const std::string& menu_path);
    void ActionCommand(const std::string& command);
    void ActionCvarChanged(const std::string& ui_name);
    void ActionCycleCvar(const std::string& ui_name, int delta);
    void ActionClose();
    void ActionCloseAll();
    void ActionQuit();
    void ActionNewGame();
    void ActionLoadGame(const std::string& slot);
    void ActionSaveGame(const std::string& slot);
    void ActionBindKey(const std::string& action);
    void ActionMainMenu();
    void ActionConnectTo(const std::string& element_id);
    void ActionHostGame(const std::string& element_id);
    void ActionLoadMod(const std::string& mod_name);

    // Helper to extract argument from action string like "func('arg')"
    static std::string ExtractArg(const std::string& action);
    static std::pair<std::string, int> ExtractTwoArgs(const std::string& action);

    static MenuEventHandler s_instance;
    static Rml::Context* s_context;
    static KeyCaptureCallback s_key_callback;
    static bool s_capturing_key;
    static std::string s_key_action;  // The action being bound
    static bool s_initialized;
    static ICommandExecutor* s_executor;  // Injected command executor
    static double s_last_new_game_time;
};

// Event listener that stores an action value and executes it when triggered
class ActionEventListener : public Rml::EventListener {
public:
    explicit ActionEventListener(const Rml::String& action) : m_action(action) {}

    void ProcessEvent(Rml::Event& event) override;

    // Called when listener is detached - we can clean up here
    void OnDetach(Rml::Element* element) override;

private:
    Rml::String m_action;
};

// Event instancer to create ActionEventListener for inline event handlers
// Stores created listeners to manage their lifetime (RmlUI does NOT take ownership)
class MenuEventInstancer : public Rml::EventListenerInstancer {
public:
    Rml::EventListener* InstanceEventListener(const Rml::String& value,
                                               Rml::Element* element) override;

    // Clean up all listeners
    void ReleaseAllListeners();

private:
    std::vector<std::unique_ptr<ActionEventListener>> m_listeners;
};

} // namespace QRmlUI

// C API
#ifdef __cplusplus
extern "C" {
#endif

// Initialize menu event handling
int MenuEventHandler_Init(void);

// Shutdown
void MenuEventHandler_Shutdown(void);

// Process an action (for testing or direct invocation)
void MenuEventHandler_ProcessAction(const char* action);

// Key capture API
int MenuEventHandler_IsCapturingKey(void);
void MenuEventHandler_OnKeyCaptured(int key, const char* key_name);

#ifdef __cplusplus
}
#endif

#endif // QRMLUI_MENU_EVENT_HANDLER_H
