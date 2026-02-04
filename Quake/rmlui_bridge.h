/*
 * RmlUI Bridge - C interface for vkQuake integration
 *
 * This header provides the C API to integrate RmlUI with vkQuake.
 * The implementation is in the tatoosh_ui library (C++).
 *
 * Note: This header intentionally does NOT include quakedef.h to avoid
 * conflicts between C11 atomics and C++ std::atomic when compiling C++ files.
 */

#ifndef RMLUI_BRIDGE_H
#define RMLUI_BRIDGE_H

#include <vulkan/vulkan.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Vulkan configuration for RmlUI renderer initialization */
typedef struct rmlui_vulkan_config_s {
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkQueue graphics_queue;
    uint32_t queue_family_index;
    VkFormat color_format;
    VkFormat depth_format;
    VkSampleCountFlagBits sample_count;
    VkRenderPass render_pass;
    uint32_t subpass;
    VkPhysicalDeviceMemoryProperties memory_properties;

    /* Function pointers from vkQuake's dispatch table */
    PFN_vkCmdBindPipeline cmd_bind_pipeline;
    PFN_vkCmdBindDescriptorSets cmd_bind_descriptor_sets;
    PFN_vkCmdBindVertexBuffers cmd_bind_vertex_buffers;
    PFN_vkCmdBindIndexBuffer cmd_bind_index_buffer;
    PFN_vkCmdDraw cmd_draw;
    PFN_vkCmdDrawIndexed cmd_draw_indexed;
    PFN_vkCmdPushConstants cmd_push_constants;
    PFN_vkCmdSetScissor cmd_set_scissor;
    PFN_vkCmdSetViewport cmd_set_viewport;
} rmlui_vulkan_config_t;

/* Initialize the RmlUI subsystem. Call from Host_Init() after VID_Init() */
int RmlUI_Init(int width, int height, const char *base_path);

/* Initialize RmlUI's Vulkan renderer. Call after Vulkan device is ready. */
void RmlUI_InitVulkan(const rmlui_vulkan_config_t *config);

/* Shutdown and cleanup. Call from Host_Shutdown() before VID_Shutdown() */
void RmlUI_Shutdown(void);

/* Process pending UI operations. Call from main thread before rendering tasks. */
void RmlUI_ProcessPending(void);

/* Update UI state. Call each frame before rendering */
void RmlUI_Update(double dt);

/* Begin frame rendering - sets up viewport and state */
void RmlUI_BeginFrame(VkCommandBuffer cmd, int width, int height);

/* Render the UI. Call from SCR_DrawGUI() */
void RmlUI_Render(void);

/* End frame rendering */
void RmlUI_EndFrame(void);

/* Garbage collection - call after GPU fence wait to safely destroy released resources */
void RmlUI_CollectGarbage(void);

/* Handle window resize */
void RmlUI_Resize(int width, int height);

/* Input event handling - returns 1 if event was consumed by UI */
int RmlUI_KeyEvent(int key, int scancode, int pressed, int repeat);
int RmlUI_CharEvent(unsigned int codepoint);
int RmlUI_MouseMove(int x, int y, int dx, int dy);
int RmlUI_MouseButton(int button, int pressed);
int RmlUI_MouseScroll(float x, float y);

/* Document management */
int RmlUI_LoadDocument(const char *path);
void RmlUI_UnloadDocument(const char *path);
void RmlUI_ShowDocument(const char *path, int modal);
void RmlUI_HideDocument(const char *path);

/* Visibility control */
void RmlUI_SetVisible(int visible);
int RmlUI_IsVisible(void);
void RmlUI_Toggle(void);

/* Debug overlay toggle */
void RmlUI_ToggleDebugger(void);

/* Input mode - controls how RmlUI interacts with vkQuake's input system */
typedef enum {
    RMLUI_INACTIVE,      /* Not handling input - game/Quake menu works normally */
    RMLUI_MENU_ACTIVE,   /* Menu captures all input (except escape handled specially) */
    RMLUI_OVERLAY,       /* HUD mode - visible but passes input through to game */
    RMLUI_KEY_CAPTURE    /* Waiting for key press to bind (for key binding menu) */
} rmlui_input_mode_t;

/* Input mode control */
void RmlUI_SetInputMode(rmlui_input_mode_t mode);
rmlui_input_mode_t RmlUI_GetInputMode(void);
int RmlUI_WantsMenuInput(void);   /* Returns 1 if MENU_ACTIVE */
void RmlUI_HandleEscape(void);    /* Close current menu or deactivate */
void RmlUI_PushMenu(const char* path);  /* Open menu, set MENU_ACTIVE */
void RmlUI_PopMenu(void);         /* Pop current menu from stack */

/* =========================================
 * Game Data Model API
 * Sync game state to RmlUI for HUD display
 * ========================================= */

/* Sync Quake game state to RmlUI data model. Call each frame when HUD is visible.
 * stats: pointer to cl.stats[] array (MAX_CL_STATS ints)
 * items: cl.items bitfield
 * intermission: cl.intermission value
 * gametype: cl.gametype value (0=sp/coop, 1+=deathmatch)
 * level_name: cl.levelname
 * map_name: cl.mapname
 * game_time: cl.time
 */
void RmlUI_SyncGameState(const int* stats, int items,
                         int intermission, int gametype,
                         const char* level_name, const char* map_name,
                         double game_time);

/* =========================================
 * Cvar Binding API
 * Two-way sync between UI elements and cvars
 * ========================================= */

/* Register cvars for UI binding. Call during initialization. */
void RmlUI_RegisterCvarFloat(const char* cvar, const char* ui_name,
                              float min, float max, float step);
void RmlUI_RegisterCvarBool(const char* cvar, const char* ui_name);
void RmlUI_RegisterCvarInt(const char* cvar, const char* ui_name, int min, int max);
void RmlUI_RegisterCvarEnum(const char* cvar, const char* ui_name, int num_values);
void RmlUI_RegisterCvarString(const char* cvar, const char* ui_name);

/* Sync all cvars to UI (call when opening options menu) */
void RmlUI_SyncCvarsToUI(void);

/* Sync a specific cvar from UI (call on slider/checkbox change) */
void RmlUI_SyncCvarFromUI(const char* ui_name);

/* Cycle an enum cvar value (for option rows with left/right navigation) */
void RmlUI_CycleCvar(const char* ui_name, int delta);

/* =========================================
 * Menu Event Handler API
 * ========================================= */

/* Process a menu action (navigate, command, etc.) */
void RmlUI_ProcessMenuAction(const char* action);

/* Key capture for key binding menu */
int RmlUI_IsCapturingKey(void);
void RmlUI_OnKeyCaptured(int key, const char* key_name);

/* =========================================
 * HUD Control
 * ========================================= */

/* Show/hide the HUD overlay */
void RmlUI_ShowHUD(const char* hud_document);
void RmlUI_HideHUD(void);
int RmlUI_IsHUDVisible(void);

/* Show scoreboard overlay (TAB key) */
void RmlUI_ShowScoreboard(void);
void RmlUI_HideScoreboard(void);

/* Show intermission screen */
void RmlUI_ShowIntermission(void);
void RmlUI_HideIntermission(void);

#ifdef __cplusplus
}
#endif

#endif /* RMLUI_BRIDGE_H */
