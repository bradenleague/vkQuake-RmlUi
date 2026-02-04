/*
 * RmlUI Bridge - Implementation
 *
 * C wrapper around the Tatoosh UI manager for vkQuake integration.
 */

#include "rmlui_bridge.h"
#include "../../rmlui/interface/ui_manager.h"
#include "../../rmlui/infrastructure/render_interface_vk.h"
#include "../../rmlui/infrastructure/game_data_model.h"
#include "../../rmlui/infrastructure/cvar_binding.h"
#include "../../rmlui/infrastructure/menu_event_handler.h"

#include <vector>

// State for HUD documents
static const char* g_current_hud = nullptr;
static bool g_hud_visible = false;
static bool g_scoreboard_visible = false;
static bool g_intermission_visible = false;
static int g_last_intermission = 0;
static bool g_cvars_registered = false;

extern "C" {

void RmlUI_RegisterDefaultCvars(void);

int RmlUI_Init(int width, int height, const char *base_path)
{
    return UI_Init(width, height, base_path);
}

void RmlUI_InitVulkan(const rmlui_vulkan_config_t *config)
{
    if (!config) return;

    // Convert C struct to C++ struct
    Tatoosh::VulkanConfig vk_config{};
    vk_config.device = config->device;
    vk_config.physical_device = config->physical_device;
    vk_config.graphics_queue = config->graphics_queue;
    vk_config.queue_family_index = config->queue_family_index;
    vk_config.color_format = config->color_format;
    vk_config.depth_format = config->depth_format;
    vk_config.sample_count = config->sample_count;
    vk_config.render_pass = config->render_pass;
    vk_config.subpass = config->subpass;
    vk_config.memory_properties = config->memory_properties;
    vk_config.cmd_bind_pipeline = config->cmd_bind_pipeline;
    vk_config.cmd_bind_descriptor_sets = config->cmd_bind_descriptor_sets;
    vk_config.cmd_bind_vertex_buffers = config->cmd_bind_vertex_buffers;
    vk_config.cmd_bind_index_buffer = config->cmd_bind_index_buffer;
    vk_config.cmd_draw = config->cmd_draw;
    vk_config.cmd_draw_indexed = config->cmd_draw_indexed;
    vk_config.cmd_push_constants = config->cmd_push_constants;
    vk_config.cmd_set_scissor = config->cmd_set_scissor;
    vk_config.cmd_set_viewport = config->cmd_set_viewport;

    UI_InitializeVulkan(&vk_config);
    RmlUI_RegisterDefaultCvars();
}

void RmlUI_Shutdown(void)
{
    UI_Shutdown();
}

void RmlUI_ProcessPending(void)
{
    UI_ProcessPending();
}

void RmlUI_Update(double dt)
{
    UI_Update(dt);
}

void RmlUI_BeginFrame(VkCommandBuffer cmd, int width, int height)
{
    UI_BeginFrame(cmd, width, height);
}

void RmlUI_Render(void)
{
    UI_Render();
}

void RmlUI_EndFrame(void)
{
    UI_EndFrame();
}

void RmlUI_CollectGarbage(void)
{
    UI_CollectGarbage();
}

void RmlUI_Resize(int width, int height)
{
    UI_Resize(width, height);
}

int RmlUI_KeyEvent(int key, int scancode, int pressed, int repeat)
{
    return UI_KeyEvent(key, scancode, pressed, repeat);
}

int RmlUI_CharEvent(unsigned int codepoint)
{
    return UI_CharEvent(codepoint);
}

int RmlUI_MouseMove(int x, int y, int dx, int dy)
{
    return UI_MouseMove(x, y, dx, dy);
}

int RmlUI_MouseButton(int button, int pressed)
{
    return UI_MouseButton(button, pressed);
}

int RmlUI_MouseScroll(float x, float y)
{
    return UI_MouseScroll(x, y);
}

int RmlUI_LoadDocument(const char *path)
{
    return UI_LoadDocument(path);
}

void RmlUI_UnloadDocument(const char *path)
{
    UI_UnloadDocument(path);
}

void RmlUI_ShowDocument(const char *path, int modal)
{
    UI_ShowDocument(path, modal);
}

void RmlUI_HideDocument(const char *path)
{
    UI_HideDocument(path);
}

void RmlUI_SetVisible(int visible)
{
    UI_SetVisible(visible);
}

int RmlUI_IsVisible(void)
{
    return UI_IsVisible();
}

void RmlUI_Toggle(void)
{
    UI_Toggle();
}

void RmlUI_ToggleDebugger(void)
{
    UI_ToggleDebugger();
}

void RmlUI_SetInputMode(rmlui_input_mode_t mode)
{
    // Map rmlui_input_mode_t to ui_input_mode_t (they have matching values)
    UI_SetInputMode(static_cast<ui_input_mode_t>(mode));
}

rmlui_input_mode_t RmlUI_GetInputMode(void)
{
    return static_cast<rmlui_input_mode_t>(UI_GetInputMode());
}

int RmlUI_WantsMenuInput(void)
{
    return UI_WantsMenuInput();
}

void RmlUI_HandleEscape(void)
{
    UI_HandleEscape();
}

void RmlUI_PushMenu(const char* path)
{
    UI_PushMenu(path);
}

void RmlUI_PopMenu(void)
{
    UI_PopMenu();
}

/* =========================================
 * Game Data Model API
 * ========================================= */

void RmlUI_SyncGameState(const int* stats, int items,
                         int intermission, int gametype,
                         const char* level_name, const char* map_name,
                         double game_time)
{
    // Ensure we're in OVERLAY mode if HUD is visible and no menu is open
    // This handles the case where menus were closed and mode was set to INACTIVE
    if (g_hud_visible && !UI_WantsMenuInput()) {
        ui_input_mode_t current_mode = UI_GetInputMode();
        if (current_mode == UI_INPUT_INACTIVE) {
            UI_SetInputMode(UI_INPUT_OVERLAY);
        }
    }

    // Detect intermission state changes
    if (intermission != g_last_intermission) {
        if (intermission > 0 && g_last_intermission == 0) {
            RmlUI_ShowIntermission();
        } else if (intermission == 0 && g_last_intermission > 0) {
            RmlUI_HideIntermission();
        }
        g_last_intermission = intermission;
    }

    GameDataModel_SyncFromQuake(stats, items, intermission, gametype,
                                 level_name, map_name, game_time);
}

void RmlUI_RegisterDefaultCvars(void)
{
    if (g_cvars_registered) {
        return;
    }

    if (!Tatoosh::CvarBindingManager::IsInitialized()) {
        return;
    }

    g_cvars_registered = true;

    using Tatoosh::CvarBindingManager;

    // Graphics options
    CvarBindingManager::RegisterEnumValues("vid_fullscreen", "fullscreen", {0, 1});
    CvarBindingManager::RegisterEnumValues("vid_vsync", "vsync", {0, 1});
    CvarBindingManager::RegisterFloat("host_maxfps", "max_fps", 30.0f, 1000.0f, 10.0f);
    CvarBindingManager::RegisterFloat("fov", "fov", 50.0f, 130.0f, 5.0f);
    CvarBindingManager::RegisterFloat("gamma", "gamma", 0.5f, 2.0f, 0.05f);
    CvarBindingManager::RegisterFloat("contrast", "contrast", 0.5f, 2.0f, 0.05f);
    CvarBindingManager::RegisterEnumValues("vid_palettize", "palettize", {0, 1});
    CvarBindingManager::RegisterEnumValues("gl_picmip", "texture_quality", {0, 1, 2, 3});
    CvarBindingManager::RegisterEnumValues("vid_filter", "texture_filter", {0, 1});
    CvarBindingManager::RegisterEnumValues("vid_anisotropic", "aniso", {0, 1});
    CvarBindingManager::RegisterEnumValues("vid_fsaa", "msaa", {0, 2, 4, 8, 16});
    CvarBindingManager::RegisterEnumValues("vid_fsaamode", "aa_mode", {0, 1});
    CvarBindingManager::RegisterEnumValues("r_scale", "render_scale", {0, 2, 4, 8});
    CvarBindingManager::RegisterEnumValues("r_particles", "particles", {0, 2, 1});
    CvarBindingManager::RegisterEnumValues("r_dynamic", "dynamic_lights", {0, 1});
    CvarBindingManager::RegisterEnumValues("r_rtshadows", "shadows", {0, 1});
    CvarBindingManager::RegisterEnumValues("r_waterwarp", "underwater_fx", {0, 1, 2});
    CvarBindingManager::RegisterEnumValues("r_enhancedmodels", "enhanced_models", {0, 1});
    CvarBindingManager::RegisterEnumValues("r_lerpmodels", "model_interpolation", {0, 1});

    // Game options
    CvarBindingManager::RegisterFloat("scr_relativescale", "ui_scale", 1.0f, 3.0f, 0.1f);
    CvarBindingManager::RegisterFloat("scr_sbaralpha", "hud_opacity", 0.0f, 1.0f, 0.1f);
    CvarBindingManager::RegisterEnumValues("crosshair_def", "crosshair", {0, 1, 2, 3, 4, 5});
    CvarBindingManager::RegisterEnumValues("scr_style", "hud_style", {0, 1, 2});
    CvarBindingManager::RegisterEnumValues("viewsize", "hud_detail", {100, 110, 120});
    CvarBindingManager::RegisterEnumValues("scr_showfps", "show_fps", {0, 1});
    CvarBindingManager::RegisterFloat("sensitivity", "sensitivity", 1.0f, 20.0f, 0.5f);
    CvarBindingManager::RegisterBool("invert_mouse", "invert_mouse");
    CvarBindingManager::RegisterBool("m_filter", "m_filter");
    CvarBindingManager::RegisterEnumValues("cl_alwaysrun", "always_run", {0, 1});
    CvarBindingManager::RegisterEnumValues("sv_aim", "sv_aim", {0, 1});
    CvarBindingManager::RegisterFloat("cl_bob", "view_bob", 0.0f, 0.04f, 0.004f);
    CvarBindingManager::RegisterFloat("cl_rollangle", "view_roll", 0.0f, 4.0f, 0.5f);
    CvarBindingManager::RegisterEnumValues("v_gunkick", "gun_kick", {0, 1, 2});
    CvarBindingManager::RegisterEnumValues("r_drawviewmodel", "show_gun", {0, 1});
    CvarBindingManager::RegisterEnumValues("autofastload", "fast_loading", {0, 1});
    CvarBindingManager::RegisterEnumValues("autoload", "auto_load", {0, 1});
    CvarBindingManager::RegisterEnumValues("cl_startdemos", "startup_demos", {0, 1});
    CvarBindingManager::RegisterEnumValues("skill", "skill", {0, 1, 2, 3});

    // Sound options
    CvarBindingManager::RegisterFloat("volume", "volume", 0.0f, 1.0f, 0.1f);
    CvarBindingManager::RegisterFloat("bgmvolume", "bgmvolume", 0.0f, 1.0f, 0.1f);
    CvarBindingManager::RegisterFloat("volume", "sfxvolume", 0.0f, 1.0f, 0.1f);
    CvarBindingManager::RegisterEnumValues("snd_filterquality", "sound_quality", {1, 2, 3, 4, 5});
    CvarBindingManager::RegisterEnumValues("snd_waterfx", "ambient", {0, 1});
}

/* =========================================
 * Cvar Binding API
 * ========================================= */

void RmlUI_RegisterCvarFloat(const char* cvar, const char* ui_name,
                              float min, float max, float step)
{
    CvarBinding_RegisterFloat(cvar, ui_name, min, max, step);
}

void RmlUI_RegisterCvarBool(const char* cvar, const char* ui_name)
{
    CvarBinding_RegisterBool(cvar, ui_name);
}

void RmlUI_RegisterCvarInt(const char* cvar, const char* ui_name, int min, int max)
{
    CvarBinding_RegisterInt(cvar, ui_name, min, max);
}

void RmlUI_RegisterCvarEnum(const char* cvar, const char* ui_name, int num_values)
{
    CvarBinding_RegisterEnum(cvar, ui_name, num_values);
}

void RmlUI_RegisterCvarString(const char* cvar, const char* ui_name)
{
    CvarBinding_RegisterString(cvar, ui_name);
}

void RmlUI_SyncCvarsToUI(void)
{
    CvarBinding_SyncToUI();
}

void RmlUI_SyncCvarFromUI(const char* ui_name)
{
    CvarBinding_SyncFromUI(ui_name);
}

void RmlUI_CycleCvar(const char* ui_name, int delta)
{
    CvarBinding_CycleEnum(ui_name, delta);
}

/* =========================================
 * Menu Event Handler API
 * ========================================= */

void RmlUI_ProcessMenuAction(const char* action)
{
    MenuEventHandler_ProcessAction(action);
}

int RmlUI_IsCapturingKey(void)
{
    return MenuEventHandler_IsCapturingKey();
}

void RmlUI_OnKeyCaptured(int key, const char* key_name)
{
    MenuEventHandler_OnKeyCaptured(key, key_name);
}

/* =========================================
 * HUD Control
 * ========================================= */

void RmlUI_ShowHUD(const char* hud_document)
{
    if (!hud_document) {
        hud_document = "ui/rml/hud/hud_classic.rml";
    }

    // Hide previous HUD if different
    if (g_current_hud && g_current_hud != hud_document && g_hud_visible) {
        UI_HideDocument(g_current_hud);
    }

    // Load and show new HUD
    if (UI_LoadDocument(hud_document)) {
        UI_ShowDocument(hud_document, 0);
        g_current_hud = hud_document;
        g_hud_visible = true;

        // Set OVERLAY mode for HUD
        UI_SetInputMode(UI_INPUT_OVERLAY);
    }

    // Reset intermission tracking on new game/map
    g_last_intermission = 0;
}

void RmlUI_HideHUD(void)
{
    // Hide all HUD-related documents (main HUD, intermission, scoreboard)
    if (g_current_hud && g_hud_visible) {
        UI_HideDocument(g_current_hud);
        g_hud_visible = false;
    }

    // Also hide intermission and scoreboard screens
    if (g_intermission_visible) {
        const char* intermission_path = "ui/rml/hud/intermission.rml";
        UI_HideDocument(intermission_path);
        g_intermission_visible = false;
    }
    if (g_scoreboard_visible) {
        const char* scoreboard_path = "ui/rml/hud/scoreboard.rml";
        UI_HideDocument(scoreboard_path);
        g_scoreboard_visible = false;
    }

    // Reset intermission tracking for next game
    g_last_intermission = 0;

    // Return to inactive if no menus open
    if (!UI_WantsMenuInput()) {
        UI_SetInputMode(UI_INPUT_INACTIVE);
    }
}

int RmlUI_IsHUDVisible(void)
{
    return g_hud_visible ? 1 : 0;
}

void RmlUI_ShowScoreboard(void)
{
    const char* scoreboard_path = "ui/rml/hud/scoreboard.rml";
    if (UI_LoadDocument(scoreboard_path)) {
        UI_ShowDocument(scoreboard_path, 0);
        g_scoreboard_visible = true;
    }
}

void RmlUI_HideScoreboard(void)
{
    if (g_scoreboard_visible) {
        const char* scoreboard_path = "ui/rml/hud/scoreboard.rml";
        UI_HideDocument(scoreboard_path);
        g_scoreboard_visible = false;
    }
}

void RmlUI_ShowIntermission(void)
{
    const char* intermission_path = "ui/rml/hud/intermission.rml";
    if (UI_LoadDocument(intermission_path)) {
        UI_ShowDocument(intermission_path, 0);
        g_intermission_visible = true;
    }
}

void RmlUI_HideIntermission(void)
{
    if (g_intermission_visible) {
        const char* intermission_path = "ui/rml/hud/intermission.rml";
        UI_HideDocument(intermission_path);
        g_intermission_visible = false;
    }
}

} /* extern "C" */
