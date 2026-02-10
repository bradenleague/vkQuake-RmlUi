/*
 * vkQuake RmlUI - Game Data Model Implementation
 *
 * Synchronizes Quake game state to RmlUI data binding system.
 */

#include "game_data_model.h"
#include "cvar_binding.h"
#include "menu_event_handler.h"
#include "notification_model.h"

#include "quake_stats.h"

#include "engine_bridge.h"

namespace QRmlUI
{

// ── WeaponInfo lookup table ──────────────────────────────────────────
// Single source of truth for weapon bitflag → short name / display name.
// Used by weapon_label binding and per-weapon reticle cvar resolution.

struct WeaponInfo
{
	int			bitflag;	  // IT_SHOTGUN, IT_ROCKET_LAUNCHER, etc.
	const char *short_name;	  // "sg", "rl" — used for cvar name construction
	const char *display_name; // "SHOTGUN", "ROCKET L." — for weapon_label binding
};

static const WeaponInfo s_weapons[] = {
	{IT_AXE, "axe", "AXE"},
	{IT_SHOTGUN, "sg", "SHOTGUN"},
	{IT_SUPER_SHOTGUN, "ssg", "SUPER SHOTGUN"},
	{IT_NAILGUN, "ng", "NAILGUN"},
	{IT_SUPER_NAILGUN, "sng", "SUPER NAILGUN"},
	{IT_GRENADE_LAUNCHER, "gl", "GRENADE L."},
	{IT_ROCKET_LAUNCHER, "rl", "ROCKET L."},
	{IT_LIGHTNING, "lg", "LIGHTNING"},
};

// Resolve the effective reticle style from crosshair + weapon overrides.
// Pure function: Off always wins → weapon override if set → player's crosshair.
static int ResolveReticleStyle (int active_weapon, ICvarProvider *provider)
{
	int crosshair = (int)provider->GetFloat ("crosshair");
	if (crosshair <= 0)
		return 0; // Off always wins

	// Check weapon-specific override
	for (const auto &w : s_weapons)
	{
		if (w.bitflag == active_weapon)
		{
			std::string cvar_name = std::string ("crosshair_weapon_") + w.short_name;
			int			override_val = (int)provider->GetFloat (cvar_name.c_str ());
			if (override_val > 0)
				return override_val;
			break;
		}
	}

	return crosshair; // Player's fallback
}

// Global game state
GameState g_game_state;

// Static members
Rml::DataModelHandle GameDataModel::s_model_handle;
bool				 GameDataModel::s_initialized = false;
GameState			 GameDataModel::s_prev_state;
bool				 GameDataModel::s_first_update = true;
Rml::String			 GameDataModel::s_prev_gamedir;
bool				 GameDataModel::s_was_chatting = false;

// Reticle animation transient state
static double s_weapon_show_time = 0.0;
static double s_fire_flash_time = 0.0;
static int	  s_prev_weaponframe = 0;
static constexpr double WEAPON_SHOW_DURATION = 0.15;
static constexpr double FIRE_FLASH_DURATION = 0.30;

bool GameDataModel::Initialize (Rml::Context *context)
{
	if (s_initialized)
	{
		Con_DPrintf ("GameDataModel: Already initialized\n");
		return true;
	}

	if (!context)
	{
		Con_Printf ("GameDataModel: ERROR - null context\n");
		return false;
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel ("game");
	if (!constructor)
	{
		Con_Printf ("GameDataModel: ERROR - Failed to create data model\n");
		return false;
	}

	// Bind core stats
	constructor.Bind ("health", &g_game_state.health);
	constructor.Bind ("armor", &g_game_state.armor);
	constructor.Bind ("ammo", &g_game_state.ammo);
	constructor.Bind ("active_weapon", &g_game_state.active_weapon);

	// Bind ammo counts
	constructor.Bind ("shells", &g_game_state.shells);
	constructor.Bind ("nails", &g_game_state.nails);
	constructor.Bind ("rockets", &g_game_state.rockets);
	constructor.Bind ("cells", &g_game_state.cells);

	// Bind level statistics
	constructor.Bind ("monsters", &g_game_state.monsters);
	constructor.Bind ("total_monsters", &g_game_state.total_monsters);
	constructor.Bind ("secrets", &g_game_state.secrets);
	constructor.Bind ("total_secrets", &g_game_state.total_secrets);

	// Bind weapon ownership
	constructor.Bind ("has_shotgun", &g_game_state.has_shotgun);
	constructor.Bind ("has_super_shotgun", &g_game_state.has_super_shotgun);
	constructor.Bind ("has_nailgun", &g_game_state.has_nailgun);
	constructor.Bind ("has_super_nailgun", &g_game_state.has_super_nailgun);
	constructor.Bind ("has_grenade_launcher", &g_game_state.has_grenade_launcher);
	constructor.Bind ("has_rocket_launcher", &g_game_state.has_rocket_launcher);
	constructor.Bind ("has_lightning_gun", &g_game_state.has_lightning_gun);

	// Bind keys
	constructor.Bind ("has_key1", &g_game_state.has_key1);
	constructor.Bind ("has_key2", &g_game_state.has_key2);

	// Bind powerups
	constructor.Bind ("has_invisibility", &g_game_state.has_invisibility);
	constructor.Bind ("has_invulnerability", &g_game_state.has_invulnerability);
	constructor.Bind ("has_suit", &g_game_state.has_suit);
	constructor.Bind ("has_quad", &g_game_state.has_quad);

	// Bind sigils
	constructor.Bind ("has_sigil1", &g_game_state.has_sigil1);
	constructor.Bind ("has_sigil2", &g_game_state.has_sigil2);
	constructor.Bind ("has_sigil3", &g_game_state.has_sigil3);
	constructor.Bind ("has_sigil4", &g_game_state.has_sigil4);

	// Bind armor type
	constructor.Bind ("armor_type", &g_game_state.armor_type);

	// Bind game state flags
	constructor.Bind ("intermission", &g_game_state.intermission);
	constructor.Bind ("intermission_type", &g_game_state.intermission_type);
	constructor.Bind ("deathmatch", &g_game_state.deathmatch);
	constructor.Bind ("coop", &g_game_state.coop);

	// Bind level info
	constructor.Bind ("level_name", &g_game_state.level_name);
	constructor.Bind ("map_name", &g_game_state.map_name);

	// Bind game title from active game directory name
	constructor.BindFunc (
		"game_title",
		[] (Rml::Variant &variant)
		{
			const char *game = COM_GetGameNames (0);
			variant = Rml::String (game && game[0] ? game : "QUAKE");
		});

	// Bind time (seconds zero-padded to 2 digits for display)
	constructor.Bind ("time_minutes", &g_game_state.time_minutes);
	constructor.BindFunc (
		"time_seconds",
		[] (Rml::Variant &variant)
		{
			char buf[4];
			snprintf (buf, sizeof (buf), "%02d", g_game_state.time_seconds);
			variant = Rml::String (buf);
		});

	// Bind face state
	constructor.Bind ("face_index", &g_game_state.face_index);
	constructor.Bind ("face_pain", &g_game_state.face_pain);

	// Computed: weapon label from active_weapon bitflag (uses WeaponInfo table)
	constructor.BindFunc (
		"weapon_label",
		[] (Rml::Variant &variant)
		{
			for (const auto &w : s_weapons)
			{
				if (w.bitflag == g_game_state.active_weapon)
				{
					variant = Rml::String (w.display_name);
					return;
				}
			}
			variant = Rml::String ("");
		});

	// Computed: ammo type label
	constructor.BindFunc (
		"ammo_type_label",
		[] (Rml::Variant &variant)
		{
			switch (g_game_state.active_weapon)
			{
			case 1:
			case 2:
				variant = Rml::String ("SHELLS");
				break;
			case 4:
			case 8:
				variant = Rml::String ("NAILS");
				break;
			case 16:
			case 32:
				variant = Rml::String ("ROCKETS");
				break;
			case 64:
				variant = Rml::String ("CELLS");
				break;
			default:
				variant = Rml::String ("");
				break;
			}
		});

	// Computed: is_axe (hide ammo display when wielding axe)
	constructor.BindFunc ("is_axe", [] (Rml::Variant &variant) { variant = (g_game_state.active_weapon == IT_AXE); });

	// Reticle (crosshair) bindings
	constructor.Bind ("reticle_style", &g_game_state.reticle_style);

	// Reticle animation triggers (class toggles for RCSS transitions)
	constructor.Bind ("weapon_show", &g_game_state.weapon_show);
	constructor.Bind ("fire_flash", &g_game_state.fire_flash);

	// Computed: active ammo type booleans for reserves highlight
	constructor.BindFunc (
		"is_shells_weapon",
		[] (Rml::Variant &variant)
		{
			int w = g_game_state.active_weapon;
			variant = (w == 1 || w == 2);
		});

	constructor.BindFunc (
		"is_nails_weapon",
		[] (Rml::Variant &variant)
		{
			int w = g_game_state.active_weapon;
			variant = (w == 4 || w == 8);
		});

	constructor.BindFunc (
		"is_rockets_weapon",
		[] (Rml::Variant &variant)
		{
			int w = g_game_state.active_weapon;
			variant = (w == 16 || w == 32);
		});

	constructor.BindFunc ("is_cells_weapon", [] (Rml::Variant &variant) { variant = (g_game_state.active_weapon == 64); });

	// Chat input overlay bindings
	constructor.BindFunc ("chat_active", [] (Rml::Variant &variant) { variant = (key_dest == key_message); });
	constructor.BindFunc ("chat_prefix", [] (Rml::Variant &variant) { variant = Rml::String (chat_team ? "say_team:" : "say:"); });
	constructor.BindFunc (
		"chat_text",
		[] (Rml::Variant &variant)
		{
			const char *buf = Key_GetChatBuffer ();
			variant = Rml::String (buf ? buf : "");
		});

	// Register PlayerInfo struct type for scoreboard
	if (auto player_handle = constructor.RegisterStruct<PlayerInfo> ())
	{
		player_handle.RegisterMember ("name", &PlayerInfo::name);
		player_handle.RegisterMember ("frags", &PlayerInfo::frags);
		player_handle.RegisterMember ("top_color", &PlayerInfo::top_color);
		player_handle.RegisterMember ("bottom_color", &PlayerInfo::bottom_color);
		player_handle.RegisterMember ("ping", &PlayerInfo::ping);
		player_handle.RegisterMember ("is_local", &PlayerInfo::is_local);
	}
	constructor.RegisterArray<std::vector<PlayerInfo>> ();

	// Bind player list and count
	constructor.Bind ("players", &g_game_state.players);
	constructor.Bind ("num_players", &g_game_state.num_players);

	// Register SaveSlotInfo struct type for load/save menus
	if (auto slot_handle = constructor.RegisterStruct<SaveSlotInfo> ())
	{
		slot_handle.RegisterMember ("slot_id", &SaveSlotInfo::slot_id);
		slot_handle.RegisterMember ("description", &SaveSlotInfo::description);
		slot_handle.RegisterMember ("slot_number", &SaveSlotInfo::slot_number);
		slot_handle.RegisterMember ("is_loadable", &SaveSlotInfo::is_loadable);
	}
	constructor.RegisterArray<std::vector<SaveSlotInfo>> ();

	// Bind save slot list
	constructor.Bind ("save_slots", &g_game_state.save_slots);

	// Event callback: load a save slot
	constructor.BindEventCallback (
		"load_slot",
		[] (Rml::DataModelHandle, Rml::Event &event, const Rml::VariantList &arguments)
		{
			if (arguments.empty ())
				return;
			Rml::String slot_id = arguments[0].Get<Rml::String> ();
			if (slot_id.empty ())
				return;
			Con_DPrintf ("GameDataModel: load_slot '%s'\n", slot_id.c_str ());
			MenuEventHandler::ProcessAction ("load_game('" + std::string (slot_id.c_str ()) + "')");
		});

	// Event callback: save to a slot
	constructor.BindEventCallback (
		"save_slot",
		[] (Rml::DataModelHandle, Rml::Event &event, const Rml::VariantList &arguments)
		{
			if (arguments.empty ())
				return;
			Rml::String slot_id = arguments[0].Get<Rml::String> ();
			if (slot_id.empty ())
				return;
			Con_DPrintf ("GameDataModel: save_slot '%s'\n", slot_id.c_str ());
			MenuEventHandler::ProcessAction ("save_game('" + std::string (slot_id.c_str ()) + "')");
		});

	// Register ModInfo struct type for mods menu
	if (auto mod_handle = constructor.RegisterStruct<ModInfo> ())
	{
		mod_handle.RegisterMember ("name", &ModInfo::name);
		mod_handle.RegisterMember ("display_name", &ModInfo::display_name);
	}
	constructor.RegisterArray<std::vector<ModInfo>> ();

	// Bind mod list and count
	constructor.Bind ("mods", &g_game_state.mods);
	constructor.Bind ("num_mods", &g_game_state.num_mods);

	// Event callback: load a mod
	constructor.BindEventCallback (
		"select_mod",
		[] (Rml::DataModelHandle, Rml::Event &event, const Rml::VariantList &arguments)
		{
			if (arguments.empty ())
				return;
			Rml::String mod_name = arguments[0].Get<Rml::String> ();
			if (mod_name.empty ())
				return;
			Con_DPrintf ("GameDataModel: select_mod '%s'\n", mod_name.c_str ());
			MenuEventHandler::ProcessAction ("load_mod('" + std::string (mod_name.c_str ()) + "')");
		});

	// Register notification bindings on the same "game" model
	NotificationModel::RegisterBindings (constructor);

	s_model_handle = constructor.GetModelHandle ();

	// Share the model handle with NotificationModel for selective dirtying
	NotificationModel::SetModelHandle (s_model_handle);

	s_initialized = true;

	Con_DPrintf ("GameDataModel: Initialized successfully\n");
	return true;
}

void GameDataModel::Shutdown ()
{
	if (!s_initialized)
		return;

	NotificationModel::Shutdown ();

	// RmlUI handles cleanup when context is destroyed
	s_model_handle = Rml::DataModelHandle ();
	s_initialized = false;

	// Reset diff-tracking state so reinit starts clean
	s_prev_state = GameState{};
	s_first_update = true;
	s_prev_gamedir.clear ();
	s_was_chatting = false;

	Con_DPrintf ("GameDataModel: Shutdown\n");
}

void GameDataModel::Update ()
{
	if (!s_initialized || !s_model_handle)
		return;

	// Compare current state against cached previous state.
	// Only dirty variables that actually changed to avoid per-frame churn.
	if (s_first_update)
	{
		s_first_update = false;
		s_model_handle.DirtyAllVariables ();
		s_prev_state = g_game_state;
		return;
	}

	bool any_dirty = false;

#define DIRTY_IF_CHANGED(field, name)             \
	if (g_game_state.field != s_prev_state.field) \
	{                                             \
		s_model_handle.DirtyVariable (name);      \
		any_dirty = true;                         \
	}

	// Core stats
	DIRTY_IF_CHANGED (health, "health")
	DIRTY_IF_CHANGED (armor, "armor")
	DIRTY_IF_CHANGED (ammo, "ammo")
	DIRTY_IF_CHANGED (active_weapon, "active_weapon")

	// When active_weapon changes, computed funcs also need re-eval
	if (g_game_state.active_weapon != s_prev_state.active_weapon)
	{
		s_model_handle.DirtyVariable ("weapon_label");
		s_model_handle.DirtyVariable ("ammo_type_label");
		s_model_handle.DirtyVariable ("is_axe");
		s_model_handle.DirtyVariable ("is_shells_weapon");
		s_model_handle.DirtyVariable ("is_nails_weapon");
		s_model_handle.DirtyVariable ("is_rockets_weapon");
		s_model_handle.DirtyVariable ("is_cells_weapon");
	}

	// Ammo counts
	DIRTY_IF_CHANGED (shells, "shells")
	DIRTY_IF_CHANGED (nails, "nails")
	DIRTY_IF_CHANGED (rockets, "rockets")
	DIRTY_IF_CHANGED (cells, "cells")

	// Level statistics
	DIRTY_IF_CHANGED (monsters, "monsters")
	DIRTY_IF_CHANGED (total_monsters, "total_monsters")
	DIRTY_IF_CHANGED (secrets, "secrets")
	DIRTY_IF_CHANGED (total_secrets, "total_secrets")

	// Weapons owned
	DIRTY_IF_CHANGED (has_shotgun, "has_shotgun")
	DIRTY_IF_CHANGED (has_super_shotgun, "has_super_shotgun")
	DIRTY_IF_CHANGED (has_nailgun, "has_nailgun")
	DIRTY_IF_CHANGED (has_super_nailgun, "has_super_nailgun")
	DIRTY_IF_CHANGED (has_grenade_launcher, "has_grenade_launcher")
	DIRTY_IF_CHANGED (has_rocket_launcher, "has_rocket_launcher")
	DIRTY_IF_CHANGED (has_lightning_gun, "has_lightning_gun")

	// Keys
	DIRTY_IF_CHANGED (has_key1, "has_key1")
	DIRTY_IF_CHANGED (has_key2, "has_key2")

	// Powerups
	DIRTY_IF_CHANGED (has_invisibility, "has_invisibility")
	DIRTY_IF_CHANGED (has_invulnerability, "has_invulnerability")
	DIRTY_IF_CHANGED (has_suit, "has_suit")
	DIRTY_IF_CHANGED (has_quad, "has_quad")

	// Sigils
	DIRTY_IF_CHANGED (has_sigil1, "has_sigil1")
	DIRTY_IF_CHANGED (has_sigil2, "has_sigil2")
	DIRTY_IF_CHANGED (has_sigil3, "has_sigil3")
	DIRTY_IF_CHANGED (has_sigil4, "has_sigil4")

	// Armor type
	DIRTY_IF_CHANGED (armor_type, "armor_type")

	// Game state flags
	DIRTY_IF_CHANGED (intermission, "intermission")
	DIRTY_IF_CHANGED (intermission_type, "intermission_type")
	DIRTY_IF_CHANGED (deathmatch, "deathmatch")
	DIRTY_IF_CHANGED (coop, "coop")

	// Level info (string comparison)
	DIRTY_IF_CHANGED (level_name, "level_name")
	DIRTY_IF_CHANGED (map_name, "map_name")

	// Time
	DIRTY_IF_CHANGED (time_minutes, "time_minutes")
	DIRTY_IF_CHANGED (time_seconds, "time_seconds")

	// Face state
	DIRTY_IF_CHANGED (face_index, "face_index")
	DIRTY_IF_CHANGED (face_pain, "face_pain")

	// Reticle (crosshair)
	DIRTY_IF_CHANGED (reticle_style, "reticle_style")

	// Chat input (dirty every frame while active, plus one frame after close)
	{
		bool is_chatting = (key_dest == key_message);
		if (is_chatting || s_was_chatting)
		{
			s_model_handle.DirtyVariable ("chat_active");
			s_model_handle.DirtyVariable ("chat_prefix");
			s_model_handle.DirtyVariable ("chat_text");
			any_dirty = true;
		}
		s_was_chatting = is_chatting;
	}

	// Player list (compare count; full array dirtied on any change)
	DIRTY_IF_CHANGED (num_players, "num_players")
	if (g_game_state.num_players != s_prev_state.num_players || g_game_state.players != s_prev_state.players)
	{
		s_model_handle.DirtyVariable ("players");
		any_dirty = true;
	}

	// Game title (gamedir-backed — track changes for mod switch)
	{
		const char *g = COM_GetGameNames (0);
		Rml::String cur_gamedir (g ? g : "");
		if (cur_gamedir != s_prev_gamedir)
		{
			s_model_handle.DirtyVariable ("game_title");
			s_prev_gamedir = cur_gamedir;
			any_dirty = true;
		}
	}

	// Expire transient reticle animation flags
	if (g_game_state.weapon_show && realtime - s_weapon_show_time > WEAPON_SHOW_DURATION)
		g_game_state.weapon_show = false;
	if (g_game_state.fire_flash && realtime - s_fire_flash_time > FIRE_FLASH_DURATION)
		g_game_state.fire_flash = false;

	DIRTY_IF_CHANGED (weapon_show, "weapon_show")
	DIRTY_IF_CHANGED (fire_flash, "fire_flash")

#undef DIRTY_IF_CHANGED

	(void)any_dirty;
	s_prev_state = g_game_state;
}

void GameDataModel::MarkAllDirty ()
{
	if (!s_initialized || !s_model_handle)
		return;
	s_model_handle.DirtyAllVariables ();
}

bool GameDataModel::IsInitialized ()
{
	return s_initialized;
}

} // namespace QRmlUI

// C API Implementation
extern "C"
{

	int GameDataModel_Init (void)
	{
		// Initialization is deferred - called from UI_InitializeVulkan
		// after context is fully ready
		return 1;
	}

	void GameDataModel_Shutdown (void)
	{
		QRmlUI::GameDataModel::Shutdown ();
	}

	void GameDataModel_Update (void)
	{
		QRmlUI::GameDataModel::Update ();
	}

	void GameDataModel_SyncFromQuake (
		const int *stats, int stats_count, int items, int intermission, int gametype, int maxclients, const char *level_name, const char *map_name,
		double game_time)
	{
		using namespace QRmlUI;

		if (!stats || stats_count < STAT_ITEMS + 1)
			return;

		// Sync core stats
		g_game_state.health = stats[STAT_HEALTH];
		g_game_state.armor = stats[STAT_ARMOR];
		g_game_state.ammo = stats[STAT_AMMO];
		g_game_state.active_weapon = stats[STAT_ACTIVEWEAPON];

		// Sync ammo counts
		g_game_state.shells = stats[STAT_SHELLS];
		g_game_state.nails = stats[STAT_NAILS];
		g_game_state.rockets = stats[STAT_ROCKETS];
		g_game_state.cells = stats[STAT_CELLS];

		// Sync level statistics
		g_game_state.monsters = stats[STAT_MONSTERS];
		g_game_state.total_monsters = stats[STAT_TOTALMONSTERS];
		g_game_state.secrets = stats[STAT_SECRETS];
		g_game_state.total_secrets = stats[STAT_TOTALSECRETS];

		// Decode item bitflags for weapons
		g_game_state.has_shotgun = (items & IT_SHOTGUN) != 0;
		g_game_state.has_super_shotgun = (items & IT_SUPER_SHOTGUN) != 0;
		g_game_state.has_nailgun = (items & IT_NAILGUN) != 0;
		g_game_state.has_super_nailgun = (items & IT_SUPER_NAILGUN) != 0;
		g_game_state.has_grenade_launcher = (items & IT_GRENADE_LAUNCHER) != 0;
		g_game_state.has_rocket_launcher = (items & IT_ROCKET_LAUNCHER) != 0;
		g_game_state.has_lightning_gun = (items & IT_LIGHTNING) != 0;

		// Decode keys
		g_game_state.has_key1 = (items & IT_KEY1) != 0;
		g_game_state.has_key2 = (items & IT_KEY2) != 0;

		// Decode powerups
		g_game_state.has_invisibility = (items & IT_INVISIBILITY) != 0;
		g_game_state.has_invulnerability = (items & IT_INVULNERABILITY) != 0;
		g_game_state.has_suit = (items & IT_SUIT) != 0;
		g_game_state.has_quad = (items & IT_QUAD) != 0;

		// Decode sigils
		g_game_state.has_sigil1 = (items & IT_SIGIL1) != 0;
		g_game_state.has_sigil2 = (items & IT_SIGIL2) != 0;
		g_game_state.has_sigil3 = (items & IT_SIGIL3) != 0;
		g_game_state.has_sigil4 = (items & IT_SIGIL4) != 0;

		// Determine armor type from items
		if (items & IT_ARMOR3)
		{
			g_game_state.armor_type = 3; // Red
		}
		else if (items & IT_ARMOR2)
		{
			g_game_state.armor_type = 2; // Yellow
		}
		else if (items & IT_ARMOR1)
		{
			g_game_state.armor_type = 1; // Green
		}
		else
		{
			g_game_state.armor_type = 0; // None
		}

		// Game state
		g_game_state.intermission = (intermission != 0);
		g_game_state.intermission_type = intermission;
		g_game_state.deathmatch = (gametype != 0);
		g_game_state.coop = (gametype == 0 && maxclients > 1);

		// Level info
		if (level_name)
		{
			g_game_state.level_name = level_name;
		}
		if (map_name)
		{
			g_game_state.map_name = map_name;
		}

		// Time calculation
		int total_seconds = static_cast<int> (game_time);
		g_game_state.time_minutes = total_seconds / 60;
		g_game_state.time_seconds = total_seconds % 60;

		// Detect damage taken (health decreased since last sync).
		// Threshold of 2 HP filters out megahealth decay (1 HP/sec tick-down)
		// while catching real damage (minimum 5 HP in Quake).
		{
			static int s_prev_health = 100;
			int		   drop = s_prev_health - g_game_state.health;
			g_game_state.face_pain = (drop >= 2 && s_prev_health > 0);
			s_prev_health = g_game_state.health;
		}

		// Detect weapon switch (skip initial equip when prev was 0)
		{
			static int s_prev_active_weapon = 0;
			if (g_game_state.active_weapon != s_prev_active_weapon && s_prev_active_weapon != 0)
			{
				g_game_state.weapon_show = true;
				s_weapon_show_time = realtime;
			}
			s_prev_active_weapon = g_game_state.active_weapon;
		}

		// Detect fire: weaponframe transitions from 0 → non-zero
		{
			int wf = (stats_count > STAT_WEAPONFRAME) ? stats[STAT_WEAPONFRAME] : 0;
			if (wf != 0 && s_prev_weaponframe == 0)
			{
				g_game_state.fire_flash = true;
				s_fire_flash_time = realtime;
			}
			s_prev_weaponframe = wf;
		}

		// Resolve reticle style from crosshair cvar + per-weapon overrides
		g_game_state.reticle_style = ResolveReticleStyle (g_game_state.active_weapon, CvarBindingManager::GetProvider ());

		// Calculate face index based on health
		int health = g_game_state.health;
		if (health >= 100)
		{
			g_game_state.face_index = 4;
		}
		else if (health >= 80)
		{
			g_game_state.face_index = 3;
		}
		else if (health >= 60)
		{
			g_game_state.face_index = 2;
		}
		else if (health >= 40)
		{
			g_game_state.face_index = 1;
		}
		else
		{
			g_game_state.face_index = 0;
		}
	}

} // extern "C"
