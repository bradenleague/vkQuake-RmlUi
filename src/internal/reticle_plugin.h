/*
 * vkQuake RmlUI - Reticle Plugin
 *
 * Registers custom RCSS properties and element instancers for reticle primitives.
 * Must be called before Rml::Initialise().
 */

#pragma once

#include <RmlUi/Core/ID.h>

namespace QRmlUI
{

class ReticlePlugin
{
  public:
	// Call before Rml::Initialise() to register properties and element instancers.
	static void Initialise ();

	// Cached PropertyId accessors for efficient lookup in OnPropertyChange().
	static Rml::PropertyId PropRadius ();
	static Rml::PropertyId PropStroke ();
	static Rml::PropertyId PropLength ();
	static Rml::PropertyId PropWidth ();
	static Rml::PropertyId PropGap ();
	static Rml::PropertyId PropStartAngle ();
	static Rml::PropertyId PropEndAngle ();
};

} // namespace QRmlUI
