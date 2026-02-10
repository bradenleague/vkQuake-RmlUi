/*
 * vkQuake RmlUI - Lua Engine Bridge Implementation
 *
 * Registers two global Lua tables:
 *   game   — read-only proxy over a backing table, refreshed each frame
 *   engine — exec(), cvar_get(), cvar_get_number(), cvar_set(), time()
 *
 * The 'game' table uses a metatable proxy so scripts can read fields
 * (game.health, game.weapon_label) but writes raise a Lua error.
 */

#ifdef USE_LUA

#include "lua_bridge.h"
#include "action_registry.h"
#include "game_data_model.h"
#include "menu_event_handler.h"
#include "engine_bridge.h"

#include "quake_stats.h"

#include <RmlUi/Lua/IncludeLua.h>
#include <RmlUi/Lua/Interpreter.h>

#include <string>

namespace QRmlUI
{
namespace LuaBridge
{

static lua_State *s_lua = nullptr;

// Registry reference to the backing table behind the 'game' proxy.
static int s_game_backing_ref = LUA_NOREF;

// ── engine.* C functions ────────────────────────────────────────────

static int l_engine_exec (lua_State *L)
{
	const char *cmd = luaL_checkstring (L, 1);
	Cbuf_AddText (cmd);
	Cbuf_AddText ("\n");
	return 0;
}

static int l_engine_cvar_get (lua_State *L)
{
	const char *name = luaL_checkstring (L, 1);
	const char *val = Cvar_VariableString (name);
	lua_pushstring (L, val);
	return 1;
}

static int l_engine_cvar_get_number (lua_State *L)
{
	const char *name = luaL_checkstring (L, 1);
	lua_pushnumber (L, Cvar_VariableValue (name));
	return 1;
}

static int l_engine_cvar_set (lua_State *L)
{
	const char *name = luaL_checkstring (L, 1);
	const char *value = luaL_checkstring (L, 2);
	Cvar_Set (name, value);
	return 0;
}

static int l_engine_time (lua_State *L)
{
	lua_pushnumber (L, realtime);
	return 1;
}

static int l_engine_on_frame (lua_State *L)
{
	const char *name = luaL_checkstring (L, 1);
	luaL_checktype (L, 2, LUA_TFUNCTION);

	lua_getglobal (L, "_frame_callbacks");
	lua_pushvalue (L, 2); // copy the function
	lua_setfield (L, -2, name);
	lua_pop (L, 1); // pop _frame_callbacks
	return 0;
}

// ── Weapon label helper (mirrors game_data_model.cpp logic) ─────────

static const char *GetWeaponLabel (int active_weapon)
{
	static const struct
	{
		int			bitflag;
		const char *label;
	} weapons[] = {
		{IT_AXE, "AXE"},
		{IT_SHOTGUN, "SHOTGUN"},
		{IT_SUPER_SHOTGUN, "SUPER SHOTGUN"},
		{IT_NAILGUN, "NAILGUN"},
		{IT_SUPER_NAILGUN, "SUPER NAILGUN"},
		{IT_GRENADE_LAUNCHER, "GRENADE L."},
		{IT_ROCKET_LAUNCHER, "ROCKET L."},
		{IT_LIGHTNING, "LIGHTNING"},
	};
	for (const auto &w : weapons)
	{
		if (w.bitflag == active_weapon)
			return w.label;
	}
	return "";
}

// ── Ammo type label helper (mirrors game_data_model.cpp logic) ──────

static const char *GetAmmoTypeLabel (int active_weapon)
{
	switch (active_weapon)
	{
	case 1:
	case 2:
		return "SHELLS";
	case 4:
	case 8:
		return "NAILS";
	case 16:
	case 32:
		return "ROCKETS";
	case 64:
		return "CELLS";
	default:
		return "";
	}
}

// ── Table field helpers ─────────────────────────────────────────────

static void SetInt (lua_State *L, const char *key, int val)
{
	lua_pushinteger (L, val);
	lua_setfield (L, -2, key);
}

static void SetBool (lua_State *L, const char *key, bool val)
{
	lua_pushboolean (L, val ? 1 : 0);
	lua_setfield (L, -2, key);
}

static void SetString (lua_State *L, const char *key, const char *val)
{
	lua_pushstring (L, val ? val : "");
	lua_setfield (L, -2, key);
}

// ── Read-only proxy metatable ───────────────────────────────────────

// __index: delegate reads to the backing table (upvalue 1)
static int l_game_index (lua_State *L)
{
	lua_pushvalue (L, lua_upvalueindex (1)); // backing table
	lua_pushvalue (L, 2);					 // key
	lua_gettable (L, -2);
	return 1;
}

// __newindex: reject writes
static int l_game_newindex (lua_State *L)
{
	return luaL_error (L, "attempt to write to read-only 'game' table");
}

// ── Menu action dispatch ────────────────────────────────────────────
// The Lua plugin replaces the default event listener instancer, so all
// onclick="..." attributes are compiled as Lua.  The existing menu
// documents use action strings like navigate('options'), new_game(),
// close(), etc.  We register each as a Lua global that reconstructs
// the action string and routes it through MenuEventHandler.

static int l_action_dispatch (lua_State *L)
{
	const char *func_name = lua_tostring (L, lua_upvalueindex (1));
	int			nargs = lua_gettop (L);

	std::string action (func_name);
	if (nargs == 0)
	{
		action += "()";
	}
	else if (nargs == 1)
	{
		action += "('";
		action += luaL_checkstring (L, 1);
		action += "')";
	}
	else
	{
		// Two args: e.g. cycle_cvar('name', 1)
		action += "('";
		action += luaL_checkstring (L, 1);
		action += "', ";
		action += std::to_string ((int)luaL_checknumber (L, 2));
		action += ")";
	}

	MenuEventHandler::ProcessAction (action);
	return 0;
}

// ── Public API ──────────────────────────────────────────────────────

void Initialize ()
{
	s_lua = Rml::Lua::Interpreter::GetLuaState ();
	if (!s_lua)
	{
		Con_Printf ("LuaBridge::Initialize: No Lua state available\n");
		return;
	}

	// Create the 'engine' table with C functions
	lua_newtable (s_lua);

	lua_pushcfunction (s_lua, l_engine_exec);
	lua_setfield (s_lua, -2, "exec");

	lua_pushcfunction (s_lua, l_engine_cvar_get);
	lua_setfield (s_lua, -2, "cvar_get");

	lua_pushcfunction (s_lua, l_engine_cvar_get_number);
	lua_setfield (s_lua, -2, "cvar_get_number");

	lua_pushcfunction (s_lua, l_engine_cvar_set);
	lua_setfield (s_lua, -2, "cvar_set");

	lua_pushcfunction (s_lua, l_engine_time);
	lua_setfield (s_lua, -2, "time");

	lua_pushcfunction (s_lua, l_engine_on_frame);
	lua_setfield (s_lua, -2, "on_frame");

	lua_setglobal (s_lua, "engine");

	// Create the named frame-callback table
	lua_newtable (s_lua);
	lua_setglobal (s_lua, "_frame_callbacks");

	// Create the read-only 'game' proxy:
	//   backing = {}          (stored in Lua registry)
	//   proxy   = {}          (exposed as global 'game')
	//   proxy's metatable:
	//     __index    = closure over backing
	//     __newindex = error
	//     __metatable = false  (hide metatable)

	// backing table
	lua_newtable (s_lua);
	s_game_backing_ref = luaL_ref (s_lua, LUA_REGISTRYINDEX);

	// proxy table
	lua_newtable (s_lua);

	// metatable
	lua_newtable (s_lua);

	// __index closure: upvalue is the backing table
	lua_rawgeti (s_lua, LUA_REGISTRYINDEX, s_game_backing_ref);
	lua_pushcclosure (s_lua, l_game_index, 1);
	lua_setfield (s_lua, -2, "__index");

	lua_pushcfunction (s_lua, l_game_newindex);
	lua_setfield (s_lua, -2, "__newindex");

	lua_pushboolean (s_lua, 0);
	lua_setfield (s_lua, -2, "__metatable");

	lua_setmetatable (s_lua, -2); // set metatable on proxy
	lua_setglobal (s_lua, "game");

	// Register menu action functions as Lua globals so existing
	// onclick="navigate('options')" etc. work with the Lua instancer.
	for (int i = 0; i < kActionRegistrySize; i++)
	{
		lua_pushstring (s_lua, kActionRegistry[i].name);
		lua_pushcclosure (s_lua, l_action_dispatch, 1);
		lua_setglobal (s_lua, kActionRegistry[i].name);
	}

	Con_DPrintf ("LuaBridge: Initialized game/engine/action Lua globals\n");
}

void Update ()
{
	if (!s_lua)
		return;

	const auto &gs = g_game_state;

	// Push the backing table onto the stack and populate it
	lua_rawgeti (s_lua, LUA_REGISTRYINDEX, s_game_backing_ref);

	// Core stats
	SetInt (s_lua, "health", gs.health);
	SetInt (s_lua, "armor", gs.armor);
	SetInt (s_lua, "ammo", gs.ammo);
	SetInt (s_lua, "active_weapon", gs.active_weapon);

	// Ammo counts
	SetInt (s_lua, "shells", gs.shells);
	SetInt (s_lua, "nails", gs.nails);
	SetInt (s_lua, "rockets", gs.rockets);
	SetInt (s_lua, "cells", gs.cells);

	// Level statistics
	SetInt (s_lua, "monsters", gs.monsters);
	SetInt (s_lua, "total_monsters", gs.total_monsters);
	SetInt (s_lua, "secrets", gs.secrets);
	SetInt (s_lua, "total_secrets", gs.total_secrets);

	// Weapons owned
	SetBool (s_lua, "has_shotgun", gs.has_shotgun);
	SetBool (s_lua, "has_super_shotgun", gs.has_super_shotgun);
	SetBool (s_lua, "has_nailgun", gs.has_nailgun);
	SetBool (s_lua, "has_super_nailgun", gs.has_super_nailgun);
	SetBool (s_lua, "has_grenade_launcher", gs.has_grenade_launcher);
	SetBool (s_lua, "has_rocket_launcher", gs.has_rocket_launcher);
	SetBool (s_lua, "has_lightning_gun", gs.has_lightning_gun);

	// Keys
	SetBool (s_lua, "has_key1", gs.has_key1);
	SetBool (s_lua, "has_key2", gs.has_key2);

	// Powerups
	SetBool (s_lua, "has_invisibility", gs.has_invisibility);
	SetBool (s_lua, "has_invulnerability", gs.has_invulnerability);
	SetBool (s_lua, "has_suit", gs.has_suit);
	SetBool (s_lua, "has_quad", gs.has_quad);

	// Sigils
	SetBool (s_lua, "has_sigil1", gs.has_sigil1);
	SetBool (s_lua, "has_sigil2", gs.has_sigil2);
	SetBool (s_lua, "has_sigil3", gs.has_sigil3);
	SetBool (s_lua, "has_sigil4", gs.has_sigil4);

	// Armor type
	SetInt (s_lua, "armor_type", gs.armor_type);

	// Computed fields (parity with GameDataModel)
	SetString (s_lua, "weapon_label", GetWeaponLabel (gs.active_weapon));
	SetString (s_lua, "ammo_type_label", GetAmmoTypeLabel (gs.active_weapon));
	SetBool (s_lua, "is_axe", gs.active_weapon == IT_AXE);

	int w = gs.active_weapon;
	SetBool (s_lua, "is_shells_weapon", w == 1 || w == 2);
	SetBool (s_lua, "is_nails_weapon", w == 4 || w == 8);
	SetBool (s_lua, "is_rockets_weapon", w == 16 || w == 32);
	SetBool (s_lua, "is_cells_weapon", w == 64);

	// Game state flags
	SetBool (s_lua, "deathmatch", gs.deathmatch);
	SetBool (s_lua, "coop", gs.coop);
	SetBool (s_lua, "intermission", gs.intermission);
	SetInt (s_lua, "intermission_type", gs.intermission_type);

	// Level info
	SetString (s_lua, "level_name", gs.level_name.c_str ());
	SetString (s_lua, "map_name", gs.map_name.c_str ());

	// Game title (from active game directory)
	{
		const char *game = COM_GetGameNames (0);
		SetString (s_lua, "game_title", (game && game[0]) ? game : "QUAKE");
	}

	// Time
	SetInt (s_lua, "time_minutes", gs.time_minutes);
	SetInt (s_lua, "time_seconds", gs.time_seconds);

	// Face animation state
	SetInt (s_lua, "face_index", gs.face_index);
	SetBool (s_lua, "face_pain", gs.face_pain);

	// Reticle state
	SetInt (s_lua, "reticle_style", gs.reticle_style);
	SetBool (s_lua, "weapon_show", gs.weapon_show);
	SetBool (s_lua, "fire_flash", gs.fire_flash);
	SetBool (s_lua, "weapon_firing", gs.weapon_firing);

	// Chat input overlay
	SetBool (s_lua, "chat_active", key_dest == key_message);
	SetString (s_lua, "chat_prefix", chat_team ? "say_team:" : "say:");
	{
		const char *buf = Key_GetChatBuffer ();
		SetString (s_lua, "chat_text", buf ? buf : "");
	}

	// Player count
	SetInt (s_lua, "num_players", gs.num_players);

	lua_pop (s_lua, 1); // pop backing table

	// Dispatch named frame callbacks
	lua_getglobal (s_lua, "_frame_callbacks");
	lua_pushnil (s_lua);
	while (lua_next (s_lua, -2) != 0)
	{
		// stack: _frame_callbacks, key, function
		if (lua_isfunction (s_lua, -1))
		{
			if (lua_pcall (s_lua, 0, 0, 0) != 0)
			{
				Con_Printf ("LuaBridge: frame callback '%s' error: %s\n", lua_tostring (s_lua, -2), lua_tostring (s_lua, -1));
				lua_pop (s_lua, 1); // pop error message
			}
		}
		else
		{
			lua_pop (s_lua, 1); // pop non-function value
		}
		// key remains on stack for lua_next
	}
	lua_pop (s_lua, 1); // pop _frame_callbacks
}

void RunTests ()
{
	if (!s_lua)
	{
		Con_Printf ("LuaBridge::RunTests: No Lua state\n");
		return;
	}

	const char *test_path = "ui/lua/tests/test_runner.lua";

	// Use RmlUI's Interpreter to load and run the file through the VFS
	if (!Rml::Lua::Interpreter::LoadFile (Rml::String (test_path)))
	{
		Con_Printf ("LuaBridge::RunTests: Failed to load '%s'\n", test_path);
	}
}

} // namespace LuaBridge
} // namespace QRmlUI

#endif // USE_LUA
