/*
 * vkQuake RmlUI - Cvar Binding Manager Implementation
 *
 * Two-way sync between RmlUI data model and Quake cvars.
 */

#include "cvar_binding.h"
#include "quake_cvar_provider.h"
#include "../types/game_state.h"

#include <algorithm>
#include <cmath>

#include "engine_bridge.h"

// Our own C API — not engine functions, but needed here to avoid
// circular includes with menu_event_handler.h.
extern "C"
{
	void MenuEventHandler_ProcessAction (const char *action);
}

namespace QRmlUI
{

// Static video modes vector (lives here because video modes are a "cvars" model concern)
static std::vector<VideoModeInfo> s_video_modes;

// Static keybinds vector for the controls menu
static std::vector<KeybindInfo> s_keybinds;

// Keybind definition table (action, label, section)
struct KeybindDef
{
	const char *action;
	const char *label;
	const char *section;
};

static const KeybindDef kKeybindDefs[] = {
	{"+forward", "Move Forward", "Movement"},
	{"+back", "Move Backward", "Movement"},
	{"+moveleft", "Strafe Left", "Movement"},
	{"+moveright", "Strafe Right", "Movement"},
	{"+jump", "Jump", "Movement"},
	{"+speed", "Run", "Movement"},

	{"+attack", "Fire", "Combat"},
	{"impulse 10", "Next Weapon", "Combat"},
	{"impulse 12", "Prev Weapon", "Combat"},

	{"impulse 1", "Axe", "Weapons"},
	{"impulse 2", "Shotgun", "Weapons"},
	{"impulse 3", "Super Shotgun", "Weapons"},
	{"impulse 4", "Nailgun", "Weapons"},
	{"impulse 5", "Super Nailgun", "Weapons"},
	{"impulse 6", "Grenade Launcher", "Weapons"},
	{"impulse 7", "Rocket Launcher", "Weapons"},
	{"impulse 8", "Thunderbolt", "Weapons"},
};

// Static member definitions
Rml::Context												 *CvarBindingManager::s_context = nullptr;
Rml::DataModelHandle										  CvarBindingManager::s_model_handle;
std::unordered_map<std::string, CvarBinding>				  CvarBindingManager::s_bindings;
std::unordered_map<std::string, std::unique_ptr<float>>		  CvarBindingManager::s_float_values;
std::unordered_map<std::string, std::unique_ptr<int>>		  CvarBindingManager::s_int_values;
std::unordered_map<std::string, std::unique_ptr<Rml::String>> CvarBindingManager::s_string_values;
bool														  CvarBindingManager::s_initialized = false;
ICvarProvider												 *CvarBindingManager::s_provider = nullptr;
bool														  CvarBindingManager::s_ignore_ui_changes = false;
int															  CvarBindingManager::s_ignore_ui_changes_frames = 0;

namespace
{

bool IsInvertMouseBinding (const std::string &ui_name)
{
	return ui_name == "invert_mouse";
}

int GetInvertMouseValue (ICvarProvider *provider)
{
	if (!provider)
		return 0;
	return provider->GetFloat ("m_pitch") < 0.0f ? 1 : 0;
}

void SetInvertMouseValue (ICvarProvider *provider, bool inverted)
{
	if (!provider)
		return;
	float pitch = provider->GetFloat ("m_pitch");
	float magnitude = std::fabs (pitch);
	if (magnitude < 0.0001f)
	{
		magnitude = 0.022f;
	}
	provider->SetFloat ("m_pitch", inverted ? -magnitude : magnitude);
}

// _cl_color packing helpers: packed = top*16 + bottom
bool IsPackedColorBinding (const std::string &ui_name)
{
	return ui_name == "cl_color_top" || ui_name == "cl_color_bottom";
}

int UnpackColorHalf (int packed, const std::string &ui_name)
{
	if (ui_name == "cl_color_top")
		return packed / 16;
	return packed % 16;
}

int RepackColor (int packed, int new_half, const std::string &ui_name)
{
	if (ui_name == "cl_color_top")
		return new_half * 16 + (packed % 16);
	return (packed / 16) * 16 + new_half;
}

} // namespace

void CvarBindingManager::SetProvider (ICvarProvider *provider)
{
	s_provider = provider;
}

ICvarProvider *CvarBindingManager::GetProvider ()
{
	// Return injected provider, or default to QuakeCvarProvider
	if (!s_provider)
	{
		s_provider = &QuakeCvarProvider::Instance ();
	}
	return s_provider;
}

bool CvarBindingManager::Initialize (Rml::Context *context)
{
	if (s_initialized)
	{
		Con_DPrintf ("CvarBindingManager: Already initialized\n");
		return true;
	}

	if (!context)
	{
		Con_Printf ("CvarBindingManager: ERROR - null context\n");
		return false;
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel ("cvars");
	if (!constructor)
	{
		Con_Printf ("CvarBindingManager: ERROR - Failed to create data model\n");
		return false;
	}

	s_model_handle = constructor.GetModelHandle ();
	s_context = context;
	s_initialized = true;

	RegisterAllBindings ();

	Con_DPrintf ("CvarBindingManager: Initialized with %zu bindings\n", s_bindings.size ());
	return true;
}

void CvarBindingManager::RegisterAllBindings ()
{
	// ── Float cvars (sliders with data-value + cvar_changed) ──

	// Graphics
	RegisterFloat ("fov", "fov", 50.0f, 130.0f, 5.0f);
	RegisterFloat ("gamma", "gamma", 0.5f, 2.0f, 0.05f);
	RegisterFloat ("contrast", "contrast", 0.5f, 2.0f, 0.05f);
	RegisterFloat ("host_maxfps", "max_fps", 30.0f, 1000.0f, 10.0f);

	// Sound
	RegisterFloat ("volume", "volume", 0.0f, 1.0f, 0.05f);
	RegisterFloat ("bgmvolume", "bgmvolume", 0.0f, 1.0f, 0.05f);

	// Game / Controls
	RegisterFloat ("sensitivity", "sensitivity", 1.0f, 20.0f, 0.5f);
	RegisterFloat ("scr_sbaralpha", "hud_opacity", 0.0f, 1.0f, 0.05f);
	RegisterFloat ("scr_uiscale", "ui_scale", 0.5f, 1.5f, 0.25f);
	RegisterFloat ("scr_fontscale", "font_scale", 0.75f, 2.0f, 0.05f);
	RegisterFloat ("cl_bob", "view_bob", 0.0f, 0.05f, 0.005f);
	RegisterFloat ("cl_rollangle", "view_roll", 0.0f, 5.0f, 0.5f);

	// ── Bool cvars (cycle_cvar toggles) ──

	// Graphics
	RegisterBool ("vid_fullscreen", "fullscreen");
	RegisterBool ("vid_vsync", "vsync");
	RegisterBool ("vid_palettize", "palettize");
	RegisterBool ("r_dynamic", "dynamic_lights");
	RegisterBool ("r_waterwarp", "underwater_fx");
	RegisterBool ("r_lerpmodels", "model_interpolation");

	// Game / Controls
	RegisterBool ("m_pitch", "invert_mouse"); // special: sign-based
	RegisterBool ("cl_alwaysrun", "always_run");
	RegisterBool ("m_filter", "m_filter");
	RegisterBool ("r_drawviewmodel", "show_gun");
	RegisterBool ("cl_startdemos", "startup_demos");

	// ── Enum cvars (cycle_cvar with display labels) ──

	// Graphics — texture quality (gl_picmip: 0=High, 1=Medium, 2=Low)
	{
		const char *labels[] = {"High", "Medium", "Low"};
		RegisterEnum ("gl_picmip", "texture_quality", 3, labels);
	}

	// Graphics — texture filter (vid_filter: 0=Smooth, 1=Classic)
	{
		const char *labels[] = {"Smooth", "Classic"};
		RegisterEnum ("vid_filter", "texture_filter", 2, labels);
	}

	// Graphics — anisotropy (vid_anisotropic: 1, 2, 4, 8, 16)
	{
		std::vector<int> values = {1, 2, 4, 8, 16};
		const char		*labels[] = {"Off", "2x", "4x", "8x", "16x"};
		RegisterEnumValues ("vid_anisotropic", "aniso", values, labels);
	}

	// Graphics — MSAA (vid_fsaa: 0, 2, 4, 8)
	{
		std::vector<int> values = {0, 2, 4, 8};
		const char		*labels[] = {"Off", "2x", "4x", "8x"};
		RegisterEnumValues ("vid_fsaa", "msaa", values, labels);
	}

	// Graphics — AA mode (vid_fsaamode: 0=Off, 1=FXAA, 2=TAA)
	{
		const char *labels[] = {"Off", "FXAA", "TAA"};
		RegisterEnum ("vid_fsaamode", "aa_mode", 3, labels);
	}

	// Graphics — render scale (r_scale: 1=Native, 2=2x, 4=4x)
	{
		std::vector<int> values = {1, 2, 4};
		const char		*labels[] = {"Native", "2x", "4x"};
		RegisterEnumValues ("r_scale", "render_scale", values, labels);
	}

	// Graphics — particles (r_particles: 0=Off, 1=Classic, 2=Enhanced)
	{
		const char *labels[] = {"Off", "Classic", "Enhanced"};
		RegisterEnum ("r_particles", "particles", 3, labels);
	}

	// Graphics — enhanced models (r_enhancedmodels)
	{
		const char *labels[] = {"Off", "On"};
		RegisterEnum ("r_enhancedmodels", "enhanced_models", 2, labels);
	}

	// Sound — quality (snd_mixspeed: common rates)
	{
		std::vector<int> values = {11025, 22050, 44100, 48000};
		const char		*labels[] = {"11 kHz", "22 kHz", "44 kHz", "48 kHz"};
		RegisterEnumValues ("snd_mixspeed", "sound_quality", values, labels);
	}

	// Sound — ambient (ambient_level mapped to off/low/medium/high)
	{
		const char *labels[] = {"Off", "On"};
		RegisterEnum ("ambient_level", "ambient", 2, labels);
	}

	// Game — crosshair (crosshair: 0=Off, 1=Cross, 2=Dot, 3=Circle, 4=Chevron)
	{
		const char *labels[] = {"Off", "Cross", "Dot", "Circle", "Chevron"};
		RegisterEnum ("crosshair", "crosshair", 5, labels);
	}

	// Game — show FPS (scr_showfps: 0=Off, 1=On)
	{
		const char *labels[] = {"Off", "On"};
		RegisterEnum ("scr_showfps", "show_fps", 2, labels);
	}

	// Game — aim assist (sv_aim: 0=Off, 1=On)
	{
		const char *labels[] = {"Off", "On"};
		RegisterEnum ("sv_aim", "sv_aim", 2, labels);
	}

	// Game — gun kick (v_gunkick: 0=Off, 1=Classic, 2=Smooth)
	{
		const char *labels[] = {"Off", "Classic", "Smooth"};
		RegisterEnum ("v_gunkick", "gun_kick", 3, labels);
	}

	// Game — auto fast load (autofastload: 0=Off, 1=On)
	{
		const char *labels[] = {"Off", "On"};
		RegisterEnum ("autofastload", "auto_load", 2, labels);
	}

	// Game — skill level (skill: 0=Easy, 1=Normal, 2=Hard, 3=Nightmare)
	{
		const char *labels[] = {"Easy", "Normal", "Hard", "Nightmare"};
		RegisterEnum ("skill", "skill", 4, labels);
	}

	// ── String cvars (text inputs) ──
	RegisterString ("_cl_name", "player_name");
	RegisterString ("hostname", "hostname");

	// Sound — extra toggles
	RegisterBool ("bgm_extmusic", "bgm_extmusic");
	RegisterBool ("snd_waterfx", "snd_waterfx");

	// ── Multiplayer game options ──

	// Game type (deathmatch: 0=Coop, 1=Deathmatch)
	{
		const char *labels[] = {"Cooperative", "Deathmatch"};
		RegisterEnum ("deathmatch", "deathmatch", 2, labels);
	}

	// Teamplay (teamplay: 0=Off, 1=No Friendly Fire, 2=Friendly Fire)
	{
		const char *labels[] = {"Off", "No Friendly Fire", "Friendly Fire"};
		RegisterEnum ("teamplay", "teamplay", 3, labels);
	}

	// Frag limit
	{
		std::vector<int> values = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
		const char		*labels[] = {"None", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100"};
		RegisterEnumValues ("fraglimit", "fraglimit", values, labels);
	}

	// Time limit (minutes)
	{
		std::vector<int> values = {0, 5, 10, 15, 20, 25, 30, 45, 60};
		const char		*labels[] = {"None", "5 min", "10 min", "15 min", "20 min", "25 min", "30 min", "45 min", "60 min"};
		RegisterEnumValues ("timelimit", "timelimit", values, labels);
	}

	// ── Computed labels for video mode picker (BindFunc, not backed by cvar) ──
	{
		Rml::DataModelConstructor constructor = s_context->GetDataModel ("cvars");
		if (constructor)
		{
			constructor.BindFunc (
				"vid_mode_label",
				[] (Rml::Variant &variant)
				{
					int w = static_cast<int> (GetProvider ()->GetFloat ("vid_width"));
					int h = static_cast<int> (GetProvider ()->GetFloat ("vid_height"));
					variant = Rml::String (std::to_string (w) + "x" + std::to_string (h));
				});
			constructor.BindFunc (
				"vid_rate_label",
				[] (Rml::Variant &variant)
				{
					int r = static_cast<int> (GetProvider ()->GetFloat ("vid_refreshrate"));
					variant = Rml::String (std::to_string (r) + " Hz");
				});
			constructor.BindFunc (
				"vid_fullscreen_label",
				[] (Rml::Variant &variant)
				{
					int fs = static_cast<int> (GetProvider ()->GetFloat ("vid_fullscreen"));
					switch (fs)
					{
					case 0:
						variant = Rml::String ("Windowed");
						break;
					case 1:
						variant = Rml::String ("Fullscreen");
						break;
					case 2:
						variant = Rml::String ("Exclusive");
						break;
					default:
						variant = Rml::String ("Unknown");
						break;
					}
				});
			constructor.BindFunc (
				"vid_vsync_label",
				[] (Rml::Variant &variant)
				{
					int vs = static_cast<int> (GetProvider ()->GetFloat ("vid_vsync"));
					switch (vs)
					{
					case 0:
						variant = Rml::String ("Off");
						break;
					case 1:
						variant = Rml::String ("On");
						break;
					case 2:
						variant = Rml::String ("Triple Buffer");
						break;
					default:
						variant = Rml::String ("Unknown");
						break;
					}
				});
		}
	}

	// ── cvar_changed event callback for data-event-change ──
	// Registered as a data model event so it fires AFTER DataControllerValue
	// has synced the element's new value into the backing store.  Using an
	// inline onchange handler instead would fire BEFORE the sync, causing
	// SyncFromUI to read the stale previous value.
	{
		Rml::DataModelConstructor constructor = s_context->GetDataModel ("cvars");
		if (constructor)
		{
			constructor.BindEventCallback (
				"cvar_changed",
				[] (Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &arguments)
				{
					if (arguments.empty ())
						return;
					Rml::String ui_name = arguments[0].Get<Rml::String> ();
					if (ui_name.empty () || ShouldIgnoreUIChange ())
						return;
					SyncFromUI (std::string (ui_name.c_str ()));
				});
		}
	}

	// Player setup — colors (_cl_color is a packed int, top = val/16, bottom = val%16)
	{
		std::vector<int> values = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
		const char *labels[] = {"White", "Brown", "Blue", "Green", "Red", "Gold", "Peach", "Purple", "Magenta", "Tan", "Grey", "Orange", "Yellow", "Olive"};
		RegisterEnumValues ("_cl_color", "cl_color_top", values, labels);
		RegisterEnumValues ("_cl_color", "cl_color_bottom", values, labels);
	}

	// ── Keybind data model for controls menu ──
	{
		Rml::DataModelConstructor constructor = s_context->GetDataModel ("cvars");
		if (constructor)
		{
			if (auto kb_handle = constructor.RegisterStruct<KeybindInfo> ())
			{
				kb_handle.RegisterMember ("action", &KeybindInfo::action);
				kb_handle.RegisterMember ("label", &KeybindInfo::label);
				kb_handle.RegisterMember ("key_name", &KeybindInfo::key_name);
				kb_handle.RegisterMember ("section", &KeybindInfo::section);
				kb_handle.RegisterMember ("is_header", &KeybindInfo::is_header);
				kb_handle.RegisterMember ("is_capturing", &KeybindInfo::is_capturing);
			}
			constructor.RegisterArray<std::vector<KeybindInfo>> ();
			constructor.Bind ("keybinds", &s_keybinds);

			constructor.BindEventCallback (
				"capture_key",
				[] (Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &arguments)
				{
					if (arguments.empty ())
						return;
					Rml::String action = arguments[0].Get<Rml::String> ();
					if (action.empty ())
						return;
					std::string cmd = "bind_key('" + std::string (action.c_str ()) + "')";
					MenuEventHandler_ProcessAction (cmd.c_str ());
				});
		}
	}

	// ── Video mode list for resolution picker ──
	{
		Rml::DataModelConstructor constructor = s_context->GetDataModel ("cvars");
		if (constructor)
		{
			if (auto mode_handle = constructor.RegisterStruct<VideoModeInfo> ())
			{
				mode_handle.RegisterMember ("width", &VideoModeInfo::width);
				mode_handle.RegisterMember ("height", &VideoModeInfo::height);
				mode_handle.RegisterMember ("label", &VideoModeInfo::label);
				mode_handle.RegisterMember ("is_current", &VideoModeInfo::is_current);
			}
			constructor.RegisterArray<std::vector<VideoModeInfo>> ();
			constructor.Bind ("video_modes", &s_video_modes);

			constructor.BindEventCallback (
				"select_resolution",
				[] (Rml::DataModelHandle, Rml::Event &event, const Rml::VariantList &arguments)
				{
					if (arguments.size () < 2)
						return;
					int w = arguments[0].Get<int> ();
					int h = arguments[1].Get<int> ();
					if (w <= 0 || h <= 0)
						return;

					Con_DPrintf ("CvarBinding: select_resolution %dx%d\n", w, h);

					// Set vid_width and vid_height cvars
					GetProvider ()->SetFloat ("vid_width", static_cast<float> (w));
					GetProvider ()->SetFloat ("vid_height", static_cast<float> (h));

					// Rebuild refresh rate list for the new resolution
					VID_Menu_RebuildRateList ();

					// Update is_current flags
					for (auto &mode : s_video_modes)
					{
						mode.is_current = (mode.width == w && mode.height == h);
					}

					// Dirty the model to refresh labels and mode list
					MarkDirty ();
				});
		}
	}
}

void CvarBindingManager::Shutdown ()
{
	if (!s_initialized)
		return;

	s_bindings.clear ();
	s_float_values.clear ();
	s_int_values.clear ();
	s_string_values.clear ();
	s_model_handle = Rml::DataModelHandle ();
	s_context = nullptr;
	s_initialized = false;
	s_ignore_ui_changes = false;
	s_ignore_ui_changes_frames = 0;

	Con_DPrintf ("CvarBindingManager: Shutdown\n");
}

// ── Common bind-or-update helpers ──────────────────────────────────

void CvarBindingManager::BindOrUpdateInt (const char *ui_name, int value)
{
	auto it = s_int_values.find (ui_name);
	if (it == s_int_values.end ())
	{
		auto value_ptr = std::make_unique<int> (value);
		int *raw_ptr = value_ptr.get ();
		s_int_values[ui_name] = std::move (value_ptr);

		if (s_context)
		{
			Rml::DataModelConstructor constructor = s_context->GetDataModel ("cvars");
			if (constructor && !constructor.Bind (ui_name, raw_ptr))
			{
				Con_Printf ("CvarBindingManager: ERROR - Failed to bind int '%s'\n", ui_name);
			}
		}
	}
	else if (it->second)
	{
		*(it->second) = value;
	}
}

void CvarBindingManager::BindOrUpdateFloat (const char *ui_name, float value)
{
	auto it = s_float_values.find (ui_name);
	if (it == s_float_values.end ())
	{
		auto   value_ptr = std::make_unique<float> (value);
		float *raw_ptr = value_ptr.get ();
		s_float_values[ui_name] = std::move (value_ptr);

		if (s_context)
		{
			Rml::DataModelConstructor constructor = s_context->GetDataModel ("cvars");
			if (constructor && !constructor.Bind (ui_name, raw_ptr))
			{
				Con_Printf ("CvarBindingManager: ERROR - Failed to bind float '%s'\n", ui_name);
			}
		}
	}
	else if (it->second)
	{
		*(it->second) = value;
	}
}

void CvarBindingManager::BindOrUpdateString (const char *ui_name, const Rml::String &value)
{
	auto it = s_string_values.find (ui_name);
	if (it == s_string_values.end ())
	{
		auto		 value_ptr = std::make_unique<Rml::String> (value);
		Rml::String *raw_ptr = value_ptr.get ();
		s_string_values[ui_name] = std::move (value_ptr);

		if (s_context)
		{
			Rml::DataModelConstructor constructor = s_context->GetDataModel ("cvars");
			if (constructor && !constructor.Bind (ui_name, raw_ptr))
			{
				Con_Printf ("CvarBindingManager: ERROR - Failed to bind string '%s'\n", ui_name);
			}
		}
	}
	else if (it->second)
	{
		*(it->second) = value;
	}
}

// ── Registration methods ──────────────────────────────────────────

void CvarBindingManager::RegisterFloat (const char *cvar, const char *ui_name, float min, float max, float step)
{
	if (!s_initialized)
		return;

	CvarBinding binding;
	binding.cvar_name = cvar;
	binding.ui_name = ui_name;
	binding.type = CvarType::Float;
	binding.min_value = min;
	binding.max_value = max;
	binding.step = step;

	s_bindings[ui_name] = binding;

	BindOrUpdateFloat (ui_name, GetProvider ()->GetFloat (cvar));

	Con_DPrintf ("CvarBindingManager: Registered float '%s' -> '%s' (%.2f-%.2f)\n", cvar, ui_name, min, max);
}

void CvarBindingManager::RegisterBool (const char *cvar, const char *ui_name)
{
	if (!s_initialized)
		return;

	CvarBinding binding;
	binding.cvar_name = cvar;
	binding.ui_name = ui_name;
	binding.type = CvarType::Bool;

	s_bindings[ui_name] = binding;

	int value = IsInvertMouseBinding (ui_name) ? GetInvertMouseValue (GetProvider ()) : static_cast<int> (GetProvider ()->GetFloat (cvar));
	BindOrUpdateInt (ui_name, value);

	Con_DPrintf ("CvarBindingManager: Registered bool '%s' -> '%s'\n", cvar, ui_name);
}

void CvarBindingManager::RegisterInt (const char *cvar, const char *ui_name, int min, int max)
{
	if (!s_initialized)
		return;

	CvarBinding binding;
	binding.cvar_name = cvar;
	binding.ui_name = ui_name;
	binding.type = CvarType::Int;
	binding.min_value = static_cast<float> (min);
	binding.max_value = static_cast<float> (max);

	s_bindings[ui_name] = binding;

	BindOrUpdateInt (ui_name, static_cast<int> (GetProvider ()->GetFloat (cvar)));

	Con_DPrintf ("CvarBindingManager: Registered int '%s' -> '%s' (%d-%d)\n", cvar, ui_name, min, max);
}

void CvarBindingManager::BindEnumLabel (const char *ui_name)
{
	auto it = s_bindings.find (ui_name);
	if (it == s_bindings.end () || it->second.enum_labels.empty ())
		return;

	std::string				  label_name = std::string (ui_name) + "_label";
	Rml::DataModelConstructor constructor = s_context->GetDataModel ("cvars");
	if (!constructor)
		return;

	std::string captured_ui_name = ui_name;
	constructor.BindFunc (
		label_name,
		[captured_ui_name] (Rml::Variant &variant)
		{
			auto it = s_bindings.find (captured_ui_name);
			if (it == s_bindings.end ())
			{
				variant = Rml::String ("");
				return;
			}
			const auto &b = it->second;
			int			current = 0;
			auto		val_it = s_int_values.find (captured_ui_name);
			if (val_it != s_int_values.end () && val_it->second)
				current = *(val_it->second);
			for (size_t i = 0; i < b.enum_values.size (); i++)
			{
				if (b.enum_values[i] == current && i < b.enum_labels.size ())
				{
					variant = Rml::String (b.enum_labels[i]);
					return;
				}
			}
			variant = Rml::String (std::to_string (current));
		});
}

void CvarBindingManager::RegisterEnum (const char *cvar, const char *ui_name, int num_values, const char **labels)
{
	if (!s_initialized)
		return;

	CvarBinding binding;
	binding.cvar_name = cvar;
	binding.ui_name = ui_name;
	binding.type = CvarType::Enum;
	binding.num_values = num_values;
	binding.min_value = 0.0f;
	binding.max_value = static_cast<float> (std::max (0, num_values - 1));
	binding.enum_values.reserve (num_values);
	for (int i = 0; i < num_values; i++)
	{
		binding.enum_values.push_back (i);
	}

	if (labels)
	{
		for (int i = 0; i < num_values; i++)
		{
			binding.enum_labels.push_back (labels[i]);
		}
	}

	s_bindings[ui_name] = binding;

	BindOrUpdateInt (ui_name, static_cast<int> (GetProvider ()->GetFloat (cvar)));
	BindEnumLabel (ui_name);

	Con_DPrintf ("CvarBindingManager: Registered enum '%s' -> '%s' (%d values)\n", cvar, ui_name, num_values);
}

void CvarBindingManager::RegisterEnumValues (const char *cvar, const char *ui_name, const std::vector<int> &values, const char **labels)
{
	if (!s_initialized)
		return;
	if (values.empty ())
	{
		Con_Printf ("CvarBindingManager: ERROR - Enum values list is empty for '%s'\n", ui_name);
		return;
	}

	CvarBinding binding;
	binding.cvar_name = cvar;
	binding.ui_name = ui_name;
	binding.type = CvarType::Enum;
	binding.num_values = static_cast<int> (values.size ());
	binding.enum_values = values;

	auto minmax = std::minmax_element (values.begin (), values.end ());
	binding.min_value = static_cast<float> (*minmax.first);
	binding.max_value = static_cast<float> (*minmax.second);

	if (labels)
	{
		for (int i = 0; i < binding.num_values; i++)
		{
			binding.enum_labels.push_back (labels[i]);
		}
	}

	s_bindings[ui_name] = binding;

	BindOrUpdateInt (ui_name, static_cast<int> (GetProvider ()->GetFloat (cvar)));
	BindEnumLabel (ui_name);

	Con_DPrintf ("CvarBindingManager: Registered enum values '%s' -> '%s' (%d values)\n", cvar, ui_name, binding.num_values);
}

void CvarBindingManager::RegisterString (const char *cvar, const char *ui_name)
{
	if (!s_initialized)
		return;

	CvarBinding binding;
	binding.cvar_name = cvar;
	binding.ui_name = ui_name;
	binding.type = CvarType::String;

	s_bindings[ui_name] = binding;

	BindOrUpdateString (ui_name, GetProvider ()->GetString (cvar));

	Con_DPrintf ("CvarBindingManager: Registered string '%s' -> '%s'\n", cvar, ui_name);
}

void CvarBindingManager::SyncToUI ()
{
	if (!s_initialized)
		return;

	// Suppress UI change handling for the next two update ticks. Data binding
	// updates emit "change" events when dirty values are pushed to elements.
	// SyncToUI is often called from within g_context->Update() (via event
	// processing), so the dirty data may not be processed until the NEXT
	// Update() call — requiring two frames of suppression.
	s_ignore_ui_changes = true;
	s_ignore_ui_changes_frames = 2;

	for (auto &pair : s_bindings)
	{
		const CvarBinding &binding = pair.second;
		const char		  *cvar_name = binding.cvar_name.c_str ();

		switch (binding.type)
		{
		case CvarType::Float:
			if (auto it = s_float_values.find (binding.ui_name); it != s_float_values.end () && it->second)
			{
				*(it->second) = GetProvider ()->GetFloat (cvar_name);
			}
			break;
		case CvarType::Bool:
		case CvarType::Int:
		case CvarType::Enum:
			if (auto it = s_int_values.find (binding.ui_name); it != s_int_values.end () && it->second)
			{
				if (IsInvertMouseBinding (binding.ui_name))
				{
					*(it->second) = GetInvertMouseValue (GetProvider ());
				}
				else if (IsPackedColorBinding (binding.ui_name))
				{
					int packed = static_cast<int> (GetProvider ()->GetFloat (cvar_name));
					*(it->second) = UnpackColorHalf (packed, binding.ui_name);
				}
				else
				{
					*(it->second) = static_cast<int> (GetProvider ()->GetFloat (cvar_name));
				}
			}
			break;
		case CvarType::String:
		{
			if (auto it = s_string_values.find (binding.ui_name); it != s_string_values.end () && it->second)
			{
				*(it->second) = GetProvider ()->GetString (cvar_name);
			}
			break;
		}
		}
	}

	MarkDirty ();
	Con_DPrintf ("CvarBindingManager: Synced %zu cvars to UI\n", s_bindings.size ());
}

bool CvarBindingManager::ShouldIgnoreUIChange ()
{
	return s_ignore_ui_changes;
}

void CvarBindingManager::NotifyUIUpdateComplete ()
{
	if (s_ignore_ui_changes_frames > 0)
	{
		s_ignore_ui_changes_frames--;
		if (s_ignore_ui_changes_frames == 0)
		{
			s_ignore_ui_changes = false;
		}
	}
}

void CvarBindingManager::SyncFromUI (const std::string &ui_name)
{
	if (!s_initialized)
		return;

	auto it = s_bindings.find (ui_name);
	if (it == s_bindings.end ())
	{
		Con_Printf ("CvarBindingManager: Unknown UI binding '%s'\n", ui_name.c_str ());
		return;
	}

	const CvarBinding &binding = it->second;
	const char		  *cvar_name = binding.cvar_name.c_str ();

	switch (binding.type)
	{
	case CvarType::Float:
	{
		auto val_it = s_float_values.find (ui_name);
		if (val_it != s_float_values.end () && val_it->second)
		{
			GetProvider ()->SetFloat (cvar_name, *(val_it->second));
		}
		break;
	}
	case CvarType::Bool:
	case CvarType::Int:
	case CvarType::Enum:
	{
		auto val_it = s_int_values.find (ui_name);
		if (val_it != s_int_values.end () && val_it->second)
		{
			if (IsInvertMouseBinding (binding.ui_name))
			{
				SetInvertMouseValue (GetProvider (), *(val_it->second) != 0);
			}
			else if (IsPackedColorBinding (ui_name))
			{
				int packed = static_cast<int> (GetProvider ()->GetFloat (cvar_name));
				int new_packed = RepackColor (packed, *(val_it->second), ui_name);
				GetProvider ()->SetFloat (cvar_name, static_cast<float> (new_packed));
			}
			else
			{
				GetProvider ()->SetFloat (cvar_name, static_cast<float> (*(val_it->second)));
			}
		}
		break;
	}
	case CvarType::String:
	{
		auto val_it = s_string_values.find (ui_name);
		if (val_it != s_string_values.end () && val_it->second)
		{
			GetProvider ()->SetString (cvar_name, val_it->second->c_str ());
		}
		break;
	}
	}
}

void CvarBindingManager::SyncAllFromUI ()
{
	if (!s_initialized)
		return;

	for (auto &pair : s_bindings)
	{
		SyncFromUI (pair.first);
	}
}

const CvarBinding *CvarBindingManager::GetBinding (const std::string &ui_name)
{
	auto it = s_bindings.find (ui_name);
	if (it != s_bindings.end ())
	{
		return &it->second;
	}
	return nullptr;
}

void CvarBindingManager::MarkDirty ()
{
	if (s_initialized && s_model_handle)
	{
		s_model_handle.DirtyAllVariables ();
	}
}

bool CvarBindingManager::IsInitialized ()
{
	return s_initialized;
}

float CvarBindingManager::GetFloatValue (const std::string &ui_name)
{
	auto it = s_float_values.find (ui_name);
	if (it != s_float_values.end () && it->second)
	{
		return *(it->second);
	}
	return 0.0f;
}

void CvarBindingManager::SetFloatValue (const std::string &ui_name, float value)
{
	auto binding = GetBinding (ui_name);
	if (binding)
	{
		// Clamp to range
		if (value < binding->min_value)
			value = binding->min_value;
		if (value > binding->max_value)
			value = binding->max_value;
	}
	auto it = s_float_values.find (ui_name);
	if (it != s_float_values.end () && it->second)
	{
		*(it->second) = value;
	}
	SyncFromUI (ui_name);
	MarkDirty ();
}

bool CvarBindingManager::GetBoolValue (const std::string &ui_name)
{
	auto it = s_int_values.find (ui_name);
	if (it != s_int_values.end () && it->second)
	{
		return *(it->second) != 0;
	}
	return false;
}

void CvarBindingManager::SetBoolValue (const std::string &ui_name, bool value)
{
	auto it = s_int_values.find (ui_name);
	if (it != s_int_values.end () && it->second)
	{
		*(it->second) = value ? 1 : 0;
	}
	SyncFromUI (ui_name);
	MarkDirty ();
}

int CvarBindingManager::GetIntValue (const std::string &ui_name)
{
	auto it = s_int_values.find (ui_name);
	if (it != s_int_values.end () && it->second)
	{
		return *(it->second);
	}
	return 0;
}

void CvarBindingManager::SetIntValue (const std::string &ui_name, int value)
{
	auto binding = GetBinding (ui_name);
	if (binding && binding->type == CvarType::Int)
	{
		// Clamp to range
		int min_val = static_cast<int> (binding->min_value);
		int max_val = static_cast<int> (binding->max_value);
		if (value < min_val)
			value = min_val;
		if (value > max_val)
			value = max_val;
	}
	auto it = s_int_values.find (ui_name);
	if (it != s_int_values.end () && it->second)
	{
		*(it->second) = value;
	}
	SyncFromUI (ui_name);
	MarkDirty ();
}

Rml::String CvarBindingManager::GetStringValue (const std::string &ui_name)
{
	auto it = s_string_values.find (ui_name);
	if (it != s_string_values.end () && it->second)
	{
		return *(it->second);
	}
	return "";
}

void CvarBindingManager::SetStringValue (const std::string &ui_name, const Rml::String &value)
{
	auto it = s_string_values.find (ui_name);
	if (it != s_string_values.end () && it->second)
	{
		*(it->second) = value;
	}
	SyncFromUI (ui_name);
	MarkDirty ();
}

void CvarBindingManager::CycleEnum (const std::string &ui_name, int delta)
{
	auto binding = GetBinding (ui_name);
	if (!binding)
		return;

	if (binding->type == CvarType::Bool)
	{
		SetBoolValue (ui_name, !GetBoolValue (ui_name));
		return;
	}

	if (binding->type != CvarType::Enum)
		return;

	if (binding->enum_values.empty ())
		return;

	int	   current = GetIntValue (ui_name);
	size_t index = 0;
	bool   found = false;
	for (size_t i = 0; i < binding->enum_values.size (); i++)
	{
		if (binding->enum_values[i] == current)
		{
			index = i;
			found = true;
			break;
		}
	}

	if (!found)
	{
		index = 0;
	}

	int size = static_cast<int> (binding->enum_values.size ());
	int new_index = (static_cast<int> (index) + delta) % size;
	if (new_index < 0)
		new_index += size;

	SetIntValue (ui_name, binding->enum_values[static_cast<size_t> (new_index)]);
}

void CvarBindingManager::SyncKeybinds ()
{
	if (!s_initialized)
		return;

	s_keybinds.clear ();

	std::string		 last_section;
	constexpr size_t num_defs = sizeof (kKeybindDefs) / sizeof (kKeybindDefs[0]);

	for (size_t i = 0; i < num_defs; i++)
	{
		const auto &def = kKeybindDefs[i];

		// Insert section header when section changes
		if (def.section != last_section)
		{
			KeybindInfo header;
			header.label = def.section;
			header.section = def.section;
			header.is_header = true;
			s_keybinds.push_back (std::move (header));
			last_section = def.section;
		}

		KeybindInfo info;
		info.action = def.action;
		info.label = def.label;
		info.section = def.section;

		const char *bound = Key_FindBinding (def.action);
		info.key_name = bound ? bound : "---";

		s_keybinds.push_back (std::move (info));
	}

	MarkDirty ();
	Con_DPrintf ("CvarBindingManager: Synced %zu keybinds\n", s_keybinds.size ());
}

void CvarBindingManager::UpdateKeybind (const std::string &action, const std::string &key_name)
{
	if (!s_initialized)
		return;

	for (auto &kb : s_keybinds)
	{
		if (!kb.is_header && kb.action == action)
		{
			kb.key_name = key_name;
			kb.is_capturing = false;
			break;
		}
	}

	MarkDirty ();
}

void CvarBindingManager::SetCapturing (const std::string &action)
{
	if (!s_initialized)
		return;

	for (auto &kb : s_keybinds)
	{
		if (kb.is_header)
			continue;
		kb.is_capturing = (kb.action == action);
	}

	MarkDirty ();
}

void CvarBindingManager::ClearCapturing ()
{
	if (!s_initialized)
		return;

	for (auto &kb : s_keybinds)
	{
		kb.is_capturing = false;
	}

	MarkDirty ();
}

void CvarBindingManager::SyncVideoModes (const ui_video_mode_t *modes, int count)
{
	if (!s_initialized || !modes)
		return;

	s_video_modes.clear ();
	s_video_modes.reserve (count);

	for (int i = 0; i < count; i++)
	{
		VideoModeInfo info;
		info.width = modes[i].width;
		info.height = modes[i].height;
		info.label = std::to_string (modes[i].width) + "x" + std::to_string (modes[i].height);
		info.is_current = modes[i].is_current != 0;
		s_video_modes.push_back (std::move (info));
	}

	MarkDirty ();
	Con_DPrintf ("CvarBindingManager: Synced %d video modes\n", count);
}

} // namespace QRmlUI

// C API Implementation
extern "C"
{

	int CvarBinding_Init (void)
	{
		// Deferred initialization - called after context is ready
		return 1;
	}

	void CvarBinding_Shutdown (void)
	{
		QRmlUI::CvarBindingManager::Shutdown ();
	}

	void CvarBinding_RegisterFloat (const char *cvar, const char *ui_name, float min, float max, float step)
	{
		QRmlUI::CvarBindingManager::RegisterFloat (cvar, ui_name, min, max, step);
	}

	void CvarBinding_RegisterBool (const char *cvar, const char *ui_name)
	{
		QRmlUI::CvarBindingManager::RegisterBool (cvar, ui_name);
	}

	void CvarBinding_RegisterInt (const char *cvar, const char *ui_name, int min, int max)
	{
		QRmlUI::CvarBindingManager::RegisterInt (cvar, ui_name, min, max);
	}

	void CvarBinding_RegisterEnum (const char *cvar, const char *ui_name, int num_values)
	{
		QRmlUI::CvarBindingManager::RegisterEnum (cvar, ui_name, num_values, nullptr);
	}

	void CvarBinding_RegisterString (const char *cvar, const char *ui_name)
	{
		QRmlUI::CvarBindingManager::RegisterString (cvar, ui_name);
	}

	void CvarBinding_SyncToUI (void)
	{
		QRmlUI::CvarBindingManager::SyncToUI ();
	}

	void CvarBinding_SyncFromUI (const char *ui_name)
	{
		if (ui_name)
		{
			QRmlUI::CvarBindingManager::SyncFromUI (ui_name);
		}
	}

	void CvarBinding_CycleEnum (const char *ui_name, int delta)
	{
		if (ui_name)
		{
			QRmlUI::CvarBindingManager::CycleEnum (ui_name, delta);
		}
	}

} // extern "C"
