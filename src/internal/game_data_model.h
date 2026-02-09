/*
 * vkQuake RmlUI - Game Data Model
 *
 * Synchronizes Quake game state (cl.stats[], cl.items) to RmlUI data model.
 * The data model is automatically updated each frame and can be used in
 * RML documents via data binding expressions.
 *
 * Usage in RML:
 *   <body data-model="game">
 *     <div>Health: {{ health }}</div>
 *     <div data-if="has_quad">QUAD DAMAGE!</div>
 *   </body>
 */

#ifndef QRMLUI_GAME_DATA_MODEL_H
#define QRMLUI_GAME_DATA_MODEL_H

#include <RmlUi/Core.h>
#include "../types/game_state.h"

namespace QRmlUI {

// Global game state that gets synced each frame
extern GameState g_game_state;

class GameDataModel {
public:
    // Initialize the data model with the RmlUI context
    // Returns true on success
    static bool Initialize(Rml::Context* context);

    // Shutdown and cleanup
    static void Shutdown();

    // Update the data model from Quake's game state
    // Call this each frame (typically from UI_Update)
    static void Update();

    // Force a dirty check on all variables (call after level load)
    static void MarkAllDirty();

    // Check if initialized
    static bool IsInitialized();

private:
    static Rml::DataModelHandle s_model_handle;
    static bool s_initialized;
    static GameState s_prev_state;
    static bool s_first_update;
    static Rml::String s_prev_gamedir;
    static bool s_was_chatting;
};

} // namespace QRmlUI

// C API for integration with vkQuake
#ifdef __cplusplus
extern "C" {
#endif

// Initialize game data model (call after UI_Init)
int GameDataModel_Init(void);

// Shutdown game data model
void GameDataModel_Shutdown(void);

// Update game data from Quake state (call each frame)
void GameDataModel_Update(void);

// Sync from Quake's game state
// stats: pointer to cl.stats[] array (MAX_CL_STATS ints)
// items: cl.items bitfield
// gametype: cl.gametype (GAME_COOP=0, GAME_DEATHMATCH=1)
// maxclients: cl.maxclients (1=SP, >1=multiplayer)
void GameDataModel_SyncFromQuake(const int* stats, int stats_count, int items,
                                  int intermission, int gametype,
                                  int maxclients,
                                  const char* level_name, const char* map_name,
                                  double game_time);

#ifdef __cplusplus
}
#endif

#endif // QRMLUI_GAME_DATA_MODEL_H
