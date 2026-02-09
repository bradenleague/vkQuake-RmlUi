/*
 * vkQuake RmlUI - Quake-Aware File Interface Implementation
 */

#include "quake_file_interface.h"
#include "engine_bridge.h"

#include <cstdio>
#include <cstring>

namespace QRmlUI {

Rml::FileHandle QuakeFileInterface::Open(const Rml::String& path)
{
    // Try mod directory first (if active and different from basedir)
    if (com_gamedir[0] != '\0') {
        std::string mod_path = std::string(com_gamedir) + "/" + path.c_str();
        FILE* f = fopen(mod_path.c_str(), "rb");
        if (f) {
            Con_DPrintf("QuakeFileInterface: Opened (mod) %s\n", mod_path.c_str());
            return reinterpret_cast<Rml::FileHandle>(f);
        }
    }

    // Try base directory
    if (com_basedir[0] != '\0') {
        std::string base_path = std::string(com_basedir) + "/" + path.c_str();
        FILE* f = fopen(base_path.c_str(), "rb");
        if (f) {
            return reinterpret_cast<Rml::FileHandle>(f);
        }
    }

    // Fallback: try path as-is (relative to CWD)
    FILE* f = fopen(path.c_str(), "rb");
    if (f) {
        return reinterpret_cast<Rml::FileHandle>(f);
    }

    return 0;
}

void QuakeFileInterface::Close(Rml::FileHandle file)
{
    fclose(reinterpret_cast<FILE*>(file));
}

size_t QuakeFileInterface::Read(void* buffer, size_t size, Rml::FileHandle file)
{
    return fread(buffer, 1, size, reinterpret_cast<FILE*>(file));
}

bool QuakeFileInterface::Seek(Rml::FileHandle file, long offset, int origin)
{
    return fseek(reinterpret_cast<FILE*>(file), offset, origin) == 0;
}

size_t QuakeFileInterface::Tell(Rml::FileHandle file)
{
    return static_cast<size_t>(ftell(reinterpret_cast<FILE*>(file)));
}

} // namespace QRmlUI
