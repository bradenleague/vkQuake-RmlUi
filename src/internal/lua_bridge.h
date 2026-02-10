/*
 * vkQuake RmlUI - Lua Engine Bridge
 *
 * Exposes engine state (game table) and commands (engine table) to the
 * Lua scripting environment.  Called from ui_manager.cpp each frame.
 */

#ifndef QRMLUI_LUA_BRIDGE_H
#define QRMLUI_LUA_BRIDGE_H

#ifdef USE_LUA

namespace QRmlUI
{
namespace LuaBridge
{

// Register 'game' and 'engine' globals in the Lua state.
// Call once after Rml::Lua::Initialise().
void Initialize ();

// Push current game state into the Lua 'game' table.
// Call each frame from UI_Update(), after GameDataModel::Update().
void Update ();

} // namespace LuaBridge
} // namespace QRmlUI

#endif // USE_LUA

#endif // QRMLUI_LUA_BRIDGE_H
