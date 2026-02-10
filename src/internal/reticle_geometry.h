/*
 * vkQuake RmlUI - Reticle Geometry Helpers
 *
 * Procedural mesh generation for reticle primitives.
 * All functions append to an existing Rml::Mesh (vertices + indices).
 */

#pragma once

#include <RmlUi/Core/Types.h>
#include <RmlUi/Core/Vertex.h>

namespace Rml
{
struct Mesh;
}

namespace QRmlUI
{

// Filled circle (triangle fan) for dot reticles.
void GenerateFilledCircle (Rml::Mesh &mesh, Rml::Vector2f center, float radius, Rml::ColourbPremultiplied color, int segments = 32);

// Ring (triangle strip) for ring reticles.
void GenerateRing (Rml::Mesh &mesh, Rml::Vector2f center, float radius, float stroke, Rml::ColourbPremultiplied color, int segments = 32);

// Arc (partial ring) for arc reticles. Angles in degrees, 0 = up, clockwise.
void GenerateArc (
	Rml::Mesh &mesh, Rml::Vector2f center, float radius, float stroke, float start_angle_deg, float end_angle_deg, Rml::ColourbPremultiplied color,
	int segments = 32);

// Rotated rectangle (line arm radiating from center) for line reticles.
// angle_deg: direction (0 = up, 90 = right). gap: distance from center to near edge. length: arm length.
void GenerateRotatedRect (Rml::Mesh &mesh, Rml::Vector2f center, float angle_deg, float gap, float length, float width, Rml::ColourbPremultiplied color);

} // namespace QRmlUI
