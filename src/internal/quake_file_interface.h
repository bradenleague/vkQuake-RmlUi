/*
 * vkQuake RmlUI - Quake-Aware File Interface
 *
 * Custom Rml::FileInterface that searches mod directories before the base
 * directory, enabling per-mod UI overrides. For example, a mod at
 * "mymod/ui/rcss/menu.rcss" automatically takes precedence over
 * "ui/rcss/menu.rcss".
 *
 * Search order:
 *   1. {com_gamedir}/{path}   — mod override (if com_gamedir is set and differs from basedir)
 *   2. {com_basedir}/{path}   — base installation
 *   3. {path}                 — relative to CWD (fallback)
 */

#ifndef QRMLUI_QUAKE_FILE_INTERFACE_H
#define QRMLUI_QUAKE_FILE_INTERFACE_H

#include <RmlUi/Core/FileInterface.h>

namespace QRmlUI {

class QuakeFileInterface : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override;
    void Close(Rml::FileHandle file) override;
    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
    bool Seek(Rml::FileHandle file, long offset, int origin) override;
    size_t Tell(Rml::FileHandle file) override;
};

} // namespace QRmlUI

#endif /* QRMLUI_QUAKE_FILE_INTERFACE_H */
