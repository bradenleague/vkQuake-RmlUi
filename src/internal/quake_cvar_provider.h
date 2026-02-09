/*
 * vkQuake RmlUI - Quake Cvar Provider Implementation
 *
 * Implements ICvarProvider using Quake's cvar system.
 */

#ifndef QRMLUI_QUAKE_CVAR_PROVIDER_H
#define QRMLUI_QUAKE_CVAR_PROVIDER_H

#include "../types/cvar_provider.h"

namespace QRmlUI {

// Quake engine implementation of ICvarProvider
class QuakeCvarProvider : public ICvarProvider {
public:
    // Singleton access - the provider is stateless, just wraps engine functions
    static QuakeCvarProvider& Instance();

    float GetFloat(const std::string& name) const override;
    std::string GetString(const std::string& name) const override;
    void SetFloat(const std::string& name, float value) override;
    void SetString(const std::string& name, const std::string& value) override;
    bool Exists(const std::string& name) const override;

private:
    QuakeCvarProvider() = default;
};

} // namespace QRmlUI

#endif // QRMLUI_QUAKE_CVAR_PROVIDER_H
