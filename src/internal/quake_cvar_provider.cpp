/*
 * vkQuake RmlUI - Quake Cvar Provider Implementation
 */

#include "quake_cvar_provider.h"

#include "engine_bridge.h"

// cvar_t struct and Cvar_FindVar are engine-internal — only this
// provider implementation needs the struct layout.
extern "C" {
    typedef struct cvar_s {
        const char* name;
        const char* string;
        unsigned int flags;
        float value;
    } cvar_t;

    cvar_t* Cvar_FindVar(const char* var_name);
}

namespace QRmlUI {

QuakeCvarProvider& QuakeCvarProvider::Instance()
{
    static QuakeCvarProvider instance;
    return instance;
}

float QuakeCvarProvider::GetFloat(const std::string& name) const
{
    return Cvar_VariableValue(name.c_str());
}

std::string QuakeCvarProvider::GetString(const std::string& name) const
{
    const char* str = Cvar_VariableString(name.c_str());
    return str ? str : "";
}

void QuakeCvarProvider::SetFloat(const std::string& name, float value)
{
    Cvar_SetValue(name.c_str(), value);
}

void QuakeCvarProvider::SetString(const std::string& name, const std::string& value)
{
    Cvar_Set(name.c_str(), value.c_str());
}

bool QuakeCvarProvider::Exists(const std::string& name) const
{
    return Cvar_FindVar(name.c_str()) != nullptr;
}

} // namespace QRmlUI
