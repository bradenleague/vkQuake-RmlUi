/*
 * vkQuake RmlUI - Game Field Schema
 *
 * Canonical list of all fields in the Lua 'game' table.
 * Used by the lua_test harness to verify parity between
 * GameDataModel bindings and the Lua game table.
 *
 * This is a validation-only header — it does NOT drive
 * data population (that lives in lua_bridge.cpp and
 * game_data_model.cpp).
 */

#ifndef QRMLUI_GAME_FIELD_SCHEMA_H
#define QRMLUI_GAME_FIELD_SCHEMA_H

namespace QRmlUI
{

enum class FieldType
{
	Int,
	Bool,
	String
};

struct GameFieldDef
{
	const char *name;
	FieldType	type;
};

// Every field that lua_bridge.cpp pushes into the 'game' table.
// Keep in sync with LuaBridge::Update().
inline constexpr GameFieldDef kGameFields[] = {
	// Core stats
	{"health", FieldType::Int},
	{"armor", FieldType::Int},
	{"ammo", FieldType::Int},
	{"active_weapon", FieldType::Int},

	// Ammo counts
	{"shells", FieldType::Int},
	{"nails", FieldType::Int},
	{"rockets", FieldType::Int},
	{"cells", FieldType::Int},

	// Level statistics
	{"monsters", FieldType::Int},
	{"total_monsters", FieldType::Int},
	{"secrets", FieldType::Int},
	{"total_secrets", FieldType::Int},

	// Weapons owned
	{"has_shotgun", FieldType::Bool},
	{"has_super_shotgun", FieldType::Bool},
	{"has_nailgun", FieldType::Bool},
	{"has_super_nailgun", FieldType::Bool},
	{"has_grenade_launcher", FieldType::Bool},
	{"has_rocket_launcher", FieldType::Bool},
	{"has_lightning_gun", FieldType::Bool},

	// Keys
	{"has_key1", FieldType::Bool},
	{"has_key2", FieldType::Bool},

	// Powerups
	{"has_invisibility", FieldType::Bool},
	{"has_invulnerability", FieldType::Bool},
	{"has_suit", FieldType::Bool},
	{"has_quad", FieldType::Bool},

	// Sigils
	{"has_sigil1", FieldType::Bool},
	{"has_sigil2", FieldType::Bool},
	{"has_sigil3", FieldType::Bool},
	{"has_sigil4", FieldType::Bool},

	// Armor type
	{"armor_type", FieldType::Int},

	// Computed weapon fields
	{"weapon_label", FieldType::String},
	{"ammo_type_label", FieldType::String},
	{"is_axe", FieldType::Bool},
	{"is_shells_weapon", FieldType::Bool},
	{"is_nails_weapon", FieldType::Bool},
	{"is_rockets_weapon", FieldType::Bool},
	{"is_cells_weapon", FieldType::Bool},

	// Game state flags
	{"deathmatch", FieldType::Bool},
	{"coop", FieldType::Bool},
	{"intermission", FieldType::Bool},
	{"intermission_type", FieldType::Int},

	// Level info
	{"level_name", FieldType::String},
	{"map_name", FieldType::String},
	{"game_title", FieldType::String},

	// Time
	{"time_minutes", FieldType::Int},
	{"time_seconds", FieldType::Int},

	// Face animation state
	{"face_index", FieldType::Int},
	{"face_pain", FieldType::Bool},

	// Reticle state
	{"reticle_style", FieldType::Int},
	{"weapon_show", FieldType::Bool},
	{"fire_flash", FieldType::Bool},
	{"weapon_firing", FieldType::Bool},

	// Chat
	{"chat_active", FieldType::Bool},
	{"chat_prefix", FieldType::String},
	{"chat_text", FieldType::String},

	// Player count
	{"num_players", FieldType::Int},
};

inline constexpr int kGameFieldCount = sizeof (kGameFields) / sizeof (kGameFields[0]);

} // namespace QRmlUI

#endif // QRMLUI_GAME_FIELD_SCHEMA_H
