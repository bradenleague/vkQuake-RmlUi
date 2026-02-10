/*
 * vkQuake RmlUI - Reticle Plugin Implementation
 *
 * Registers 7 custom RCSS properties and 5 element instancers.
 */

#include "reticle_plugin.h"
#include "reticle_elements.h"

#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/StyleSheetSpecification.h>

namespace QRmlUI
{

// File-scoped cached PropertyId values, populated during Initialise().
static Rml::PropertyId s_prop_radius = Rml::PropertyId::Invalid;
static Rml::PropertyId s_prop_stroke = Rml::PropertyId::Invalid;
static Rml::PropertyId s_prop_length = Rml::PropertyId::Invalid;
static Rml::PropertyId s_prop_width = Rml::PropertyId::Invalid;
static Rml::PropertyId s_prop_gap = Rml::PropertyId::Invalid;
static Rml::PropertyId s_prop_start_angle = Rml::PropertyId::Invalid;
static Rml::PropertyId s_prop_end_angle = Rml::PropertyId::Invalid;

// Element instancers — kept alive until Rml::Shutdown().
static Rml::ElementInstancerGeneric<ElementReticle>		*s_instancer_reticle = nullptr;
static Rml::ElementInstancerGeneric<ElementReticleDot>	*s_instancer_dot = nullptr;
static Rml::ElementInstancerGeneric<ElementReticleLine> *s_instancer_line = nullptr;
static Rml::ElementInstancerGeneric<ElementReticleRing> *s_instancer_ring = nullptr;
static Rml::ElementInstancerGeneric<ElementReticleArc>	*s_instancer_arc = nullptr;

void ReticlePlugin::Initialise ()
{
	// Register custom RCSS properties (all forces_layout=false → animatable via transitions)
	Rml::StyleSheetSpecification::RegisterProperty ("reticle-radius", "8dp", false, false).AddParser ("length");
	Rml::StyleSheetSpecification::RegisterProperty ("reticle-stroke", "1.5dp", false, false).AddParser ("length");
	Rml::StyleSheetSpecification::RegisterProperty ("reticle-length", "8dp", false, false).AddParser ("length");
	Rml::StyleSheetSpecification::RegisterProperty ("reticle-width", "2dp", false, false).AddParser ("length");
	Rml::StyleSheetSpecification::RegisterProperty ("reticle-gap", "3dp", false, false).AddParser ("length");
	Rml::StyleSheetSpecification::RegisterProperty ("reticle-start-angle", "0", false, false).AddParser ("number");
	Rml::StyleSheetSpecification::RegisterProperty ("reticle-end-angle", "360", false, false).AddParser ("number");

	// Cache PropertyId values for efficient lookup
	s_prop_radius = Rml::StyleSheetSpecification::GetPropertyId ("reticle-radius");
	s_prop_stroke = Rml::StyleSheetSpecification::GetPropertyId ("reticle-stroke");
	s_prop_length = Rml::StyleSheetSpecification::GetPropertyId ("reticle-length");
	s_prop_width = Rml::StyleSheetSpecification::GetPropertyId ("reticle-width");
	s_prop_gap = Rml::StyleSheetSpecification::GetPropertyId ("reticle-gap");
	s_prop_start_angle = Rml::StyleSheetSpecification::GetPropertyId ("reticle-start-angle");
	s_prop_end_angle = Rml::StyleSheetSpecification::GetPropertyId ("reticle-end-angle");

	// Register element instancers
	s_instancer_reticle = new Rml::ElementInstancerGeneric<ElementReticle> ();
	s_instancer_dot = new Rml::ElementInstancerGeneric<ElementReticleDot> ();
	s_instancer_line = new Rml::ElementInstancerGeneric<ElementReticleLine> ();
	s_instancer_ring = new Rml::ElementInstancerGeneric<ElementReticleRing> ();
	s_instancer_arc = new Rml::ElementInstancerGeneric<ElementReticleArc> ();

	Rml::Factory::RegisterElementInstancer ("reticle", s_instancer_reticle);
	Rml::Factory::RegisterElementInstancer ("reticle-dot", s_instancer_dot);
	Rml::Factory::RegisterElementInstancer ("reticle-line", s_instancer_line);
	Rml::Factory::RegisterElementInstancer ("reticle-ring", s_instancer_ring);
	Rml::Factory::RegisterElementInstancer ("reticle-arc", s_instancer_arc);
}

Rml::PropertyId ReticlePlugin::PropRadius ()
{
	return s_prop_radius;
}
Rml::PropertyId ReticlePlugin::PropStroke ()
{
	return s_prop_stroke;
}
Rml::PropertyId ReticlePlugin::PropLength ()
{
	return s_prop_length;
}
Rml::PropertyId ReticlePlugin::PropWidth ()
{
	return s_prop_width;
}
Rml::PropertyId ReticlePlugin::PropGap ()
{
	return s_prop_gap;
}
Rml::PropertyId ReticlePlugin::PropStartAngle ()
{
	return s_prop_start_angle;
}
Rml::PropertyId ReticlePlugin::PropEndAngle ()
{
	return s_prop_end_angle;
}

} // namespace QRmlUI
