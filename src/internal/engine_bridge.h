/*
 * vkQuake RmlUI - Engine Bridge
 *
 * Single source of truth for all vkQuake engine function and variable
 * declarations used by the RmlUI integration layer. Centralizes the
 * extern "C" boundary to prevent signature drift and duplication.
 *
 * NOTE: This header declares engine symbols only. Our own C API
 * (UI_PushMenu, MenuEventHandler_ProcessAction, etc.) stays in
 * their respective headers or as local forward declarations.
 */

#ifndef QRMLUI_ENGINE_BRIDGE_H
#define QRMLUI_ENGINE_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── Console output ───────────────────────────────────────────────── */

void Con_Printf(const char* fmt, ...);
void Con_DPrintf(const char* fmt, ...);

/* ── Cvar access ──────────────────────────────────────────────────── */

/* NOTE: Cvar_VariableValue returns double, NOT float.
 * Mismatching the return type causes silent data corruption on x86_64
 * due to SSE register width differences. See CLAUDE.md. */
double Cvar_VariableValue(const char* var_name);
const char* Cvar_VariableString(const char* var_name);
void Cvar_SetValue(const char* var_name, float value);
void Cvar_SetValueROM(const char* var_name, const float value);
void Cvar_Set(const char* var_name, const char* value);

/* NOTE: Cvar_FindVar and the cvar_t struct forward declaration stay
 * local to quake_cvar_provider.cpp — the struct layout is an engine
 * implementation detail that only that file needs. */

/* ── Command buffer ───────────────────────────────────────────────── */

void Cbuf_AddText(const char* text);
void Cbuf_InsertText(const char* text);

/* ── Input system ─────────────────────────────────────────────────── */

void IN_Activate(void);
void IN_Deactivate(int clear);
void IN_EndIgnoringMouseEvents(void);
float VID_GetDisplayDPIScale(void);

/* ── Key/input state ──────────────────────────────────────────────── */

const char* Key_FindBinding(const char* action);
const char* Key_GetChatBuffer(void);
int Key_GetChatMsgLen(void);

/* ── Engine globals ───────────────────────────────────────────────── */

extern double realtime;
extern int key_dest;
extern int /*qboolean*/ chat_team;

/* Key destination constants (from engine keys.h).
 * The engine defines these in an enum, but we can't include keys.h
 * directly in C++ code, so we mirror the values here. */
#define key_game    0
#define key_console 1
#define key_message 2
#define key_menu    3

/* ── Game directory / filesystem ──────────────────────────────────── */

const char* COM_GetGameNames(int/*qboolean*/ full);

/* com_basedir: installation root (e.g. "/home/user/vkQuake-RmlUi")
 * com_gamedir: active mod directory full path (e.g. ".../mymod")
 * Both are char[MAX_OSPATH] in the engine (MAX_OSPATH = PATH_MAX). */
#include <limits.h>
extern char com_basedir[PATH_MAX];
extern char com_gamedir[PATH_MAX];

/* ── Engine-side UI sync callbacks ────────────────────────────────── */
/* These are engine functions (guarded by USE_RMLUI in their source
 * files) that push data into the RmlUI layer on demand. */

void M_SyncSavesToUI(void);
void M_SyncModsToUI(void);
void VID_SyncModesToUI(void);
void VID_Menu_RebuildRateList(void);

#ifdef __cplusplus
}
#endif

#endif /* QRMLUI_ENGINE_BRIDGE_H */
