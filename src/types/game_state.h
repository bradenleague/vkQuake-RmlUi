/*
 * vkQuake RmlUI - Game State Domain Object
 *
 * Represents the current state of the game for UI display.
 * This is a pure data structure with no framework dependencies.
 */

#ifndef QRMLUI_DOMAIN_GAME_STATE_H
#define QRMLUI_DOMAIN_GAME_STATE_H

#include <string>
#include <vector>

namespace QRmlUI {

// Save slot info for load/save menus
struct SaveSlotInfo {
    std::string slot_id;        // "s0", "s1", ..., "s19"
    std::string description;    // From .sav file or "--- UNUSED SLOT ---"
    int slot_number = 0;        // 0-19 (for display as "Slot 1-20")
    bool is_loadable = false;
};

// Video mode info for resolution picker
struct VideoModeInfo {
    int width = 0;
    int height = 0;
    std::string label;      // "1920x1080"
    bool is_current = false;
};

// Keybind info for controls menu
struct KeybindInfo {
    std::string action;       // "+forward", "impulse 1", etc.
    std::string label;        // "Move Forward" (or section name for headers)
    std::string key_name;     // "w", "MOUSE1", "---"
    std::string section;      // "Movement", "Combat", "Weapons"
    bool is_header = false;   // true = section header row
    bool is_capturing = false;
};

// Mod info for mods menu
struct ModInfo {
    std::string name;           // Directory name (e.g. "re-mobilize")
    std::string display_name;   // Human-readable name for UI display
};

// Per-player scoreboard info for deathmatch
struct PlayerInfo {
    std::string name;
    int frags = 0;
    int top_color = 0;
    int bottom_color = 0;
    int ping = 0;
    bool is_local = false;

    bool operator==(const PlayerInfo& o) const {
        return name == o.name && frags == o.frags &&
               top_color == o.top_color && bottom_color == o.bottom_color &&
               ping == o.ping && is_local == o.is_local;
    }
    bool operator!=(const PlayerInfo& o) const { return !(*this == o); }
};

// Game state structure that mirrors Quake's cl.stats and cl.items
struct GameState {
    // Core stats (from cl.stats[])
    int health = 100;
    int armor = 0;
    int ammo = 0;
    int active_weapon = 0;

    // Ammo counts
    int shells = 0;
    int nails = 0;
    int rockets = 0;
    int cells = 0;

    // Level statistics
    int monsters = 0;
    int total_monsters = 0;
    int secrets = 0;
    int total_secrets = 0;

    // Weapons owned (from cl.items bitflags)
    bool has_shotgun = false;
    bool has_super_shotgun = false;
    bool has_nailgun = false;
    bool has_super_nailgun = false;
    bool has_grenade_launcher = false;
    bool has_rocket_launcher = false;
    bool has_lightning_gun = false;

    // Keys
    bool has_key1 = false;  // Silver key
    bool has_key2 = false;  // Gold key

    // Powerups
    bool has_invisibility = false;
    bool has_invulnerability = false;
    bool has_suit = false;
    bool has_quad = false;

    // Sigils (runes)
    bool has_sigil1 = false;
    bool has_sigil2 = false;
    bool has_sigil3 = false;
    bool has_sigil4 = false;

    // Armor type (0=none, 1=green, 2=yellow, 3=red)
    int armor_type = 0;

    // Game state flags
    bool intermission = false;
    int intermission_type = 0;  // 0=none, 1=level end, 2=finale, 3=cutscene
    bool deathmatch = false;
    bool coop = false;

    // Level info
    std::string level_name;
    std::string map_name;

    // Time
    int time_minutes = 0;
    int time_seconds = 0;

    // Face animation state (0-4 health tier, plus special states)
    int face_index = 0;
    bool face_pain = false;

    // Deathmatch player list (sorted by frags descending)
    std::vector<PlayerInfo> players;
    int num_players = 0;

    // Save slot data (populated on-demand when load/save menu opens)
    std::vector<SaveSlotInfo> save_slots;

    // Mod list (populated on-demand when mods menu opens)
    std::vector<ModInfo> mods;
    int num_mods = 0;
};

} // namespace QRmlUI

#endif // QRMLUI_DOMAIN_GAME_STATE_H
