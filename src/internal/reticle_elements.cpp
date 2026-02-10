/*
 * vkQuake RmlUI - Reticle Custom Elements Implementation
 *
 * Each primitive reads RCSS custom properties and generates procedural geometry.
 * Color comes from computed image-color + opacity. All geometry is centered
 * in the parent <reticle> container's content box.
 */

#include "reticle_elements.h"
#include "reticle_geometry.h"
#include "reticle_plugin.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/PropertyIdSet.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/Box.h>

namespace QRmlUI
{

// Resolve a custom RCSS length property to pixels via the element's dp scaling.
static float ResolveCustomProperty (Rml::Element *element, Rml::PropertyId id, float fallback)
{
	const Rml::Property *prop = element->GetProperty (id);
	if (!prop)
		return fallback;
	return element->ResolveLength (prop->GetNumericValue ());
}

// Resolve a custom RCSS number property (unitless, e.g. angle in degrees).
static float ResolveCustomNumber (Rml::Element *element, Rml::PropertyId id, float fallback)
{
	const Rml::Property *prop = element->GetProperty (id);
	if (!prop)
		return fallback;
	return prop->GetNumericValue ().number;
}

// Get the center of the parent's content box in absolute coordinates.
static Rml::Vector2f GetParentCenter (Rml::Element *element)
{
	Rml::Element *parent = element->GetParentNode ();
	if (!parent)
		return element->GetAbsoluteOffset (Rml::BoxArea::Content);

	Rml::Vector2f parent_size = parent->GetBox ().GetSize (Rml::BoxArea::Content);
	Rml::Vector2f parent_offset = parent->GetAbsoluteOffset (Rml::BoxArea::Content);
	return parent_offset + parent_size * 0.5f;
}

// Get premultiplied color from computed image-color and opacity.
static Rml::ColourbPremultiplied GetElementColor (Rml::Element *element)
{
	const auto &computed = element->GetComputedValues ();
	return computed.image_color ().ToPremultiplied (computed.opacity ());
}

// Check if changed properties contain any of our custom reticle properties.
static bool HasReticlePropertyChange (const Rml::PropertyIdSet &changed)
{
	return changed.Contains (ReticlePlugin::PropRadius ()) || changed.Contains (ReticlePlugin::PropStroke ()) ||
		   changed.Contains (ReticlePlugin::PropLength ()) || changed.Contains (ReticlePlugin::PropWidth ()) || changed.Contains (ReticlePlugin::PropGap ()) ||
		   changed.Contains (ReticlePlugin::PropStartAngle ()) || changed.Contains (ReticlePlugin::PropEndAngle ());
}

// ──────────────────────────────────────────────────────────────────
// ElementReticle — container
// ──────────────────────────────────────────────────────────────────

ElementReticle::ElementReticle (const Rml::String &tag) : Rml::Element (tag) {}

bool ElementReticle::GetIntrinsicDimensions (Rml::Vector2f &dimensions, float &ratio)
{
	(void)ratio;
	dimensions = {0.0f, 0.0f};
	return false;
}

// ──────────────────────────────────────────────────────────────────
// ElementReticleDot — filled circle
// ──────────────────────────────────────────────────────────────────

ElementReticleDot::ElementReticleDot (const Rml::String &tag) : Rml::Element (tag) {}

bool ElementReticleDot::GetIntrinsicDimensions (Rml::Vector2f &dimensions, float &ratio)
{
	(void)ratio;
	dimensions = {0.0f, 0.0f};
	return false;
}

void ElementReticleDot::OnResize ()
{
	m_geometry_dirty = true;
}

void ElementReticleDot::OnPropertyChange (const Rml::PropertyIdSet &changed)
{
	Rml::Element::OnPropertyChange (changed);
	if (HasReticlePropertyChange (changed) || changed.Contains (Rml::PropertyId::ImageColor) || changed.Contains (Rml::PropertyId::Opacity))
		m_geometry_dirty = true;
}

void ElementReticleDot::OnRender ()
{
	if (m_geometry_dirty)
		GenerateGeometry ();

	Rml::Vector2f center = GetParentCenter (this);
	m_geometry.Render (center);
}

void ElementReticleDot::GenerateGeometry ()
{
	m_geometry_dirty = false;

	float					  radius = ResolveCustomProperty (this, ReticlePlugin::PropRadius (), 2.0f);
	Rml::ColourbPremultiplied color = GetElementColor (this);

	Rml::Mesh mesh = m_geometry.Release (Rml::Geometry::ReleaseMode::ClearMesh);
	GenerateFilledCircle (mesh, {0.0f, 0.0f}, radius, color);

	Rml::RenderManager *rm = GetRenderManager ();
	if (rm)
		m_geometry = rm->MakeGeometry (std::move (mesh));
}

// ──────────────────────────────────────────────────────────────────
// ElementReticleLine — rotated rectangle arm
// ──────────────────────────────────────────────────────────────────

ElementReticleLine::ElementReticleLine (const Rml::String &tag) : Rml::Element (tag) {}

bool ElementReticleLine::GetIntrinsicDimensions (Rml::Vector2f &dimensions, float &ratio)
{
	(void)ratio;
	dimensions = {0.0f, 0.0f};
	return false;
}

void ElementReticleLine::OnResize ()
{
	m_geometry_dirty = true;
}

void ElementReticleLine::OnPropertyChange (const Rml::PropertyIdSet &changed)
{
	Rml::Element::OnPropertyChange (changed);
	if (HasReticlePropertyChange (changed) || changed.Contains (Rml::PropertyId::ImageColor) || changed.Contains (Rml::PropertyId::Opacity))
		m_geometry_dirty = true;
}

void ElementReticleLine::OnAttributeChange (const Rml::ElementAttributes &changed_attributes)
{
	Rml::Element::OnAttributeChange (changed_attributes);
	if (changed_attributes.count ("angle"))
		m_geometry_dirty = true;
}

void ElementReticleLine::OnRender ()
{
	if (m_geometry_dirty)
		GenerateGeometry ();

	Rml::Vector2f center = GetParentCenter (this);
	m_geometry.Render (center);
}

void ElementReticleLine::GenerateGeometry ()
{
	m_geometry_dirty = false;

	float angle = GetAttribute ("angle", 0.0f);
	float length = ResolveCustomProperty (this, ReticlePlugin::PropLength (), 8.0f);
	float width = ResolveCustomProperty (this, ReticlePlugin::PropWidth (), 2.0f);
	float gap = ResolveCustomProperty (this, ReticlePlugin::PropGap (), 3.0f);

	Rml::ColourbPremultiplied color = GetElementColor (this);

	Rml::Mesh mesh = m_geometry.Release (Rml::Geometry::ReleaseMode::ClearMesh);
	GenerateRotatedRect (mesh, {0.0f, 0.0f}, angle, gap, length, width, color);

	Rml::RenderManager *rm = GetRenderManager ();
	if (rm)
		m_geometry = rm->MakeGeometry (std::move (mesh));
}

// ──────────────────────────────────────────────────────────────────
// ElementReticleRing — full ring
// ──────────────────────────────────────────────────────────────────

ElementReticleRing::ElementReticleRing (const Rml::String &tag) : Rml::Element (tag) {}

bool ElementReticleRing::GetIntrinsicDimensions (Rml::Vector2f &dimensions, float &ratio)
{
	(void)ratio;
	dimensions = {0.0f, 0.0f};
	return false;
}

void ElementReticleRing::OnResize ()
{
	m_geometry_dirty = true;
}

void ElementReticleRing::OnPropertyChange (const Rml::PropertyIdSet &changed)
{
	Rml::Element::OnPropertyChange (changed);
	if (HasReticlePropertyChange (changed) || changed.Contains (Rml::PropertyId::ImageColor) || changed.Contains (Rml::PropertyId::Opacity))
		m_geometry_dirty = true;
}

void ElementReticleRing::OnRender ()
{
	if (m_geometry_dirty)
		GenerateGeometry ();

	Rml::Vector2f center = GetParentCenter (this);
	m_geometry.Render (center);
}

void ElementReticleRing::GenerateGeometry ()
{
	m_geometry_dirty = false;

	float radius = ResolveCustomProperty (this, ReticlePlugin::PropRadius (), 10.0f);
	float stroke = ResolveCustomProperty (this, ReticlePlugin::PropStroke (), 1.5f);

	Rml::ColourbPremultiplied color = GetElementColor (this);

	Rml::Mesh mesh = m_geometry.Release (Rml::Geometry::ReleaseMode::ClearMesh);
	GenerateRing (mesh, {0.0f, 0.0f}, radius, stroke, color);

	Rml::RenderManager *rm = GetRenderManager ();
	if (rm)
		m_geometry = rm->MakeGeometry (std::move (mesh));
}

// ──────────────────────────────────────────────────────────────────
// ElementReticleArc — partial ring
// ──────────────────────────────────────────────────────────────────

ElementReticleArc::ElementReticleArc (const Rml::String &tag) : Rml::Element (tag) {}

bool ElementReticleArc::GetIntrinsicDimensions (Rml::Vector2f &dimensions, float &ratio)
{
	(void)ratio;
	dimensions = {0.0f, 0.0f};
	return false;
}

void ElementReticleArc::OnResize ()
{
	m_geometry_dirty = true;
}

void ElementReticleArc::OnPropertyChange (const Rml::PropertyIdSet &changed)
{
	Rml::Element::OnPropertyChange (changed);
	if (HasReticlePropertyChange (changed) || changed.Contains (Rml::PropertyId::ImageColor) || changed.Contains (Rml::PropertyId::Opacity))
		m_geometry_dirty = true;
}

void ElementReticleArc::OnRender ()
{
	if (m_geometry_dirty)
		GenerateGeometry ();

	Rml::Vector2f center = GetParentCenter (this);
	m_geometry.Render (center);
}

void ElementReticleArc::GenerateGeometry ()
{
	m_geometry_dirty = false;

	float radius = ResolveCustomProperty (this, ReticlePlugin::PropRadius (), 10.0f);
	float stroke = ResolveCustomProperty (this, ReticlePlugin::PropStroke (), 2.0f);
	float start_angle = ResolveCustomNumber (this, ReticlePlugin::PropStartAngle (), 0.0f);
	float end_angle = ResolveCustomNumber (this, ReticlePlugin::PropEndAngle (), 360.0f);

	Rml::ColourbPremultiplied color = GetElementColor (this);

	Rml::Mesh mesh = m_geometry.Release (Rml::Geometry::ReleaseMode::ClearMesh);
	GenerateArc (mesh, {0.0f, 0.0f}, radius, stroke, start_angle, end_angle, color);

	Rml::RenderManager *rm = GetRenderManager ();
	if (rm)
		m_geometry = rm->MakeGeometry (std::move (mesh));
}

} // namespace QRmlUI
