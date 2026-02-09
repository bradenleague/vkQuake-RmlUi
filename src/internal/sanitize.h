/*
 * vkQuake RmlUI - Console Command Sanitization
 *
 * Strips characters that could inject additional commands when UI-sourced
 * strings are interpolated into Quake console command buffers.
 *
 * Dangerous characters: ';' (command separator), '\n'/'\r' (line break),
 * '"' (breaks out of quoted arguments).
 */

#ifndef QRMLUI_SANITIZE_H
#define QRMLUI_SANITIZE_H

#include <string>
#include <algorithm>

namespace QRmlUI {

inline std::string SanitizeForConsole(const std::string& input)
{
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        if (c != ';' && c != '\n' && c != '\r' && c != '"') {
            out += c;
        }
    }
    return out;
}

} // namespace QRmlUI

#endif /* QRMLUI_SANITIZE_H */
