/*
 * vkQuake RmlUI - Reticle Geometry Helpers
 *
 * Procedural mesh generation for reticle primitives.
 * Counter-clockwise winding. Untextured (tex_coord = {0,0}).
 */

#include "reticle_geometry.h"

#include <RmlUi/Core/Mesh.h>
#include <cmath>

namespace QRmlUI
{

static constexpr float PI = 3.14159265358979323846f;

// Convert degrees to radians, where 0 = up (negative Y), clockwise.
static float DegToRad (float deg)
{
	return (deg - 90.0f) * (PI / 180.0f);
}

static Rml::Vertex MakeVertex (Rml::Vector2f pos, Rml::ColourbPremultiplied color)
{
	return Rml::Vertex{pos, color, {0.0f, 0.0f}};
}

void GenerateFilledCircle (Rml::Mesh &mesh, Rml::Vector2f center, float radius, Rml::ColourbPremultiplied color, int segments)
{
	if (radius <= 0.0f || segments < 3)
		return;

	const int base = (int)mesh.vertices.size ();

	// Center vertex
	mesh.vertices.push_back (MakeVertex (center, color));

	// Perimeter vertices
	for (int i = 0; i <= segments; ++i)
	{
		float		  angle = 2.0f * PI * (float)i / (float)segments;
		Rml::Vector2f pos = center + Rml::Vector2f (std::cos (angle) * radius, std::sin (angle) * radius);
		mesh.vertices.push_back (MakeVertex (pos, color));
	}

	// Triangle fan indices (CCW)
	for (int i = 0; i < segments; ++i)
	{
		mesh.indices.push_back (base);
		mesh.indices.push_back (base + 1 + i);
		mesh.indices.push_back (base + 1 + ((i + 1) % (segments + 1)));
	}
}

void GenerateRing (Rml::Mesh &mesh, Rml::Vector2f center, float radius, float stroke, Rml::ColourbPremultiplied color, int segments)
{
	GenerateArc (mesh, center, radius, stroke, 0.0f, 360.0f, color, segments);
}

void GenerateArc (
	Rml::Mesh &mesh, Rml::Vector2f center, float radius, float stroke, float start_angle_deg, float end_angle_deg, Rml::ColourbPremultiplied color,
	int segments)
{
	if (radius <= 0.0f || stroke <= 0.0f || segments < 3)
		return;

	float inner_radius = radius - stroke * 0.5f;
	float outer_radius = radius + stroke * 0.5f;
	if (inner_radius < 0.0f)
		inner_radius = 0.0f;

	float start_rad = DegToRad (start_angle_deg);
	float end_rad = DegToRad (end_angle_deg);
	float sweep = end_rad - start_rad;

	// Scale segment count to arc sweep proportion
	int arc_segments = (int)std::ceil (std::abs (sweep) / (2.0f * PI) * (float)segments);
	if (arc_segments < 2)
		arc_segments = 2;

	const int base = (int)mesh.vertices.size ();

	// Generate inner/outer vertex pairs along the arc
	for (int i = 0; i <= arc_segments; ++i)
	{
		float t = (float)i / (float)arc_segments;
		float angle = start_rad + sweep * t;
		float cos_a = std::cos (angle);
		float sin_a = std::sin (angle);

		Rml::Vector2f inner_pos = center + Rml::Vector2f (cos_a * inner_radius, sin_a * inner_radius);
		Rml::Vector2f outer_pos = center + Rml::Vector2f (cos_a * outer_radius, sin_a * outer_radius);

		mesh.vertices.push_back (MakeVertex (inner_pos, color));
		mesh.vertices.push_back (MakeVertex (outer_pos, color));
	}

	// Triangle strip indices (CCW)
	for (int i = 0; i < arc_segments; ++i)
	{
		int i0 = base + i * 2;			 // inner current
		int o0 = base + i * 2 + 1;		 // outer current
		int i1 = base + (i + 1) * 2;	 // inner next
		int o1 = base + (i + 1) * 2 + 1; // outer next

		// Two triangles per segment (CCW)
		mesh.indices.push_back (i0);
		mesh.indices.push_back (i1);
		mesh.indices.push_back (o0);

		mesh.indices.push_back (o0);
		mesh.indices.push_back (i1);
		mesh.indices.push_back (o1);
	}
}

void GenerateRotatedRect (Rml::Mesh &mesh, Rml::Vector2f center, float angle_deg, float gap, float length, float width, Rml::ColourbPremultiplied color)
{
	if (length <= 0.0f || width <= 0.0f)
		return;

	// Direction vector (angle_deg: 0 = up, 90 = right)
	float		  rad = DegToRad (angle_deg);
	Rml::Vector2f dir (std::cos (rad), std::sin (rad));
	Rml::Vector2f perp (-dir.y, dir.x); // perpendicular (rotated 90 CCW)

	// Rectangle extends from gap to gap+length along dir, centered on width
	Rml::Vector2f near_center = center + dir * gap;
	Rml::Vector2f far_center = center + dir * (gap + length);
	Rml::Vector2f half_w = perp * (width * 0.5f);

	const int base = (int)mesh.vertices.size ();

	// Four corners (CCW winding)
	mesh.vertices.push_back (MakeVertex (near_center - half_w, color));
	mesh.vertices.push_back (MakeVertex (far_center - half_w, color));
	mesh.vertices.push_back (MakeVertex (far_center + half_w, color));
	mesh.vertices.push_back (MakeVertex (near_center + half_w, color));

	// Two triangles (CCW)
	mesh.indices.push_back (base + 0);
	mesh.indices.push_back (base + 1);
	mesh.indices.push_back (base + 2);

	mesh.indices.push_back (base + 0);
	mesh.indices.push_back (base + 2);
	mesh.indices.push_back (base + 3);
}

} // namespace QRmlUI
