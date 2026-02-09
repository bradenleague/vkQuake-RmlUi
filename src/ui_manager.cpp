/*
 * vkQuake RmlUI - UI Manager Implementation
 *
 * Main integration layer between RmlUI and vkQuake.
 * Provides C-compatible API for engine hooks.
 */

#include "ui_manager.h"
#include "internal/render_interface_vk.h"
#include "internal/system_interface.h"
#include "internal/game_data_model.h"
#include "internal/cvar_binding.h"
#include "internal/menu_event_handler.h"
#include "internal/notification_model.h"
#include "internal/ui_paths.h"
#include "internal/sdl_key_map.h"
#include "internal/quake_file_interface.h"

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <string>
#include <vector>
#include <memory>

// vkQuake engine declarations — single source of truth
#include "internal/engine_bridge.h"

namespace {

// Debounce window (seconds) to prevent immediate close when a menu was just opened
constexpr double MENU_DEBOUNCE_SECONDS = 0.1;

// Reference resolution for dp scaling — UI was designed at 1080p
constexpr float REFERENCE_WIDTH  = 1920.0f;
constexpr float REFERENCE_HEIGHT = 1080.0f;
constexpr float DP_RATIO_MIN = 0.5f;
constexpr float DP_RATIO_MAX = 3.0f;
// Tallest menu panel (options_video) is ~850dp. Cap dp_ratio so it fits
// within this fraction of the viewport height at all window sizes.
constexpr float MAX_MENU_HEIGHT_DP = 850.0f;
constexpr float VIEWPORT_FIT_FRACTION = 0.88f;
constexpr int MENU_ENTER_DELAY_FRAMES = 3;
constexpr double MENU_ENTER_RESIZE_SETTLE_SECONDS = 0.12;

// Consolidated mutable state for the UI manager.
// Constants (MENU_DEBOUNCE_SECONDS, REFERENCE_*, DP_RATIO_*, kHudDoc*) stay standalone.
struct UIManagerState {
    // Core RmlUI
    std::unique_ptr<QRmlUI::QuakeFileInterface>  file_interface;
    std::unique_ptr<QRmlUI::RenderInterface_VK>  render_interface;
    std::unique_ptr<QRmlUI::SystemInterface>      system_interface;
    Rml::Context* context       = nullptr;
    bool          initialized   = false;
    bool          visible       = false;
    int           width         = 0;
    int           height        = 0;
    bool          assets_loaded = false;

    // Input & menu stack
    ui_input_mode_t          input_mode     = UI_INPUT_INACTIVE;
    std::vector<std::string> menu_stack;
    double                   menu_open_time = 0.0;

    // HUD & overlay
    std::string current_hud;
    bool        hud_visible          = false;
    bool        scoreboard_visible   = false;
    bool        intermission_visible = false;
    int         last_intermission    = 0;

    // Deferred ops
    bool pending_escape    = false;
    bool pending_close_all = false;
    Rml::ElementDocument* pending_menu_enter = nullptr;
    int pending_menu_enter_frames = 0;
    bool startup_menu_enter = false;
    double last_resize_time = -1.0;
    int weapon_flicker_frames = 0;

    // Document & asset tracking
    std::unordered_map<std::string, Rml::ElementDocument*> documents;
    std::string ui_base_path;
    std::string engine_base_path;

    // Mouse position
    int last_mouse_x = 0;
    int last_mouse_y = 0;

    // Display DPI scale (cached, updated on init/resize)
    float dpi_scale = 1.0f;

    // HiDPI pixel ratio (logical-to-physical, e.g. 2.0 on Retina)
    float pixel_ratio = 1.0f;
};

UIManagerState g_state;

bool IsViewportSettledForMenuEnter()
{
    if (g_state.last_resize_time < 0.0) {
        return true;
    }
    return (realtime - g_state.last_resize_time) >= MENU_ENTER_RESIZE_SETTLE_SECONDS;
}

const char* GetHudDocumentFromStyle()
{
    const double style = Cvar_VariableValue("scr_style");
    if (style < 1.0) {
        return QRmlUI::Paths::kHudSimple;
    }
    if (style < 2.0) {
        return QRmlUI::Paths::kHudClassic;
    }
    return QRmlUI::Paths::kHudModern;
}

// Update cached DPI scale from the display, with sqrt dampening and defensive clamping.
// Raw DPI ratio (e.g. 163/96 = 1.7x) is too aggressive as a straight multiplier —
// sqrt gives a gentle nudge: 1.7 → 1.30, 1.5 → 1.22, 1.0 → 1.0.
void UpdateCachedDpiScale()
{
    float raw = VID_GetDisplayDPIScale();

    if (raw < 0.5f || raw > 4.0f) {
        Con_Printf("WARNING: Display DPI scale %.2f outside expected range, clamping\n", raw);
        if (raw < 0.5f) raw = 0.5f;
        if (raw > 4.0f) raw = 4.0f;
    }

    // Dampen: full physical-size matching is too aggressive, sqrt is a gentler curve
    float scale = (raw > 1.0f) ? sqrtf(raw) : raw;

    if (g_state.dpi_scale != scale) {
        g_state.dpi_scale = scale;
        Cvar_SetValueROM("scr_dpiscale", scale);
    }
}

// Compute and apply dp_ratio based on window size, display DPI, and scr_uiscale cvar.
// DPI contribution fades out as base_ratio exceeds 1.0 — at reference resolution
// (1080p) you get the full DPI bump; at fullscreen 4K it fades to 1.0 since the
// extra pixels already make the UI physically larger.
void UpdateDpRatio()
{
    if (!g_state.context || g_state.width <= 0 || g_state.height <= 0) return;

    float scale_x = static_cast<float>(g_state.width) / REFERENCE_WIDTH;
    float scale_y = static_cast<float>(g_state.height) / REFERENCE_HEIGHT;
    float base_ratio = (scale_x < scale_y) ? scale_x : scale_y;

    // Fade DPI contribution: full at base<=1.0, zero at base>=2.0
    float blend = (base_ratio > 1.0f) ? std::min(base_ratio - 1.0f, 1.0f) : 0.0f;
    float effective_dpi = g_state.dpi_scale * (1.0f - blend) + blend;

    float user_scale = static_cast<float>(Cvar_VariableValue("scr_uiscale"));
    if (user_scale < DP_RATIO_MIN) user_scale = 1.0f;

    float dp_ratio = base_ratio * effective_dpi * user_scale;

    // Viewport cap: ensure tallest menu always fits in the window height.
    // Without this, DPI scaling can push dp content past the viewport edge
    // at small window sizes on high-DPI displays.
    float viewport_cap = VIEWPORT_FIT_FRACTION * static_cast<float>(g_state.height) / MAX_MENU_HEIGHT_DP;
    if (dp_ratio > viewport_cap) dp_ratio = viewport_cap;

    if (dp_ratio < DP_RATIO_MIN) dp_ratio = DP_RATIO_MIN;
    if (dp_ratio > DP_RATIO_MAX) dp_ratio = DP_RATIO_MAX;

    g_state.context->SetDensityIndependentPixelRatio(dp_ratio);
}

bool IsMenuDocumentPath(const std::string& path)
{
    return path.find("/menus/") != std::string::npos;
}

bool HasVisibleMenuDocument()
{
    for (const auto& pair : g_state.documents) {
        if (!IsMenuDocumentPath(pair.first)) {
            continue;
        }
        if (pair.second && pair.second->IsVisible()) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

// C API Implementation

extern "C" {

int UI_Init(int width, int height, const char* base_path)
{
    if (g_state.initialized) {
        Con_Printf("UI_Init: Already initialized\n");
        return 1;
    }

    // Reset any leftover state in case we reinitialize within the same process.
    g_state = UIManagerState{};

    g_state.width = width;
    g_state.height = height;
    g_state.engine_base_path = (base_path && base_path[0]) ? base_path : "";

    // Create interfaces
    g_state.file_interface = std::make_unique<QRmlUI::QuakeFileInterface>();
    g_state.system_interface = std::make_unique<QRmlUI::SystemInterface>();
    g_state.system_interface->Initialize(&realtime);

    g_state.render_interface = std::make_unique<QRmlUI::RenderInterface_VK>();

    // Install interfaces before initializing RmlUI
    Rml::SetFileInterface(g_state.file_interface.get());
    Rml::SetSystemInterface(g_state.system_interface.get());
    Rml::SetRenderInterface(g_state.render_interface.get());

    // Initialize RmlUI
    if (!Rml::Initialise()) {
        Con_Printf("UI_Init: Failed to initialize RmlUI\n");
        return 0;
    }

    // Create context
    g_state.context = Rml::CreateContext("main", Rml::Vector2i(width, height));
    if (!g_state.context) {
        Con_Printf("UI_Init: Failed to create RmlUI context\n");
        Rml::Shutdown();
        return 0;
    }

    // Initialize debugger (optional, for development)
    Rml::Debugger::Initialise(g_state.context);

    g_state.initialized = true;
    UpdateCachedDpiScale();
    UpdateDpRatio();
    Con_DPrintf("UI_Init: RmlUI core initialized (%dx%d)\n", width, height);
    Con_DPrintf("UI_Init: Fonts and documents will load after Vulkan init\n");

    return 1;
}

// Load fonts - called AFTER Vulkan is initialized.
// Uses the QuakeFileInterface which searches com_gamedir → com_basedir → CWD.
static void UI_LoadAssets()
{
    static const char* fonts[] = {
        "ui/fonts/LatoLatin-Regular.ttf",
        "ui/fonts/LatoLatin-Bold.ttf",
        "ui/fonts/LatoLatin-Italic.ttf",
        "ui/fonts/LatoLatin-BoldItalic.ttf",
        "ui/fonts/SpaceGrotesk-Bold.ttf",
    };

    bool any_loaded = false;
    for (const char* font : fonts) {
        if (Rml::LoadFontFace(font)) {
            Con_DPrintf("UI_LoadAssets: Loaded %s\n", font);
            any_loaded = true;
        }
    }

    if (!any_loaded) {
        Con_Printf("UI_LoadAssets: WARNING - No fonts loaded! UI text will not render.\n");
        Con_Printf("UI_LoadAssets: QuakeFileInterface searched: com_gamedir='%s', com_basedir='%s'\n",
                   com_gamedir, com_basedir);
    }

    g_state.assets_loaded = true;
}

void UI_Shutdown(void)
{
    if (!g_state.initialized) return;

    // Shutdown data models first
    QRmlUI::MenuEventHandler::Shutdown();
    QRmlUI::CvarBindingManager::Shutdown();
    QRmlUI::GameDataModel::Shutdown();

    // Unload all documents
    for (auto& pair : g_state.documents) {
        if (pair.second) {
            pair.second->Close();
        }
    }
    g_state.documents.clear();

    // Shutdown debugger
    Rml::Debugger::Shutdown();

    // Destroy context
    if (g_state.context) {
        Rml::RemoveContext("main");
        g_state.context = nullptr;
    }

    // Shutdown RmlUI
    Rml::Shutdown();

    // Cleanup interfaces
    g_state.render_interface->Shutdown();
    g_state.render_interface.reset();
    g_state.system_interface.reset();
    g_state.file_interface.reset();

    // Reset all mutable state so a reinit starts clean.
    g_state = UIManagerState{};
    Con_DPrintf("UI_Shutdown: RmlUI shut down\n");
}

// Internal function to process escape - called from UI_Update to avoid race conditions
static void UI_ProcessPendingEscape(void)
{
    if (!g_state.initialized || !g_state.context) return;

    // Prevent immediate close if menu was just opened (same key event causing open+close)
    if (realtime - g_state.menu_open_time < MENU_DEBOUNCE_SECONDS) {
        return;
    }

    if (g_state.menu_stack.empty()) {
        // No menus on stack, just deactivate
        UI_SetInputMode(UI_INPUT_INACTIVE);
        return;
    }

    // Pop and hide current menu
    std::string current = g_state.menu_stack.back();
    g_state.menu_stack.pop_back();

    auto it = g_state.documents.find(current);
    if (it != g_state.documents.end() && it->second) {
        it->second->SetClass("menu-enter", false);
        it->second->SetClass("startup-enter", false);
        it->second->Hide();
        Con_DPrintf("UI_HandleEscape: Closed menu '%s'\n", current.c_str());
    }

    // If stack empty after pop, return to inactive mode
    if (g_state.menu_stack.empty()) {
        UI_SetInputMode(UI_INPUT_INACTIVE);
        Con_DPrintf("UI_HandleEscape: Menu stack empty, returning to game\n");
#ifdef __cplusplus
        // Restore game input when leaving menus.
        IN_Activate();
        key_dest = key_game;
#endif
    } else {
        // Show the previous menu in the stack (it should already be visible, but ensure it)
        std::string& prev = g_state.menu_stack.back();
        auto prev_it = g_state.documents.find(prev);
        if (prev_it != g_state.documents.end() && prev_it->second) {
            prev_it->second->Show();
            g_state.pending_menu_enter = prev_it->second;
            g_state.pending_menu_enter_frames = MENU_ENTER_DELAY_FRAMES;
        }
    }
}

// Process pending operations - MUST be called from main thread before rendering
void UI_ProcessPending(void)
{
    if (!g_state.initialized || !g_state.context) return;

    // Process pending operations BEFORE rendering can start
    // This ensures UI state changes happen atomically between frames
    if (g_state.pending_escape) {
        g_state.pending_escape = false;
        UI_ProcessPendingEscape();
    }

    if (g_state.pending_close_all) {
        g_state.pending_close_all = false;
        // Close all menus — bypass debounce, bounded by stack size
        const size_t max_iters = g_state.menu_stack.size() + 1;
        for (size_t i = 0; i < max_iters && !g_state.menu_stack.empty(); ++i) {
            // Pop and hide directly (no debounce check)
            std::string current = g_state.menu_stack.back();
            g_state.menu_stack.pop_back();
            auto it = g_state.documents.find(current);
            if (it != g_state.documents.end() && it->second) {
                it->second->SetClass("menu-enter", false);
                it->second->SetClass("startup-enter", false);
                it->second->Hide();
            }
        }
        if (!g_state.menu_stack.empty()) {
            Con_Printf("WARNING: UI_ProcessPending: close-all failed to drain menu stack\n");
        }
        if (g_state.menu_stack.empty()) {
            UI_SetInputMode(UI_INPUT_INACTIVE);
        }
    }

    // Reconcile: if the menu stack says menus are active, make sure the
    // engine-side state agrees.  We trust the stack (set by UI_PushMenu /
    // UI_CloseAllMenusImmediate) rather than probing RmlUI document
    // visibility, which may lag by a frame.
    if (!g_state.menu_stack.empty()) {
        if (g_state.input_mode != UI_INPUT_MENU_ACTIVE) {
            UI_SetInputMode(UI_INPUT_MENU_ACTIVE);
        }
        if (key_dest != key_menu) {
            IN_Deactivate(true);
            key_dest = key_menu;
        }
        IN_EndIgnoringMouseEvents();
        g_state.visible = true;
    } else if (g_state.input_mode == UI_INPUT_MENU_ACTIVE) {
        UI_SetInputMode(UI_INPUT_INACTIVE);
    }
}

void UI_Update(double dt)
{
    if (!g_state.initialized || !g_state.context) return;

    // Recompute dp ratio each frame so scr_uiscale changes take effect live.
    UpdateDpRatio();

    // Note: Pending operations are now processed in UI_ProcessPending()
    // which is called from the main thread before rendering tasks start.

    // Update game data model to sync with Quake state
    QRmlUI::GameDataModel::Update();

    // Weapon switch flicker — toggle "weapon-switched" class on HUD doc.
    // The animation itself is defined in hud.rcss (@keyframes weapon-flicker).
    // Remove the class first to reset, defer re-add by 1 frame so the
    // style system sees the gap and retriggers the animation.
    {
        static int prev_weapon = 0;
        int cur_weapon = QRmlUI::g_game_state.active_weapon;
        if (cur_weapon != prev_weapon && prev_weapon != 0) {
            auto it = g_state.documents.find(g_state.current_hud);
            if (it != g_state.documents.end() && it->second) {
                it->second->SetClass("weapon-switched", false);
            }
            g_state.weapon_flicker_frames = 2;
        }
        prev_weapon = cur_weapon;
    }
    if (g_state.weapon_flicker_frames > 0) {
        g_state.weapon_flicker_frames--;
        if (g_state.weapon_flicker_frames == 0) {
            auto it = g_state.documents.find(g_state.current_hud);
            if (it != g_state.documents.end() && it->second) {
                it->second->SetClass("weapon-switched", true);
            }
        }
    }

    // Update notification expiry state
    QRmlUI::NotificationModel::Update(realtime);

    g_state.context->Update();

    // Apply deferred menu-enter class AFTER Update() has resolved styles.
    // The delay ensures the document has computed its base state (opacity: 0)
    // before the class change triggers a transition.  This also lets the menu
    // background appear for a couple of frames before the content cascades in,
    // which looks cleaner on startup where the console is still visible.
    if (g_state.pending_menu_enter) {
        if (!IsViewportSettledForMenuEnter()) {
            // Keep postponing while resize events are still arriving, then let
            // the normal frame delay elapse once the viewport is stable.
            g_state.pending_menu_enter_frames = MENU_ENTER_DELAY_FRAMES;
        } else if (g_state.pending_menu_enter_frames > 0) {
            g_state.pending_menu_enter_frames--;
        } else {
            Rml::ElementDocument* doc = g_state.pending_menu_enter;
            if (g_state.startup_menu_enter) {
                doc->SetClass("startup-enter", true);
                g_state.startup_menu_enter = false;
            }
            doc->SetClass("menu-enter", true);

            g_state.pending_menu_enter = nullptr;
        }
    }

    // Clear any temporary suppression of UI change events after data bindings update.
    QRmlUI::CvarBindingManager::NotifyUIUpdateComplete();

}

void UI_Render(void)
{
    if (!g_state.initialized || !g_state.context || !g_state.visible) return;
    g_state.context->Render();
}

void UI_Resize(int width, int height)
{
    if (!g_state.initialized || !g_state.context) return;

    if (g_state.width != width || g_state.height != height) {
        g_state.last_resize_time = realtime;
    }

    g_state.width = width;
    g_state.height = height;
    g_state.context->SetDimensions(Rml::Vector2i(width, height));
    UpdateCachedDpiScale();
    UpdateDpRatio();
}

void UI_SetPixelRatio(float ratio)
{
    if (ratio > 0.0f)
        g_state.pixel_ratio = ratio;
}

int UI_KeyEvent(int key, int scancode, int pressed, int repeat)
{
    if (!g_state.initialized || !g_state.context) return 0;
    if (!g_state.visible && g_state.menu_stack.empty()) return 0;

    Rml::Input::KeyIdentifier rml_key = QRmlUI::TranslateKey(key);
    int modifiers = QRmlUI::GetKeyModifiers();

    bool consumed = false;
    if (pressed) {
        consumed = g_state.context->ProcessKeyDown(rml_key, modifiers);
    } else {
        consumed = g_state.context->ProcessKeyUp(rml_key, modifiers);
    }

    return consumed ? 1 : 0;
}

int UI_CharEvent(unsigned int codepoint)
{
    if (!g_state.initialized || !g_state.context) return 0;
    if (!g_state.visible && g_state.menu_stack.empty()) return 0;

    bool consumed = g_state.context->ProcessTextInput(static_cast<Rml::Character>(codepoint));
    return consumed ? 1 : 0;
}

int UI_MouseMove(int x, int y, int dx, int dy)
{
    // Scale logical (point) coords to physical pixels for HiDPI
    int px = static_cast<int>(x * g_state.pixel_ratio);
    int py = static_cast<int>(y * g_state.pixel_ratio);

    // Store position for hit testing debug
    g_state.last_mouse_x = px;
    g_state.last_mouse_y = py;

    if (!g_state.initialized || !g_state.context) return 0;
    if (!g_state.visible && g_state.menu_stack.empty()) return 0;

    int modifiers = QRmlUI::GetKeyModifiers();
    bool consumed = g_state.context->ProcessMouseMove(px, py, modifiers);
    return consumed ? 1 : 0;
}

int UI_MouseButton(int button, int pressed)
{
    if (!g_state.initialized || !g_state.context) return 0;
    if (!g_state.visible && g_state.menu_stack.empty()) return 0;

    int rml_button = 0;
    switch (button) {
        case SDL_BUTTON_LEFT:   rml_button = 0; break;
        case SDL_BUTTON_RIGHT:  rml_button = 1; break;
        case SDL_BUTTON_MIDDLE: rml_button = 2; break;
        default: return 0;
    }

    int modifiers = QRmlUI::GetKeyModifiers();
    bool consumed = false;

    if (pressed) {
        consumed = g_state.context->ProcessMouseButtonDown(rml_button, modifiers);
    } else {
        consumed = g_state.context->ProcessMouseButtonUp(rml_button, modifiers);
    }

    if (pressed) {
        Rml::Element* hover = g_state.context->GetHoverElement();
        const char* hover_tag = hover ? hover->GetTagName().c_str() : "<none>";
        const char* hover_id = hover ? hover->GetId().c_str() : "";
        Con_DPrintf("UI_MouseButton: btn=%d pressed=%d consumed=%d hover=%s id=%s\n",
                   rml_button, pressed ? 1 : 0, consumed ? 1 : 0, hover_tag, hover_id);
    }

    return consumed ? 1 : 0;
}

int UI_MouseScroll(float x, float y)
{
    if (!g_state.initialized || !g_state.context) return 0;
    if (!g_state.visible && g_state.menu_stack.empty()) return 0;

    int modifiers = QRmlUI::GetKeyModifiers();
    bool consumed = g_state.context->ProcessMouseWheel(Rml::Vector2f(x, -y), modifiers);
    return consumed ? 1 : 0;
}

// Path resolution — QuakeFileInterface handles mod-directory layering.
static std::string ResolveUIPath(const char* path)
{
    return path ? path : "";
}

int UI_LoadDocument(const char* path)
{
    if (!path) { Con_Printf("WARNING: UI_LoadDocument: null path\n"); return 0; }
    if (!g_state.initialized || !g_state.context) return 0;

    // Check if already loaded (use original path as key)
    auto it = g_state.documents.find(path);
    if (it != g_state.documents.end() && it->second) {
        return 1;  // Already loaded
    }

    // Resolve the path relative to UI base directory
    std::string resolved_path = ResolveUIPath(path);

    Rml::ElementDocument* doc = g_state.context->LoadDocument(resolved_path);
    if (!doc) {
        Con_Printf("UI_LoadDocument: Failed to load '%s' (resolved: '%s')\n", path, resolved_path.c_str());
        return 0;
    }

    // Store with original path as key for consistency
    g_state.documents[path] = doc;
    QRmlUI::MenuEventHandler::RegisterWithDocument(doc);
    Con_DPrintf("UI_LoadDocument: Loaded '%s'\n", path);
    return 1;
}

void UI_UnloadDocument(const char* path)
{
    if (!path) { Con_Printf("WARNING: UI_UnloadDocument: null path\n"); return; }
    if (!g_state.initialized || !g_state.context) return;

    auto it = g_state.documents.find(path);
    if (it != g_state.documents.end() && it->second) {
        if (g_state.pending_menu_enter == it->second)
            g_state.pending_menu_enter = nullptr;
        it->second->Close();
        g_state.documents.erase(it);
        Con_DPrintf("UI_UnloadDocument: Unloaded '%s'\n", path);
    }
}

void UI_ShowDocument(const char* path, int modal)
{
    if (!path) { Con_Printf("WARNING: UI_ShowDocument: null path\n"); return; }
    if (!g_state.initialized || !g_state.context) return;

    auto it = g_state.documents.find(path);
    if (it != g_state.documents.end() && it->second) {
        if (modal) {
            it->second->Show(Rml::ModalFlag::Modal);
        } else {
            it->second->Show();
        }
    }
}

void UI_HideDocument(const char* path)
{
    if (!path) { Con_Printf("WARNING: UI_HideDocument: null path\n"); return; }
    if (!g_state.initialized || !g_state.context) return;

    auto it = g_state.documents.find(path);
    if (it != g_state.documents.end() && it->second) {
        it->second->Hide();
    }
}

void UI_SetVisible(int visible)
{
    g_state.visible = visible != 0;
}

int UI_IsVisible(void)
{
    return g_state.visible ? 1 : 0;
}

int UI_IsMenuVisible(void)
{
    return HasVisibleMenuDocument() ? 1 : 0;
}

void UI_Toggle(void)
{
    g_state.visible = !g_state.visible;
}


void UI_ToggleDebugger(void)
{
    if (!g_state.initialized || !g_state.context) return;
    Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
}

void UI_ReloadDocuments(void)
{
#ifdef QRMLUI_HOT_RELOAD
    if (!g_state.initialized || !g_state.context) return;

    Con_DPrintf("UI_ReloadDocuments: Reloading all documents\n");

    // Clear caches so RmlUI re-reads files from disk
    Rml::Factory::ClearStyleSheetCache();
    Rml::Factory::ClearTemplateCache();

    // Invalidate deferred pointer — documents are about to be replaced.
    g_state.pending_menu_enter = nullptr;

    // Store visibility state and reload each document
    for (auto& pair : g_state.documents) {
        if (pair.second) {
            bool was_visible = pair.second->IsVisible();
            std::string path = pair.first;

            pair.second->Close();
            pair.second = g_state.context->LoadDocument(path);

            if (pair.second) {
                QRmlUI::MenuEventHandler::RegisterWithDocument(pair.second);
                if (was_visible) {
                    pair.second->Show();
                }
            }
        }
    }

    Con_DPrintf("UI_ReloadDocuments: Done\n");
#else
    Con_DPrintf("UI_ReloadDocuments: Hot reload not enabled\n");
#endif
}

void UI_ReloadStyleSheets(void)
{
#ifdef QRMLUI_HOT_RELOAD
    if (!g_state.initialized || !g_state.context) return;

    Con_DPrintf("UI_ReloadStyleSheets: Reloading stylesheets\n");

    for (auto& pair : g_state.documents) {
        if (pair.second) {
            pair.second->ReloadStyleSheet();
        }
    }

    Con_DPrintf("UI_ReloadStyleSheets: Done\n");
#else
    Con_DPrintf("UI_ReloadStyleSheets: Hot reload not enabled\n");
#endif
}

// Track if we've done the initial asset load

// Helper to initialize render interface with vkQuake's Vulkan context
// Called from vkQuake after Vulkan is initialized (or reinitialized)
void UI_InitializeVulkan(const void* config)
{
    if (g_state.render_interface && config) {
        const QRmlUI::VulkanConfig* vk_config = static_cast<const QRmlUI::VulkanConfig*>(config);

        // If already initialized, just reinitialize with new render pass
        // This preserves geometry and textures
        if (g_state.render_interface->IsInitialized()) {
            if (g_state.render_interface->Reinitialize(*vk_config)) {
                Con_DPrintf("UI_InitializeVulkan: Vulkan renderer reinitialized\n");
            } else {
                Con_Printf("UI_InitializeVulkan: ERROR - Failed to reinitialize Vulkan renderer\n");
            }
            return;
        }

        // First-time initialization
        if (g_state.render_interface->Initialize(*vk_config)) {
            Con_DPrintf("UI_InitializeVulkan: Vulkan renderer initialized\n");

            // Only load assets on first initialization
            if (!g_state.assets_loaded) {
                UI_LoadAssets();

                // Initialize data models now that context is ready
                if (g_state.context) {
                    QRmlUI::GameDataModel::Initialize(g_state.context);
                    QRmlUI::CvarBindingManager::Initialize(g_state.context);
                    QRmlUI::MenuEventHandler::Initialize(g_state.context);
                }
                g_state.assets_loaded = true;
            }
        } else {
            Con_Printf("UI_InitializeVulkan: ERROR - Failed to initialize Vulkan renderer\n");
        }
    }
}

// Called each frame before UI rendering
void UI_BeginFrame(void* cmd, int width, int height)
{
    if (g_state.render_interface) {
        g_state.render_interface->BeginFrame(static_cast<VkCommandBuffer>(cmd), width, height);
    }
}

// Called after UI rendering
void UI_EndFrame(void)
{
    if (g_state.render_interface) {
        g_state.render_interface->EndFrame();
    }
}

// Garbage collection - call after GPU fence wait
void UI_CollectGarbage(void)
{
    if (g_state.render_interface) {
        g_state.render_interface->CollectGarbage();
    }
}

// Input mode control
void UI_SetInputMode(ui_input_mode_t mode)
{
    ui_input_mode_t old_mode = g_state.input_mode;
    g_state.input_mode = mode;

    // Automatically manage visibility based on mode
    if (mode == UI_INPUT_MENU_ACTIVE || mode == UI_INPUT_OVERLAY) {
        g_state.visible = true;
    } else if (mode == UI_INPUT_INACTIVE && old_mode == UI_INPUT_MENU_ACTIVE) {
        // When exiting menu mode, hide UI unless we have HUD elements
        // For now, just hide - future: check if HUD documents are loaded
        g_state.visible = false;
        // Note: Unlike native menus, we don't need to restore the demo loop here
        // because RmlUI menus don't disable it in the first place. Demos continue
        // cycling in the background while menus are displayed.
    }

    Con_DPrintf("UI_SetInputMode: %s -> %s\n",
        old_mode == UI_INPUT_INACTIVE ? "INACTIVE" :
        old_mode == UI_INPUT_MENU_ACTIVE ? "MENU_ACTIVE" : "OVERLAY",
        mode == UI_INPUT_INACTIVE ? "INACTIVE" :
        mode == UI_INPUT_MENU_ACTIVE ? "MENU_ACTIVE" : "OVERLAY");
}

ui_input_mode_t UI_GetInputMode(void)
{
    return g_state.input_mode;
}

int UI_WantsMenuInput(void)
{
    if (!g_state.initialized || !g_state.context) {
        return 0;
    }

    // Menu stack is the authoritative source of truth.  Check it first so
    // that UI_CloseAllMenusImmediate() (which clears the stack and sets
    // INACTIVE) takes effect immediately, even before RmlUI document
    // visibility properties have fully propagated.
    if (g_state.input_mode == UI_INPUT_MENU_ACTIVE || !g_state.menu_stack.empty()) {
        return 1;
    }

    return 0;
}

int UI_WantsInput(void)
{
    return UI_WantsMenuInput() || UI_IsMenuVisible();
}

void UI_HandleEscape(void)
{
    if (!g_state.initialized || !g_state.context) return;

    // Defer the actual escape handling to the next UI_Update call.
    // This prevents race conditions between input handling (main thread)
    // and rendering (worker threads). The state change will happen
    // during UI_Update, before rendering is scheduled.
    g_state.pending_escape = true;
}

void UI_CloseAllMenus(void)
{
    if (!g_state.initialized || !g_state.context) return;

    // Defer closing all menus to the next UI_Update call.
    // This prevents race conditions with rendering.
    g_state.pending_close_all = true;
}

// Immediately close all menus - for internal use when we're already in the update phase
// (e.g., from RmlUI event handlers). Do not call from external code.
// Uses Hide() instead of Close() to avoid invalidating documents during RmlUI event dispatch.
void UI_CloseAllMenusImmediate(void)
{
    if (!g_state.initialized || !g_state.context) return;

    // Hide all menu documents (safe during event dispatch — doesn't invalidate elements)
    for (const auto& path : g_state.menu_stack) {
        auto it = g_state.documents.find(path);
        if (it != g_state.documents.end() && it->second) {
            it->second->SetClass("menu-enter", false);
            it->second->SetClass("startup-enter", false);
            it->second->Hide();
        }
    }
    g_state.menu_stack.clear();

    // Transition to inactive and restore game input
    UI_SetInputMode(UI_INPUT_INACTIVE);
    IN_Activate();
    key_dest = key_game;
}

void UI_PushMenu(const char* path)
{
    if (!g_state.initialized || !g_state.context || !path) return;

    // Cancel any pending close requests since we're explicitly opening a menu.
    g_state.pending_escape = false;
    g_state.pending_close_all = false;

    // Sync cvar backing stores with current engine values BEFORE loading or
    // showing the document.  LoadDocument() creates elements that read the data
    // model immediately, and Show() on an already-loaded document relies on the
    // dirty-data mechanism to refresh elements on the next Update().
    if (QRmlUI::CvarBindingManager::IsInitialized()) {
        QRmlUI::CvarBindingManager::SyncToUI();
    }

    // Load document if not already loaded
    auto it = g_state.documents.find(path);
    if (it == g_state.documents.end() || !it->second) {
        // Resolve the path relative to UI base directory
        std::string resolved_path = ResolveUIPath(path);

        Rml::ElementDocument* doc = g_state.context->LoadDocument(resolved_path);
        if (!doc) {
            Con_Printf("UI_PushMenu: Failed to load '%s' (resolved: '%s')\n", path, resolved_path.c_str());
            return;
        }
        // Store with original path as key for consistency
        g_state.documents[path] = doc;
        QRmlUI::MenuEventHandler::RegisterWithDocument(doc);
    }

    // Hide current menu if there is one (optional - could layer them)
    if (!g_state.menu_stack.empty()) {
        std::string& current = g_state.menu_stack.back();
        auto current_it = g_state.documents.find(current);
        if (current_it != g_state.documents.end() && current_it->second) {
            current_it->second->SetClass("menu-enter", false);
            current_it->second->SetClass("startup-enter", false);
            current_it->second->Hide();
        }
    }

    // Push new menu onto stack and show it
    g_state.menu_stack.push_back(path);
    Rml::ElementDocument* doc = g_state.documents[path];
    doc->SetClass("menu-enter", false);
    doc->SetClass("startup-enter", false);
    doc->Show();
    g_state.pending_menu_enter = doc;
    g_state.pending_menu_enter_frames = MENU_ENTER_DELAY_FRAMES;

    // Set menu mode
    UI_SetInputMode(UI_INPUT_MENU_ACTIVE);

    // Ensure input is routed to menu when a menu is pushed.
    if (key_dest != key_menu) {
        IN_Deactivate(true);
        key_dest = key_menu;
    }
    IN_EndIgnoringMouseEvents();

    // Record open time to prevent immediate close from same key event
    g_state.menu_open_time = realtime;
}

void UI_SetStartupMenuEnter(void)
{
    g_state.startup_menu_enter = true;
}

void UI_PopMenu(void)
{
    // Same as HandleEscape for now - could have different behavior later
    UI_HandleEscape();
}

// ── HUD / Scoreboard / Intermission ────────────────────────────────

void UI_ShowHUD(const char* hud_document)
{
    if (!hud_document) {
        hud_document = GetHudDocumentFromStyle();
    }

    const bool hud_changed = g_state.current_hud.empty() || g_state.current_hud != hud_document;
    if (!hud_changed && g_state.hud_visible) {
        UI_SetInputMode(UI_INPUT_OVERLAY);
        return;
    }

    // Hide previous HUD if different
    if (!g_state.current_hud.empty() && hud_changed && g_state.hud_visible) {
        UI_HideDocument(g_state.current_hud.c_str());
    }

    // Load and show new HUD
    if (UI_LoadDocument(hud_document)) {
        UI_ShowDocument(hud_document, 0);
        g_state.current_hud = hud_document;
        g_state.hud_visible = true;
        UI_SetInputMode(UI_INPUT_OVERLAY);
    }

    // Reset intermission tracking on new game/map
    g_state.last_intermission = 0;
}

void UI_HideHUD(void)
{
    if (!g_state.current_hud.empty() && g_state.hud_visible) {
        UI_HideDocument(g_state.current_hud.c_str());
        g_state.hud_visible = false;
    }
    if (g_state.intermission_visible) {
        UI_HideDocument(QRmlUI::Paths::kIntermission);
        g_state.intermission_visible = false;
    }
    if (g_state.scoreboard_visible) {
        UI_HideDocument(QRmlUI::Paths::kScoreboard);
        g_state.scoreboard_visible = false;
    }
    g_state.last_intermission = 0;

    if (!UI_WantsMenuInput()) {
        UI_SetInputMode(UI_INPUT_INACTIVE);
    }
}

int UI_IsHUDVisible(void)
{
    return g_state.hud_visible ? 1 : 0;
}

void UI_ShowScoreboard(void)
{
    if (UI_LoadDocument(QRmlUI::Paths::kScoreboard)) {
        UI_ShowDocument(QRmlUI::Paths::kScoreboard, 0);
        g_state.scoreboard_visible = true;
    }
}

void UI_HideScoreboard(void)
{
    if (g_state.scoreboard_visible) {
        UI_HideDocument(QRmlUI::Paths::kScoreboard);
        g_state.scoreboard_visible = false;
    }
}

void UI_ShowIntermission(void)
{
    if (UI_LoadDocument(QRmlUI::Paths::kIntermission)) {
        UI_ShowDocument(QRmlUI::Paths::kIntermission, 0);
        g_state.intermission_visible = true;
    }
}

void UI_HideIntermission(void)
{
    if (g_state.intermission_visible) {
        UI_HideDocument(QRmlUI::Paths::kIntermission);
        g_state.intermission_visible = false;
    }
}

// ── Game state synchronization ─────────────────────────────────────

void UI_SyncGameState(const int* stats, int stats_count, int items,
                      int intermission, int gametype,
                      int maxclients,
                      const char* level_name, const char* map_name,
                      double game_time)
{
    // Ensure OVERLAY mode if HUD is visible and no menu is open
    if (g_state.hud_visible && !UI_WantsMenuInput()) {
        if (UI_GetInputMode() == UI_INPUT_INACTIVE) {
            UI_SetInputMode(UI_INPUT_OVERLAY);
        }

        // Hot-swap HUD document when style cvar changes while in-game.
        const char* desired_hud = GetHudDocumentFromStyle();
        if (g_state.current_hud.empty() || g_state.current_hud != desired_hud) {
            UI_ShowHUD(desired_hud);
        }
    }

    // Detect intermission state changes
    if (intermission != g_state.last_intermission) {
        if (intermission > 0 && g_state.last_intermission == 0) {
            UI_ShowIntermission();
        } else if (intermission == 0 && g_state.last_intermission > 0) {
            UI_HideIntermission();
        }
        g_state.last_intermission = intermission;
    }

    GameDataModel_SyncFromQuake(stats, stats_count, items, intermission, gametype,
                                maxclients, level_name, map_name, game_time);
}

// ── Scoreboard sync ────────────────────────────────────────────────

void UI_SyncScoreboard(const ui_player_info_t* players, int count)
{
    if (!g_state.initialized) return;

    QRmlUI::g_game_state.players.clear();
    QRmlUI::g_game_state.players.reserve(count);

    for (int i = 0; i < count; i++) {
        QRmlUI::PlayerInfo pi;
        pi.name = players[i].name ? players[i].name : "";
        pi.frags = players[i].frags;
        pi.top_color = (players[i].colors >> 4) & 0xf;
        pi.bottom_color = players[i].colors & 0xf;
        pi.ping = players[i].ping;
        pi.is_local = players[i].is_local != 0;
        QRmlUI::g_game_state.players.push_back(pi);
    }
    QRmlUI::g_game_state.num_players = count;
}

// ── Notification system ────────────────────────────────────────────

void UI_NotifyCenterPrint(const char* text)
{
    if (!g_state.initialized || !text) return;
    QRmlUI::NotificationModel::CenterPrint(text, realtime);
}

void UI_NotifyPrint(const char* text)
{
    if (!g_state.initialized || !text) return;
    QRmlUI::NotificationModel::NotifyPrint(text, realtime);
}

// ── Save slot sync ─────────────────────────────────────────────────

void UI_SyncSaveSlots(const ui_save_slot_t* slots, int count)
{
    if (!g_state.initialized) return;

    QRmlUI::g_game_state.save_slots.clear();
    QRmlUI::g_game_state.save_slots.reserve(count);

    for (int i = 0; i < count; i++) {
        QRmlUI::SaveSlotInfo info;
        info.slot_id = slots[i].slot_id ? slots[i].slot_id : "";
        info.description = slots[i].description ? slots[i].description : "";
        info.slot_number = slots[i].slot_number;
        info.is_loadable = slots[i].is_loadable != 0;
        QRmlUI::g_game_state.save_slots.push_back(std::move(info));
    }

    QRmlUI::GameDataModel::MarkAllDirty();
}

void UI_SyncMods(const ui_mod_info_t* mods, int count)
{
    if (!g_state.initialized) return;

    QRmlUI::g_game_state.mods.clear();
    QRmlUI::g_game_state.mods.reserve(count);

    for (int i = 0; i < count; i++) {
        QRmlUI::ModInfo info;
        info.name = mods[i].name ? mods[i].name : "";
        info.display_name = mods[i].display_name ? mods[i].display_name : info.name;
        QRmlUI::g_game_state.mods.push_back(std::move(info));
    }

    QRmlUI::g_game_state.num_mods = count;
    QRmlUI::GameDataModel::MarkAllDirty();
}

// ── Video mode sync ────────────────────────────────────────────────

void UI_SyncVideoModes(const ui_video_mode_t* modes, int count)
{
    if (!g_state.initialized) return;

    QRmlUI::CvarBindingManager::SyncVideoModes(modes, count);
}

// ── Key capture ────────────────────────────────────────────────────

int UI_IsCapturingKey(void)
{
    return MenuEventHandler_IsCapturingKey();
}

void UI_OnKeyCaptured(int key, const char* key_name)
{
    if (!key_name) { Con_Printf("WARNING: UI_OnKeyCaptured: null key_name\n"); return; }
    MenuEventHandler_OnKeyCaptured(key, key_name);
}

void UI_CancelKeyCapture(void)
{
    QRmlUI::MenuEventHandler::CancelKeyCapture();
}

} // extern "C"
