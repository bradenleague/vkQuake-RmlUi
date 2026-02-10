/*
 * vkQuake RmlUI - Reticle Custom Elements
 *
 * Custom RmlUI elements for procedural reticle rendering:
 *   <reticle>       — container, provides coordinate space
 *   <reticle-dot>   — filled circle
 *   <reticle-line>  — rotated rectangle arm
 *   <reticle-ring>  — ring (full circle stroke)
 *   <reticle-arc>   — partial ring
 */

#pragma once

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>

namespace QRmlUI
{

// Container element. No own geometry; children render relative to its content box center.
class ElementReticle : public Rml::Element
{
  public:
	RMLUI_RTTI_DefineWithParent (ElementReticle, Rml::Element)

		explicit ElementReticle (const Rml::String &tag);

  protected:
	bool GetIntrinsicDimensions (Rml::Vector2f &dimensions, float &ratio) override;
};

// Filled circle.
class ElementReticleDot : public Rml::Element
{
  public:
	RMLUI_RTTI_DefineWithParent (ElementReticleDot, Rml::Element)

		explicit ElementReticleDot (const Rml::String &tag);

  protected:
	void OnRender () override;
	void OnResize () override;
	void OnPropertyChange (const Rml::PropertyIdSet &changed_properties) override;
	bool GetIntrinsicDimensions (Rml::Vector2f &dimensions, float &ratio) override;

  private:
	void		  GenerateGeometry ();
	Rml::Geometry m_geometry;
	bool		  m_geometry_dirty = true;
};

// Rotated rectangle arm. Direction set via angle="" HTML attribute.
class ElementReticleLine : public Rml::Element
{
  public:
	RMLUI_RTTI_DefineWithParent (ElementReticleLine, Rml::Element)

		explicit ElementReticleLine (const Rml::String &tag);

  protected:
	void OnRender () override;
	void OnResize () override;
	void OnPropertyChange (const Rml::PropertyIdSet &changed_properties) override;
	void OnAttributeChange (const Rml::ElementAttributes &changed_attributes) override;
	bool GetIntrinsicDimensions (Rml::Vector2f &dimensions, float &ratio) override;

  private:
	void		  GenerateGeometry ();
	Rml::Geometry m_geometry;
	bool		  m_geometry_dirty = true;
};

// Full ring (circle stroke).
class ElementReticleRing : public Rml::Element
{
  public:
	RMLUI_RTTI_DefineWithParent (ElementReticleRing, Rml::Element)

		explicit ElementReticleRing (const Rml::String &tag);

  protected:
	void OnRender () override;
	void OnResize () override;
	void OnPropertyChange (const Rml::PropertyIdSet &changed_properties) override;
	bool GetIntrinsicDimensions (Rml::Vector2f &dimensions, float &ratio) override;

  private:
	void		  GenerateGeometry ();
	Rml::Geometry m_geometry;
	bool		  m_geometry_dirty = true;
};

// Partial ring (arc).
class ElementReticleArc : public Rml::Element
{
  public:
	RMLUI_RTTI_DefineWithParent (ElementReticleArc, Rml::Element)

		explicit ElementReticleArc (const Rml::String &tag);

  protected:
	void OnRender () override;
	void OnResize () override;
	void OnPropertyChange (const Rml::PropertyIdSet &changed_properties) override;
	bool GetIntrinsicDimensions (Rml::Vector2f &dimensions, float &ratio) override;

  private:
	void		  GenerateGeometry ();
	Rml::Geometry m_geometry;
	bool		  m_geometry_dirty = true;
};

} // namespace QRmlUI
