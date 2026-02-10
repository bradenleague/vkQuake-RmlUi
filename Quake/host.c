/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// host.c -- coordinates spawning and killing of local servers

#include "quakedef.h"
#include "bgmusic.h"
#include "tasks.h"
#include <setjmp.h>
#ifdef _DEBUG
#include "gl_heap.h"
#endif

#ifdef USE_RMLUI
#include "ui_manager.h"

/* Read-only indicator that RmlUI is compiled in */
cvar_t ui_use_rmlui = {"ui_use_rmlui", "1", CVAR_ARCHIVE};

#define UI_STARTUP_SETTLE_SECS 0.8 /* total time startup suppression is active */
#define UI_STARTUP_FADE_SECS   0.3 /* tail fade-out duration within settle window */
#define UI_AUTO_MENU_DELAY	   0.1 /* seconds after init/game-change before auto-detect */

typedef enum
{
	STARTUP_IDLE,		/* no menu pending */
	STARTUP_SETTLING,	/* waiting for engine to finish loading */
	STARTUP_READY,		/* settle done, waiting for show conditions */
	STARTUP_SHOWN,		/* menu has been opened */
	STARTUP_AUTO_DETECT /* deferred: checking if idle after game change */
} ui_startup_phase_t;

static struct
{
	ui_startup_phase_t phase;
	double			   settle_until;	  /* realtime when settle/fade expires */
	double			   pending_since;	  /* realtime when show was first queued */
	double			   auto_detect_after; /* earliest realtime for auto-detect */
} ui_startup = {STARTUP_IDLE, 0.0, 0.0, 0.0};

static void UI_CloseMenu_f (void);
static void UI_ShowWhenReady_f (void);
static void UI_TickStartup (void);
int			UI_IsMainMenuStartupPending (void);
double		UI_StartupBlackoutAlpha (void);
static void UI_UseRmluiChanged_f (cvar_t *var);

static qboolean UI_IsEnabled (void)
{
	return ui_use_rmlui.value != 0.0;
}

/* Console commands for RmlUI */
static void UI_Toggle_f (void)
{
	/* Toggle the menu stack (more useful than toggling visibility with no menus open). */
	if (UI_WantsMenuInput ())
		UI_CloseMenu_f ();
	else
		UI_PushMenu ("ui/rml/menus/main_menu.rml");
}

static void UI_Show_f (void)
{
	/* Immediate show (manual command behavior). */
	ui_startup.phase = STARTUP_IDLE;
	ui_startup.pending_since = 0.0;
	ui_startup.settle_until = 0.0;
	UI_PushMenu ("ui/rml/menus/main_menu.rml");
}

static void UI_Hide_f (void)
{
	/* Alias for closing menus. Don't force-hide if HUD is visible. */
	ui_startup.phase = STARTUP_IDLE;
	ui_startup.pending_since = 0.0;
	ui_startup.settle_until = 0.0;
	UI_CloseMenu_f ();
}

static void UI_Debugger_f (void)
{
	UI_ToggleDebugger ();
	Con_Printf ("RmlUI debugger toggled\n");
}

static void UI_Reload_f (void)
{
	UI_ReloadDocuments ();
}

static void UI_ReloadCSS_f (void)
{
	UI_ReloadStyleSheets ();
}

static void UI_LuaTest_f (void)
{
	UI_RunLuaTests ();
}

/* Open an RmlUI menu - sets key_dest and captures mouse */
static void UI_Menu_f (void)
{
	const char *menu_path;

	if (Cmd_Argc () < 2)
		menu_path = "ui/rml/menus/main_menu.rml"; /* Default menu */
	else
		menu_path = Cmd_Argv (1);

	/* Push the menu onto RmlUI's stack */
	UI_PushMenu (menu_path);
}

/* Close all RmlUI menus and return to game */
static void UI_CloseMenu_f (void)
{
	ui_startup.phase = STARTUP_IDLE;
	ui_startup.pending_since = 0.0;
	ui_startup.settle_until = 0.0;

	/* Tear down the menu stack synchronously */
	UI_CloseAllMenusImmediate ();
	if (!UI_IsHUDVisible ())
		UI_SetVisible (0);
}

static qboolean UI_IsMainMenuShowReady (void)
{
	/* If disconnected with no world loaded (e.g. mod with no startdemos, or
	 * demos that failed to load), there's nothing to wait for. */
	if (cls.state != ca_connected && !cl.worldmodel && !scr_disabled_for_loading)
		return true;

	/* Wait for gameplay view AND for the startup console to finish retracting. */
	return cls.state == ca_connected && cls.signon == SIGNONS && cl.worldmodel && !scr_disabled_for_loading && !con_forcedup && scr_con_current <= 1.0f;
}

static void UI_OpenMainMenuNow (void)
{
	ui_startup.settle_until = realtime + UI_STARTUP_SETTLE_SECS;
	ui_startup.phase = STARTUP_SHOWN;
	ui_startup.pending_since = 0.0;
	UI_SetStartupMenuEnter ();
	UI_PushMenu ("ui/rml/menus/main_menu.rml");
}

static void UI_ShowWhenReady_f (void)
{
	/* Preload to avoid first-show layout/style jitter when it finally appears. */
	UI_LoadDocument ("ui/rml/menus/main_menu.rml");

	if (UI_IsMainMenuShowReady ())
	{
		UI_OpenMainMenuNow ();
		return;
	}

	ui_startup.phase = STARTUP_SETTLING;
	if (ui_startup.pending_since <= 0.0)
		ui_startup.pending_since = realtime;
	Con_DPrintf ("UI_ShowWhenReady_f: deferring main menu until startup load/console settle\n");
}

/* Central state machine tick — called once per frame from Host_Frame.
 * Replaces UI_TryOpenPendingMainMenu + UI_AutoShowMainMenuIfIdle. */
static void UI_TickStartup (void)
{
	if (!UI_IsEnabled ())
	{
		ui_startup.phase = STARTUP_IDLE;
		ui_startup.settle_until = 0.0;
		ui_startup.pending_since = 0.0;
		ui_startup.auto_detect_after = 0.0;
		return;
	}

	switch (ui_startup.phase)
	{
	case STARTUP_IDLE:
	case STARTUP_SHOWN:
		return;

	case STARTUP_SETTLING:
		if (UI_IsMainMenuShowReady ())
		{
			UI_OpenMainMenuNow ();
			return;
		}
		/* Safety valve so a failed/missing demo doesn't block menu forever. */
		if (ui_startup.pending_since > 0.0 && realtime - ui_startup.pending_since > 10.0 && !scr_disabled_for_loading)
		{
			Con_DPrintf ("UI_TickStartup: timed out waiting, opening menu anyway\n");
			UI_OpenMainMenuNow ();
		}
		return;

	case STARTUP_READY:
		/* Reserved for future use (e.g. explicit "ready" signal). */
		if (UI_IsMainMenuShowReady ())
			UI_OpenMainMenuNow ();
		return;

	case STARTUP_AUTO_DETECT:
		if (realtime < ui_startup.auto_detect_after)
			return; /* let startup / game-change commands settle */

		if (UI_WantsMenuInput ())
		{
			ui_startup.phase = STARTUP_IDLE;
			return; /* menu already open */
		}
		if (sv.active && !cls.demoplayback)
		{
			ui_startup.phase = STARTUP_IDLE;
			return; /* hosting a real server — don't auto-show menu */
		}

		/* Queue deferred show.  UI_IsMainMenuShowReady() handles both the
		 * idle-at-console case (returns true immediately) and the demo-
		 * playback case (waits for signon + console retract). */
		Con_DPrintf ("UI_TickStartup: auto-detect queuing menu show\n");
		ui_startup.phase = STARTUP_SETTLING;
		ui_startup.pending_since = realtime;
		return;
	}
}

/* Called from COM_Game_f when the game directory changes so the auto-detect
 * can re-evaluate after the new mod's quake.rc runs. */
void UI_NotifyGameChanged (void)
{
	if (!UI_IsEnabled ())
	{
		ui_startup.phase = STARTUP_IDLE;
		ui_startup.settle_until = 0.0;
		ui_startup.pending_since = 0.0;
		ui_startup.auto_detect_after = 0.0;
		return;
	}

	/* Flush cached documents so the file interface picks up the new mod's
	 * UI files (or falls back to base) on the next load. */
	UI_CloseAllMenusImmediate ();
	UI_ReloadDocuments ();

	ui_startup.phase = STARTUP_AUTO_DETECT;
	ui_startup.auto_detect_after = realtime + UI_AUTO_MENU_DELAY;
}

int UI_IsMainMenuStartupPending (void)
{
	if (!UI_IsEnabled ())
		return 0;
	if (ui_startup.phase == STARTUP_SETTLING || ui_startup.phase == STARTUP_READY)
		return 1;
	if (ui_startup.settle_until > 0.0 && realtime < ui_startup.settle_until)
		return 1;
	return 0;
}

double UI_StartupBlackoutAlpha (void)
{
	if (!UI_IsEnabled ())
		return 0.0;
	if (ui_startup.phase == STARTUP_SETTLING || ui_startup.phase == STARTUP_READY)
		return 1.0;
	if (ui_startup.settle_until <= 0.0 || realtime >= ui_startup.settle_until)
		return 0.0;
	double remaining = ui_startup.settle_until - realtime;
	if (remaining > UI_STARTUP_FADE_SECS)
		return 1.0;							 /* solid phase — entrance animation still playing */
	return remaining / UI_STARTUP_FADE_SECS; /* linear fade-out */
}

static void UI_UseRmluiChanged_f (cvar_t *var)
{
	if (var->value != 0.0)
	{
		ui_startup.phase = STARTUP_AUTO_DETECT;
		ui_startup.auto_detect_after = realtime + UI_AUTO_MENU_DELAY;
		return;
	}

	ui_startup.phase = STARTUP_IDLE;
	ui_startup.settle_until = 0.0;
	ui_startup.pending_since = 0.0;
	ui_startup.auto_detect_after = 0.0;

	UI_CloseAllMenusImmediate ();
	UI_SetVisible (0);

	if (key_dest == key_menu)
	{
		IN_Activate ();
		key_dest = key_game;
	}
}
#endif

/*

A server can allways be started, even if the system started out as a client
to a remote system.

A client can NOT be started if the system started as a dedicated server.

Memory is cleared / released when a server or client begins, not when they end.

*/

quakeparms_t *host_parms;

qboolean host_initialized; // true if into command execution

double host_frametime;
double realtime;	// without any filtering or bounding
double oldrealtime; // last frame run

int host_framecount;

int minimum_memory;

client_t *host_client; // current client

jmp_buf host_abortserver;
jmp_buf screen_error;

byte  *host_colormap;
float  host_netinterval = 1.0 / HOST_NETITERVAL_FREQ;
cvar_t host_framerate = {"host_framerate", "0", CVAR_NONE}; // set for slow motion
cvar_t host_speeds = {"host_speeds", "0", CVAR_NONE};		// set for running times
cvar_t host_maxfps = {"host_maxfps", "200", CVAR_ARCHIVE};	// johnfitz

cvar_t host_phys_max_ticrate = {"host_phys_max_ticrate", "0", CVAR_NONE}; // vso = [0 = disabled; MAX_PHYSICS_FREQ]

cvar_t host_timescale = {"host_timescale", "0", CVAR_NONE}; // johnfitz
cvar_t max_edicts = {"max_edicts", "32000", CVAR_NONE};		// vso -- changed from 8192 to 32000 = MAX_EDICTS, because there is no performance impact to do so
cvar_t cl_nocsqc = {"cl_nocsqc", "0", CVAR_NONE};			// spike -- blocks the loading of any csqc modules

cvar_t sys_ticrate = {"sys_ticrate", "0.025", CVAR_NONE}; // dedicated server
cvar_t serverprofile = {"serverprofile", "0", CVAR_NONE};

cvar_t fraglimit = {"fraglimit", "0", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t timelimit = {"timelimit", "0", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t teamplay = {"teamplay", "0", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t samelevel = {"samelevel", "0", CVAR_NONE};
cvar_t noexit = {"noexit", "0", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t skill = {"skill", "1", CVAR_NONE};			// 0 - 3
cvar_t deathmatch = {"deathmatch", "0", CVAR_NONE}; // 0, 1, or 2
cvar_t coop = {"coop", "0", CVAR_NONE};				// 0 or 1

cvar_t pausable = {"pausable", "1", CVAR_NONE};

cvar_t autoload = {"autoload", "1", CVAR_ARCHIVE};
cvar_t autofastload = {"autofastload", "0", CVAR_ARCHIVE};

cvar_t developer = {"developer", "0", CVAR_NONE};

static cvar_t pr_engine = {"pr_engine", ENGINE_NAME_AND_VER, CVAR_NONE};
cvar_t		  temp1 = {"temp1", "0", CVAR_NONE};

cvar_t devstats = {"devstats", "0", CVAR_NONE}; // johnfitz -- track developer statistics that vary every frame

cvar_t campaign = {"campaign", "0", CVAR_NONE};	  // for the 2021 rerelease
cvar_t horde = {"horde", "0", CVAR_NONE};		  // for the 2021 rerelease
cvar_t sv_cheats = {"sv_cheats", "0", CVAR_NONE}; // for the 2021 rerelease

devstats_t		dev_stats, dev_peakstats;
overflowtimes_t dev_overflows; // this stores the last time overflow messages were displayed, not the last time overflows occured

/*
================
Max_Edicts_f -- johnfitz
================
*/
static void Max_Edicts_f (cvar_t *var)
{
	// TODO: clamp it here?
	if (cls.state == ca_connected || sv.active)
		Con_Printf ("Changes to max_edicts will not take effect until the next time a map is loaded.\n");
}

// forward declarations for below...
static void Max_Fps_f (cvar_t *var);
static void Phys_Ticrate_f (cvar_t *var);

/*
================
Max_Fps_f -- ericw
================
*/
static void Max_Fps_f (cvar_t *var)
{
	// host_phys_max_ticrate overrides normal behaviour
	if (host_phys_max_ticrate.value > 0)
	{
		Phys_Ticrate_f (&host_phys_max_ticrate);
		return;
	}

	if (var->value > MAX_PHYSICS_FREQ || var->value <= 0)
	{
		if (!host_netinterval)
			Con_Printf ("Using renderer/network isolation.\n");
		host_netinterval = 1.0 / HOST_NETITERVAL_FREQ;
	}
	else
	{
		if (host_netinterval)
			Con_Printf ("Disabling renderer/network isolation.\n");
		host_netinterval = 0;

		if (var->value > MAX_PHYSICS_FREQ)
			Con_Warning ("host_maxfps above 72 breaks physics.\n");
	}
}

/*
================
Phys_Ticrate_f -- vso
================
*/
static void Phys_Ticrate_f (cvar_t *var)
{
	if (var->value > 0)
	{
		// clamp within valid limits, authorize float values
		var->value = CLAMP (0.0, var->value, MAX_PHYSICS_FREQ);

		Con_Printf ("Using max physics tics rate = %dHz.\n", (int)var->value);
		host_netinterval = 1.0 / var->value;
	}
	else
	{
		Con_Printf ("Disable max physics tics rate, using host_maxfps control...\n");
		// apply max_fps policy
		Max_Fps_f (&host_maxfps);
	}
}

/*
================
Host_EndGame
================
*/
void Host_EndGame (const char *message, ...)
{
	va_list argptr;
	char	string[1024];

	va_start (argptr, message);
	q_vsnprintf (string, sizeof (string), message, argptr);
	va_end (argptr);
	Con_DPrintf ("Host_EndGame: %s\n", string);

	PR_SwitchQCVM (NULL);

	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.state == ca_dedicated)
		Sys_Error ("Host_EndGame: %s\n", string); // dedicated servers exit

	Con_Printf ("Host_EndGame: demonum=%d, timedemo=%d\n", cls.demonum, cls.timedemo);
	if (cls.demonum != -1 && !cls.timedemo)
		CL_NextDemo ();
	else
		CL_Disconnect ();

	longjmp (host_abortserver, 1);
}

/*
================
Host_Error

This shuts down both the client and server
================
*/
void Host_Error (const char *error, ...)
{
	va_list			argptr;
	char			string[1024];
	static qboolean inerror = false;

	if (inerror)
		Sys_Error ("Host_Error: recursively entered");
	inerror = true;

	PR_SwitchQCVM (NULL);

	SCR_EndLoadingPlaque (); // reenable screen updates

	va_start (argptr, error);
	q_vsnprintf (string, sizeof (string), error, argptr);
	va_end (argptr);
	Con_Printf ("Host_Error: %s\n", string);

	if (cl.qcvm.extfuncs.CSQC_DrawHud && in_update_screen)
	{
		inerror = false;
		longjmp (screen_error, 1);
	}

	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.state == ca_dedicated)
		Sys_Error ("Host_Error: %s\n", string); // dedicated servers exit

	CL_Disconnect ();
	cls.demonum = -1;
	cl.intermission = 0; // johnfitz -- for errors during intermissions (changelevel with no map found, etc.)

	inerror = false;

	longjmp (host_abortserver, 1);
}

/*
================
Host_FindMaxClients
================
*/
void Host_FindMaxClients (void)
{
	int i;

	svs.maxclients = 1;

	i = COM_CheckParm ("-dedicated");
	if (i)
	{
		cls.state = ca_dedicated;
		if (i != (com_argc - 1))
		{
			svs.maxclients = atoi (com_argv[i + 1]);
		}
		else
			svs.maxclients = 8;
	}
	else
		cls.state = ca_disconnected;

	i = COM_CheckParm ("-listen");
	if (i)
	{
		if (cls.state == ca_dedicated)
			Sys_Error ("Only one of -dedicated or -listen can be specified");
		if (i != (com_argc - 1))
			svs.maxclients = atoi (com_argv[i + 1]);
		else
			svs.maxclients = 8;
	}
	if (svs.maxclients < 1)
		svs.maxclients = 8;
	else if (svs.maxclients > MAX_SCOREBOARD)
		svs.maxclients = MAX_SCOREBOARD;

	svs.maxclientslimit = MAX_SCOREBOARD;
	svs.clients = (struct client_s *)Mem_Alloc (svs.maxclientslimit * sizeof (client_t));

	if (svs.maxclients > 1)
		Cvar_SetQuick (&deathmatch, "1");
	else
		Cvar_SetQuick (&deathmatch, "0");
}

void Host_Version_f (void)
{
	Con_Printf ("Quake Version %1.2f\n", VERSION);
	Con_Printf ("QuakeSpasm Version " QUAKESPASM_VER_STRING "\n");
	Con_Printf ("vkQuake Version " ENGINE_NAME_AND_VER "\n");
	// clang-format off
	Con_Printf ("Exe: " __TIME__ " " __DATE__ "\n");
	// clang-format on
}

/* cvar callback functions : */
void Host_Callback_Notify (cvar_t *var)
{
	if (sv.active)
		SV_BroadcastPrintf ("\"%s\" changed to \"%s\"\n", var->name, var->string);
}

/*
=======================
Host_InitLocal
======================
*/
void Host_InitLocal (void)
{
	Cmd_AddCommand ("version", Host_Version_f);

	Host_InitCommands ();

	Cvar_RegisterVariable (&pr_engine);
	Cvar_RegisterVariable (&host_framerate);
	Cvar_RegisterVariable (&host_speeds);
	Cvar_RegisterVariable (&host_maxfps); // johnfitz
	Cvar_SetCallback (&host_maxfps, Max_Fps_f);
	Cvar_RegisterVariable (&host_phys_max_ticrate); // vso
	Cvar_SetCallback (&host_phys_max_ticrate, Phys_Ticrate_f);
	Cvar_RegisterVariable (&host_timescale); // johnfitz

	Cvar_RegisterVariable (&cl_nocsqc);	 // spike
	Cvar_RegisterVariable (&max_edicts); // johnfitz
	Cvar_SetCallback (&max_edicts, Max_Edicts_f);
	Cvar_RegisterVariable (&devstats); // johnfitz

	Cvar_RegisterVariable (&sys_ticrate);
	Cvar_RegisterVariable (&serverprofile);

	Cvar_RegisterVariable (&fraglimit);
	Cvar_RegisterVariable (&timelimit);
	Cvar_RegisterVariable (&teamplay);
	Cvar_SetCallback (&fraglimit, Host_Callback_Notify);
	Cvar_SetCallback (&timelimit, Host_Callback_Notify);
	Cvar_SetCallback (&teamplay, Host_Callback_Notify);
	Cvar_RegisterVariable (&samelevel);
	Cvar_RegisterVariable (&noexit);
	Cvar_SetCallback (&noexit, Host_Callback_Notify);
	Cvar_RegisterVariable (&skill);
	Cvar_RegisterVariable (&developer);
	Cvar_RegisterVariable (&coop);
	Cvar_RegisterVariable (&deathmatch);

	Cvar_RegisterVariable (&campaign);
	Cvar_RegisterVariable (&horde);
	Cvar_RegisterVariable (&sv_cheats);

	Cvar_RegisterVariable (&pausable);

	Cvar_RegisterVariable (&autoload);
	Cvar_RegisterVariable (&autofastload);

	Cvar_RegisterVariable (&temp1);

	Host_FindMaxClients ();
}

/*
===============
Host_WriteConfiguration

Writes key bindings and archived cvars to config.cfg
===============
*/
void Host_WriteConfiguration (void)
{
	FILE *f = NULL;

	// dedicated servers initialize the host but don't parse and set the
	// config.cfg cvars
	if (host_initialized && !isDedicated && !host_parms->errstate)
	{
		if (multiuser)
		{
			char *pref_path = SDL_GetPrefPath ("", "vkQuake");
			f = fopen (va ("%s/config.cfg", pref_path), "w");
			SDL_free (pref_path);
		}
		else
			f = fopen (va ("%s/" CONFIG_NAME, com_gamedir), "w");
		if (!f)
		{
			Con_Printf ("Couldn't write " CONFIG_NAME ".\n");
			return;
		}

		// VID_SyncCvars (); //johnfitz -- write actual current mode to config file, in case cvars were messed with

		Key_WriteBindings (f);
		Cvar_WriteVariables (f);

		// johnfitz -- extra commands to preserve state
		fprintf (f, "vid_restart\n");
		fprintf (f, "+mlook\n"); // always enable mouse look on config, can be overriden by -mlook in autoexec.cfg
		// johnfitz

		fclose (f);
	}
}

/*
=================
SV_ClientPrintf

Sends text across to be displayed
FIXME: make this just a stuffed echo?
=================
*/
void SV_ClientPrintf (const char *fmt, ...)
{
	va_list argptr;
	char	string[1024];

	va_start (argptr, fmt);
	q_vsnprintf (string, sizeof (string), fmt, argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_print);
	MSG_WriteString (&host_client->message, string);
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients
=================
*/
void SV_BroadcastPrintf (const char *fmt, ...)
{
	va_list argptr;
	char	string[1024];
	int		i;

	va_start (argptr, fmt);
	q_vsnprintf (string, sizeof (string), fmt, argptr);
	va_end (argptr);

	for (i = 0; i < svs.maxclients; i++)
	{
		if (svs.clients[i].active && svs.clients[i].spawned)
		{
			MSG_WriteByte (&svs.clients[i].message, svc_print);
			MSG_WriteString (&svs.clients[i].message, string);
		}
	}
}

/*
=================
Host_ClientCommands

Send text over to the client to be executed
=================
*/
void Host_ClientCommands (const char *fmt, ...)
{
	va_list argptr;
	char	string[1024];

	va_start (argptr, fmt);
	q_vsnprintf (string, sizeof (string), fmt, argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_stufftext);
	MSG_WriteString (&host_client->message, string);
}

/*
=====================
SV_DropClient

Called when the player is getting totally kicked off the host
if (crash = true), don't bother sending signofs
=====================
*/
void SV_DropClient (qboolean crash)
{
	int		  saveSelf;
	int		  i;
	client_t *client;

	if (!crash)
	{
		// send any final messages (don't check for errors)
		if (NET_CanSendMessage (host_client->netconnection))
		{
			MSG_WriteByte (&host_client->message, svc_disconnect);
			NET_SendMessage (host_client->netconnection, &host_client->message);
		}

		if (host_client->edict && host_client->spawned)
		{
			// call the prog function for removing a client
			// this will set the body to a dead frame, among other things
			qcvm_t *oldvm = qcvm;
			PR_SwitchQCVM (NULL);
			PR_SwitchQCVM (&sv.qcvm);
			saveSelf = pr_global_struct->self;
			pr_global_struct->self = EDICT_TO_PROG (host_client->edict);
			PR_ExecuteProgram (pr_global_struct->ClientDisconnect);
			pr_global_struct->self = saveSelf;
			PR_SwitchQCVM (NULL);
			PR_SwitchQCVM (oldvm);
		}

		Sys_Printf ("Client %s removed\n", host_client->name);
	}

	// break the net connection
	NET_Close (host_client->netconnection);
	host_client->netconnection = NULL;

	SVFTE_DestroyFrames (host_client); // release any delta state

	// free the client (the body stays around)
	host_client->active = false;
	host_client->name[0] = 0;
	host_client->old_frags = -999999;
	net_activeconnections--;

	// send notification to all clients
	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
		if (!client->knowntoqc)
			continue;

		MSG_WriteByte (&client->message, svc_updatename);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteString (&client->message, "");
		MSG_WriteByte (&client->message, svc_updatecolors);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteByte (&client->message, 0);

		MSG_WriteByte (&client->message, svc_updatefrags);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteShort (&client->message, 0);
	}
}

/*
==================
Host_ShutdownServer

This only happens at the end of a game, not between levels
==================
*/
void Host_ShutdownServer (qboolean crash)
{
	int		  i;
	int		  count;
	sizebuf_t buf;
	byte	  message[4];
	double	  start;

	if (!sv.active)
		return;

	sv.active = false;

	// stop all client sounds immediately
	if (cls.state == ca_connected)
		CL_Disconnect ();

	// flush any pending messages - like the score!!!
	start = Sys_DoubleTime ();
	do
	{
		count = 0;
		for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		{
			if (host_client->active && host_client->message.cursize && host_client->netconnection)
			{
				if (NET_CanSendMessage (host_client->netconnection))
				{
					NET_SendMessage (host_client->netconnection, &host_client->message);
					SZ_Clear (&host_client->message);
				}
				else
				{
					NET_GetMessage (host_client->netconnection);
					count++;
				}
			}
		}
		if ((Sys_DoubleTime () - start) > 3.0)
			break;
	} while (count);

	// make sure all the clients know we're disconnecting
	buf.data = message;
	buf.maxsize = 4;
	buf.cursize = 0;
	MSG_WriteByte (&buf, svc_disconnect);
	count = NET_SendToAll (&buf, 5.0);
	if (count)
		Con_Printf ("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);

	PR_SwitchQCVM (&sv.qcvm);
	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		if (host_client->active)
			SV_DropClient (crash);

	qcvm->worldmodel = NULL;
	PR_SwitchQCVM (NULL);

	//
	// clear structures
	//
	//	memset (&sv, 0, sizeof(sv)); // ServerSpawn already do this by Host_ClearMemory
	memset (svs.clients, 0, svs.maxclientslimit * sizeof (client_t));
}

/*
================
Host_ClearMemory

This clears all the memory used by both the client and server, but does
not reinitialize anything.
================
*/
void Host_ClearMemory (void)
{
	if (cl.qcvm.extfuncs.CSQC_Shutdown)
	{
		PR_SwitchQCVM (&cl.qcvm);
		PR_ExecuteProgram (qcvm->extfuncs.CSQC_Shutdown);
		qcvm->extfuncs.CSQC_Shutdown = 0;
		PR_SwitchQCVM (NULL);
	}

	Con_DPrintf ("Clearing memory\n");
	Mod_ClearAll ();
	Sky_ClearAll ();
	if (!isDedicated)
		S_ClearAll ();
	cls.signon = 0;
	PR_ClearProgs (&sv.qcvm);
	Mem_Free (sv.static_entities); // spike -- this is dynamic too, now
	for (int i = 1; i < MAX_PARTICLETYPES; ++i)
		Mem_Free (sv.particle_precache[i]);
	memset (&sv, 0, sizeof (sv));

	CL_FreeState ();
}

//==============================================================================
//
// Host Frame
//
//==============================================================================

/*
===================
Host_FilterTime

Returns false if the time is too short to run a frame
===================
*/
qboolean Host_FilterTime (float time)
{
	float maxfps; // johnfitz
	float min_frame_time;
	float delta_since_last_frame;

	realtime += time;
	delta_since_last_frame = realtime - oldrealtime;

	if (host_maxfps.value)
	{
		// johnfitz -- max fps cvar
		maxfps = CLAMP (10.0, host_maxfps.value, 1000.0);

		// Check if we still have more than 2ms till next frame and if so wait for "1ms"
		// E.g. Windows is not a real time OS and the sleeps can vary in length even with timeBeginPeriod(1)
		min_frame_time = 1.0f / maxfps;
		if ((min_frame_time - delta_since_last_frame) > (2.0f / 1000.0f))
			SDL_Delay (1);

		if (!cls.timedemo && (delta_since_last_frame < min_frame_time))
			return false; // framerate is too high
						  // johnfitz
	}

	host_frametime = delta_since_last_frame;
	oldrealtime = realtime;

	if (cls.demoplayback && cls.demospeed != 1.f && cls.demospeed > 0.f)
		host_frametime *= cls.demospeed;
	// johnfitz -- host_timescale is more intuitive than host_framerate
	else if (host_timescale.value > 0)
		host_frametime *= host_timescale.value;
	// johnfitz
	else if (host_framerate.value > 0)
		host_frametime = host_framerate.value;
	else if (host_maxfps.value)								  // don't allow really long or short frames
		host_frametime = CLAMP (0.0001, host_frametime, 0.1); // johnfitz -- use CLAMP

	return true;
}

/*
===================
Host_GetConsoleCommands

Add them exactly as if they had been typed at the console
===================
*/
void Host_GetConsoleCommands (void)
{
	const char *cmd;

	if (!isDedicated)
		return; // no stdin necessary in graphical mode

	while (1)
	{
		cmd = Sys_ConsoleInput ();
		if (!cmd)
			break;
		Cbuf_AddText (cmd);
	}
}

/*
==================
Host_ServerFrame
==================
*/
void Host_ServerFrame (void)
{
	int		 i, active; // johnfitz
	edict_t *ent;		// johnfitz

	// run the world state
	pr_global_struct->frametime = host_frametime;

	// set the time and clear the general datagram
	SV_ClearDatagram ();

	// check for new clients
	SV_CheckForNewClients ();

	// read client messages
	SV_RunClients ();

	// move things around and think
	// always pause in single player if in console or menus
	if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game))
		SV_Physics ();

	// johnfitz -- devstats
	if (cls.signon == SIGNONS)
	{
		for (i = 0, active = 0; i < qcvm->num_edicts; i++)
		{
			ent = EDICT_NUM (i);
			if (!ent->free)
				active++;
		}
		if (active > 600 && dev_peakstats.edicts <= 600)
			Con_DWarning ("%i edicts exceeds standard limit of 600 (max = %d).\n", active, qcvm->max_edicts);
		dev_stats.edicts = active;
		dev_peakstats.edicts = q_max (active, dev_peakstats.edicts);
	}
	// johnfitz

	// send all messages to the clients
	SV_SendClientMessages ();
}

static void CL_LoadCSProgs (void)
{
	PR_ClearProgs (&cl.qcvm);
	if (pr_checkextension.value && !cl_nocsqc.value)
	{ // only try to use csqc if qc extensions are enabled.
		char		 versionedname[MAX_QPATH];
		unsigned int csqchash;
		PR_SwitchQCVM (&cl.qcvm);
		csqchash = strtoul (Info_GetKey (cl.serverinfo, "*csprogs", versionedname, sizeof (versionedname)), NULL, 0);

		q_snprintf (versionedname, MAX_QPATH, "csprogsvers/%x.dat", csqchash);

		// try csprogs.dat first, then fall back on progs.dat in case someone tried merging the two.
		// we only care about it if it actually contains a CSQC_DrawHud, otherwise its either just a (misnamed) ssqc progs or a full csqc progs that would just
		// crash us on 3d stuff.
		if ((PR_LoadProgs (versionedname, false, PROGHEADER_CRC, pr_csqcbuiltins, pr_csqcnumbuiltins) && qcvm->extfuncs.CSQC_DrawHud) ||
			(PR_LoadProgs ("csprogs.dat", false, PROGHEADER_CRC, pr_csqcbuiltins, pr_csqcnumbuiltins) && qcvm->extfuncs.CSQC_DrawHud) ||
			(PR_LoadProgs ("progs.dat", false, PROGHEADER_CRC, pr_csqcbuiltins, pr_csqcnumbuiltins) && qcvm->extfuncs.CSQC_DrawHud))
		{
			qcvm->max_edicts = CLAMP (MIN_EDICTS, (int)max_edicts.value, MAX_EDICTS);
			qcvm->edicts = (edict_t *)Mem_Alloc (qcvm->max_edicts * qcvm->edict_size);
			qcvm->num_edicts = qcvm->reserved_edicts = 1;
			memset (qcvm->edicts, 0, qcvm->num_edicts * qcvm->edict_size);

			if (!qcvm->extfuncs.CSQC_DrawHud)
			{ // no simplecsqc entry points... abort entirely!
				PR_ClearProgs (qcvm);
				PR_SwitchQCVM (NULL);
				return;
			}

			// set a few globals, if they exist
			if (qcvm->extglobals.maxclients)
				*qcvm->extglobals.maxclients = cl.maxclients;
			pr_global_struct->time = cl.time;
			pr_global_struct->mapname = PR_SetEngineString (cl.mapname);
			pr_global_struct->total_monsters = cl.statsf[STAT_TOTALMONSTERS];
			pr_global_struct->total_secrets = cl.statsf[STAT_TOTALSECRETS];
			pr_global_struct->deathmatch = cl.gametype;
			pr_global_struct->coop = (cl.gametype == GAME_COOP) && cl.maxclients != 1;
			if (qcvm->extglobals.player_localnum)
				*qcvm->extglobals.player_localnum = cl.viewentity - 1; // this is a guess, but is important for scoreboards.

			// set a few worldspawn fields too
			qcvm->edicts->v.solid = SOLID_BSP;
			qcvm->edicts->v.modelindex = 1;
			qcvm->edicts->v.model = PR_SetEngineString (cl.worldmodel->name);
			VectorCopy (cl.worldmodel->mins, qcvm->edicts->v.mins);
			VectorCopy (cl.worldmodel->maxs, qcvm->edicts->v.maxs);
			qcvm->edicts->v.message = PR_SetEngineString (cl.levelname);

			// and call the init function... if it exists.
			qcvm->worldmodel = cl.worldmodel;
			SV_ClearWorld ();
			if (qcvm->extfuncs.CSQC_Init)
			{
				int maj = (int)VKQUAKE_VERSION;
				int min = (VKQUAKE_VERSION - maj) * 100;
				G_FLOAT (OFS_PARM0) = false;
				G_INT (OFS_PARM1) = PR_SetEngineString ("vkQuake");
				G_FLOAT (OFS_PARM2) = 10000 * maj + 100 * (min) + VKQUAKE_VER_PATCH;
				PR_ExecuteProgram (qcvm->extfuncs.CSQC_Init);
			}
		}
		else
			PR_ClearProgs (qcvm);
		PR_SwitchQCVM (NULL);
	}
}

/*
==================
Host_Frame

Runs all active servers
==================
*/
void _Host_Frame (double time)
{
	static double accumtime = 0;
	static double time1 = 0;
	static double time2 = 0;
	static double time3 = 0;
	double		  pass1, pass2, pass3;

	if (setjmp (host_abortserver))
		return; // something bad happened, or the server disconnected

	// keep the random time dependent
	COM_Rand ();

	// decide the simulation time
	accumtime += host_netinterval ? CLAMP (0, time, 0.2) : 0; // for renderer/server isolation
	if (!Host_FilterTime (time))
		return; // don't run too fast, or packets will flood out

	if (host_speeds.value)
		time3 = Sys_DoubleTime ();

	if (!isDedicated)
	{
		// get new key events
		Key_UpdateForDest ();
		IN_UpdateInputMode ();
		Sys_SendKeyEvents ();

		// allow mice or other external controllers to add commands
		IN_Commands ();
	}

	// check the stdin for commands (dedicated servers)
	Host_GetConsoleCommands ();

	// process console commands
	Cbuf_Execute ();

	NET_Poll ();

	if (cl.sendprespawn)
	{
		CL_LoadCSProgs ();

		cl.sendprespawn = false;
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "prespawn");
		vid.recalc_refdef = true;
	}

	CL_AccumulateCmd ();
	M_UpdateMouse ();

	// Run the server+networking (client->server->client), at a different rate from everyt
	while ((host_netinterval == 0) || (accumtime >= host_netinterval))
	{
		double realframetime = host_frametime;
		if (host_netinterval && isDedicated == 0)
		{
			if (sv.active)
			{
				if (listening)
				{
					host_frametime = q_min (accumtime, 0.017);
				}
				else
				{
					host_frametime = q_max (accumtime, host_netinterval);
				}
			}
			else
			{
				host_frametime = accumtime;
			}

			accumtime -= host_frametime;
			if (host_timescale.value > 0)
				host_frametime *= host_timescale.value;
			else if (host_framerate.value)
				host_frametime = host_framerate.value;
		}

		CL_SendCmd ();
		if (sv.active)
		{
			PR_SwitchQCVM (&sv.qcvm);
			Host_ServerFrame ();
			PR_SwitchQCVM (NULL);
		}
		host_frametime = realframetime;
		Cbuf_Waited ();

		if (host_netinterval == 0 || isDedicated)
			break;
	}

	if (cl.qcvm.progs)
	{
		PR_SwitchQCVM (&cl.qcvm);
		pr_global_struct->frametime = host_frametime;
		SV_Physics ();
		PR_SwitchQCVM (NULL);
	}

	// fetch results from server
	if (cls.state == ca_connected)
		CL_ReadFromServer ();

#ifdef USE_RMLUI
	UI_TickStartup ();
#endif

	// update video
	if (host_speeds.value)
		time1 = Sys_DoubleTime ();

	SCR_UpdateScreen (true);

	CL_RunParticles (); // johnfitz -- seperated from rendering

	if (host_speeds.value)
		time2 = Sys_DoubleTime ();

	// update audio
	BGM_Update (); // adds music raw samples and/or advances midi driver
	if (cls.signon == SIGNONS)
	{
		S_Update (r_origin, vpn, vright, vup);
		CL_DecayLights ();
	}
	else if (!isDedicated)
		S_Update (vec3_origin, vec3_origin, vec3_origin, vec3_origin);

	CDAudio_Update ();

	if (host_speeds.value)
	{
		pass1 = (time1 - time3) * 1000;
		time3 = Sys_DoubleTime ();
		pass2 = (time2 - time1) * 1000;
		pass3 = (time3 - time2) * 1000;
		Con_Printf ("%5.2f tot %5.2f server %5.2f gfx %5.2f snd\n", pass1 + pass2 + pass3, pass1, pass2, pass3);
	}

	host_framecount++;
}

void Host_Frame (double time)
{
	double		  time1, time2;
	static double timetotal;
	static int	  timecount;
	int			  i, c, m;

	if (!serverprofile.value)
	{
		_Host_Frame (time);
		return;
	}

	time1 = Sys_DoubleTime ();
	_Host_Frame (time);
	time2 = Sys_DoubleTime ();

	timetotal += time2 - time1;
	timecount++;

	if (timecount < 1000)
		return;

	m = timetotal * 1000 / timecount;
	timecount = 0;
	timetotal = 0;
	c = 0;
	for (i = 0; i < svs.maxclients; i++)
	{
		if (svs.clients[i].active)
			c++;
	}

	Con_Printf ("serverprofile: %2i clients %2i msec\n", c, m);
}

/*
====================
Tests_Init
====================
*/
static void Tests_Init ()
{
#ifdef _DEBUG
	Cmd_AddCommand ("test_hash_map", TestHashMap_f);
	Cmd_AddCommand ("test_gl_heap", GL_HeapTest_f);
	Cmd_AddCommand ("test_tasks", TestTasks_f);
#endif
}

/*
====================
Host_Init
====================
*/
void Host_Init (void)
{
	com_argc = host_parms->argc;
	com_argv = host_parms->argv;

	Mem_Init ();
	Tasks_Init ();
	Cbuf_Init ();
	Cmd_Init ();
	LOG_Init (host_parms);
	Cvar_Init (); // johnfitz
	COM_Init ();
	COM_InitFilesystem ();
	Host_InitLocal ();
	W_LoadWadFile (); // johnfitz -- filename is now hard-coded for honesty
	if (cls.state != ca_dedicated)
	{
		Key_Init ();
		Con_Init ();
	}
	PR_Init ();
	Mod_Init ();
	NET_Init ();
	SV_Init ();

	Con_Printf ("Exe: " __TIME__ " " __DATE__ "\n");

	if (cls.state != ca_dedicated)
	{
		host_colormap = (byte *)COM_LoadFile ("gfx/colormap.lmp", NULL);
		if (!host_colormap)
			Sys_Error ("Couldn't load gfx/colormap.lmp");

		V_Init ();
		Chase_Init ();
		M_Init ();
		ExtraMaps_Init (); // johnfitz
		Modlist_Init ();   // johnfitz
		DemoList_Init ();  // ericw
		SaveList_Init ();
#ifdef USE_RMLUI
		/* Initialize RmlUI core BEFORE VID_Init so the render interface exists
		 * when UI_InitializeVulkan is called from GL_InitDevice */
		UI_Init (1280, 720, com_basedir); /* Initial size, will resize after VID_Init */
		/* Register cvars and console commands for RmlUI */
		Cvar_SetCallback (&ui_use_rmlui, UI_UseRmluiChanged_f);
		Cvar_RegisterVariable (&ui_use_rmlui);
		Cmd_AddCommand ("ui_toggle", UI_Toggle_f);
		Cmd_AddCommand ("ui_show", UI_Show_f);
		Cmd_AddCommand ("ui_show_when_ready", UI_ShowWhenReady_f);
		Cmd_AddCommand ("ui_hide", UI_Hide_f);
		Cmd_AddCommand ("ui_debugger", UI_Debugger_f);
		Cmd_AddCommand ("ui_debuger", UI_Debugger_f);
		Cmd_AddCommand ("ui_menu", UI_Menu_f);
		Cmd_AddCommand ("ui_closemenu", UI_CloseMenu_f);
		Cmd_AddCommand ("ui_reload", UI_Reload_f);
		Cmd_AddCommand ("ui_reload_css", UI_ReloadCSS_f);
		Cmd_AddCommand ("lua_test", UI_LuaTest_f);
		ui_startup.phase = STARTUP_AUTO_DETECT;
		ui_startup.auto_detect_after = realtime + UI_AUTO_MENU_DELAY;
#endif
		VID_Init ();
#ifdef USE_RMLUI
		/* Resize RmlUI context to actual window size */
		UI_Resize (vid.width, vid.height);
#endif
		IN_Init ();
		TexMgr_Init (); // johnfitz
		Draw_Init ();
		SCR_Init ();
		R_Init ();
		S_Init ();
		CDAudio_Init ();
		BGM_Init ();
		Sbar_Init ();
		CL_Init ();
		Tests_Init ();
	}

#ifdef PSET_SCRIPT
	PScript_InitParticles ();
#endif
	LOC_Init (); // for 2021 rerelease support.

	host_initialized = true;
	Con_Printf ("\n========= Quake Initialized =========\n\n");

	if (cls.state != ca_dedicated)
	{
		Cbuf_InsertText ("exec quake.rc\n");
		// johnfitz -- in case the vid mode was locked during vid_init, we can unlock it now.
		// note: two leading newlines because the command buffer swallows one of them.
		Cbuf_AddText ("\n\nvid_unlock\n");
	}

	if (cls.state == ca_dedicated)
	{
		Cbuf_AddText ("exec autoexec.cfg\n");
		Cbuf_AddText ("stuffcmds");
		Cbuf_Execute ();
		if (!sv.active)
			Cbuf_AddText ("map start\n");
	}
}

/*
===============
Host_Shutdown

FIXME: this is a callback from Sys_Quit and Sys_Error.  It would be better
to run quit through here before the final handoff to the sys code.
===============
*/
void Host_Shutdown (void)
{
	assert (!Tasks_IsWorker ());
	static qboolean isdown = false;

	if (isdown)
	{
		printf ("recursive shutdown\n");
		return;
	}
	isdown = true;

	// keep Con_Printf from trying to update the screen
	scr_disabled_for_loading = true;

	Host_WriteConfiguration ();

	NET_Shutdown ();

	if (cls.state != ca_dedicated)
	{
		if (con_initialized)
			History_Shutdown ();
		BGM_Shutdown ();
		CDAudio_Shutdown ();
		S_Shutdown ();
#ifdef USE_RMLUI
		UI_Shutdown ();
#endif
		IN_Shutdown ();
		VID_Shutdown ();
	}

	LOG_Close ();

	LOC_Shutdown ();
}
