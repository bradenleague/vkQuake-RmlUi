/*
 * vkQuake RmlUI - Shared Action Registry
 *
 * Single source of truth for all menu action names and their argument
 * types.  Consumed by:
 *   - lua_bridge.cpp    (register Lua globals)
 *   - menu_event_handler.cpp  (validate action names in ExecuteAction)
 */

#ifndef QRMLUI_ACTION_REGISTRY_H
#define QRMLUI_ACTION_REGISTRY_H

namespace QRmlUI
{

enum class ActionArgType
{
	None,	  // close(), quit(), new_game(), close_all(), main_menu()
	String,	  // navigate('x'), command('x'), load_game('x'), etc.
	StringInt // cycle_cvar('x', 1)
};

struct ActionDef
{
	const char	 *name;
	ActionArgType arg_type;
};

// All 15 menu actions supported by MenuEventHandler::ExecuteAction.
// Order is irrelevant; the array is searched linearly.
inline constexpr ActionDef kActionRegistry[] = {
	{"navigate", ActionArgType::String},	 {"command", ActionArgType::String},
	{"cvar_changed", ActionArgType::String}, {"cycle_cvar", ActionArgType::StringInt},
	{"close", ActionArgType::None},			 {"close_all", ActionArgType::None},
	{"quit", ActionArgType::None},			 {"new_game", ActionArgType::None},
	{"load_game", ActionArgType::String},	 {"save_game", ActionArgType::String},
	{"bind_key", ActionArgType::String},	 {"main_menu", ActionArgType::None},
	{"connect_to", ActionArgType::String},	 {"host_game", ActionArgType::String},
	{"load_mod", ActionArgType::String},
};

inline constexpr int kActionRegistrySize = sizeof (kActionRegistry) / sizeof (kActionRegistry[0]);

// Look up an action by name.  Returns nullptr if not found.
inline const ActionDef *FindAction (const char *name)
{
	for (int i = 0; i < kActionRegistrySize; i++)
	{
		// strcmp-style comparison (no <cstring> needed for constexpr path)
		const char *a = kActionRegistry[i].name;
		const char *b = name;
		while (*a && *a == *b)
		{
			a++;
			b++;
		}
		if (*a == '\0' && *b == '\0')
			return &kActionRegistry[i];
	}
	return nullptr;
}

} // namespace QRmlUI

#endif // QRMLUI_ACTION_REGISTRY_H
