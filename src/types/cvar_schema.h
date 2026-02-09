/*
 * vkQuake RmlUI - Cvar Schema Domain Types
 *
 * Defines the schema for console variable bindings.
 * No framework dependencies - pure domain types.
 */

#ifndef QRMLUI_DOMAIN_CVAR_SCHEMA_H
#define QRMLUI_DOMAIN_CVAR_SCHEMA_H

#include <string>
#include <vector>

namespace QRmlUI {

// Cvar type enumeration
enum class CvarType {
    Float,
    Bool,
    Int,
    Enum,
    String
};

// Registered cvar binding info
struct CvarBinding {
    std::string cvar_name;   // Console variable name (e.g., "sensitivity")
    std::string ui_name;     // Name used in RmlUI data model (e.g., "mouse_speed")
    CvarType type;
    float min_value = 0.0f;
    float max_value = 1.0f;
    float step = 0.1f;
    int num_values = 0;      // For enum type
    std::vector<int> enum_values;        // Optional explicit values for enum
    std::vector<std::string> enum_labels;  // Optional display labels for enum
};

} // namespace QRmlUI

#endif // QRMLUI_DOMAIN_CVAR_SCHEMA_H
