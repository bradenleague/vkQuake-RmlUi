/*
 * vkQuake RmlUI - UI Document Paths
 *
 * Centralized constants for all RML document paths used by the UI layer.
 * Defining paths once prevents typo bugs and makes mod override integration
 * (via FileInterface) straightforward — each logical path is a single symbol.
 */

#ifndef QRMLUI_UI_PATHS_H
#define QRMLUI_UI_PATHS_H

namespace QRmlUI
{
namespace Paths
{

/* ── HUD document ─────────────────────────────────────────────────── */
inline constexpr const char *kHud = "ui/rml/hud.rml";

/* ── HUD overlays ─────────────────────────────────────────────────── */
inline constexpr const char *kScoreboard = "ui/rml/hud/scoreboard.rml";
inline constexpr const char *kIntermission = "ui/rml/hud/intermission.rml";

/* ── Menu prefix (used by ActionNavigate for shorthand names) ─────── */
inline constexpr const char *kMenuPrefix = "ui/rml/menus/";
inline constexpr const char *kMenuSuffix = ".rml";

} // namespace Paths
} // namespace QRmlUI

#endif /* QRMLUI_UI_PATHS_H */
