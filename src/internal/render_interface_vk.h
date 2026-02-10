/*
 * vkQuake RmlUI - RmlUI Vulkan Render Interface
 *
 * Custom RmlUI render backend that integrates with vkQuake's Vulkan context.
 * Uses vkQuake's device, command buffers, and synchronization primitives.
 */

#ifndef QRMLUI_RENDER_INTERFACE_VK_H
#define QRMLUI_RENDER_INTERFACE_VK_H

#include <RmlUi/Core/RenderInterface.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include "vk_allocator.h"

// Forward declaration for vkQuake types
struct cb_context_s;
typedef struct cb_context_s cb_context_t;

namespace QRmlUI
{

// Configuration passed from vkQuake during initialization
struct VulkanConfig
{
	VkDevice			  device;
	VkPhysicalDevice	  physical_device;
	VkQueue				  graphics_queue;
	uint32_t			  queue_family_index;
	VkFormat			  color_format;
	VkFormat			  depth_format;
	VkSampleCountFlagBits sample_count;
	VkRenderPass		  render_pass;
	uint32_t			  subpass;

	// Memory properties for allocation
	VkPhysicalDeviceMemoryProperties memory_properties;

	// Function pointers from vkQuake's dispatch table
	PFN_vkCmdBindPipeline		cmd_bind_pipeline;
	PFN_vkCmdBindDescriptorSets cmd_bind_descriptor_sets;
	PFN_vkCmdBindVertexBuffers	cmd_bind_vertex_buffers;
	PFN_vkCmdBindIndexBuffer	cmd_bind_index_buffer;
	PFN_vkCmdDraw				cmd_draw;
	PFN_vkCmdDrawIndexed		cmd_draw_indexed;
	PFN_vkCmdPushConstants		cmd_push_constants;
	PFN_vkCmdSetScissor			cmd_set_scissor;
	PFN_vkCmdSetViewport		cmd_set_viewport;
};

class RenderInterface_VK : public Rml::RenderInterface
{
  public:
	RenderInterface_VK ();
	~RenderInterface_VK () override;

	// Initialize with vkQuake's Vulkan context
	bool Initialize (const VulkanConfig &config);
	void Shutdown ();
	bool IsInitialized () const
	{
		return m_initialized;
	}

	// Reinitialize with new render pass (preserves geometry/textures)
	bool Reinitialize (const VulkanConfig &config);

	// Frame management - call these around RmlUI rendering
	void BeginFrame (VkCommandBuffer cmd, int width, int height);
	void EndFrame ();

	// Per-frame render stats (reset in BeginFrame)
	uint32_t GetFrameDrawCalls () const
	{
		return m_frame_draw_calls;
	}
	uint32_t GetFrameIndices () const
	{
		return m_frame_indices;
	}

	// Garbage collection - call after GPU fence wait to safely destroy resources
	void CollectGarbage ();

	// Set the active command buffer (from vkQuake's cb_context_t)
	void SetCommandBuffer (VkCommandBuffer cmd);

	// -- Inherited from Rml::RenderInterface --

	Rml::CompiledGeometryHandle CompileGeometry (Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
	void						RenderGeometry (Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
	void						ReleaseGeometry (Rml::CompiledGeometryHandle geometry) override;

	Rml::TextureHandle LoadTexture (Rml::Vector2i &texture_dimensions, const Rml::String &source) override;
	Rml::TextureHandle GenerateTexture (Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override;
	void			   ReleaseTexture (Rml::TextureHandle texture) override;

	void EnableScissorRegion (bool enable) override;
	void SetScissorRegion (Rml::Rectanglei region) override;

	void SetTransform (const Rml::Matrix4f *transform) override;

  private:
	// Internal geometry data — uses pool-based buffer allocations
	struct GeometryData
	{
		BufferAllocation vertex_alloc;
		BufferAllocation index_alloc;
		int				 num_indices;
	};

	// Internal texture data — uses pool-based image memory allocations
	struct TextureData
	{
		VkImage				  image;
		VkImageView			  view;
		VkSampler			  sampler;
		ImageMemoryAllocation memory_alloc;
		VkDescriptorSet		  descriptor_set;
		Rml::Vector2i		  dimensions;
	};

	// Pending async texture upload — fence tracks GPU completion
	struct PendingUpload
	{
		VkFence		   fence;
		VkCommandPool  cmd_pool;
		VkBuffer	   staging_buffer;
		VkDeviceMemory staging_memory;
	};

	// Push constant data for vertex shader
	struct PushConstants
	{
		float transform[16];
		float translation[2];
		float padding[2];
	};

	// Vulkan resource creation helpers
	bool CreatePipeline ();
	bool CreateDescriptorPool ();
	bool CreateDescriptorSetLayout ();
	bool CreateSampler ();
	void DestroyPipelines ();

	VkBuffer CreateBuffer (VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkDeviceMemory &memory);
	uint32_t FindMemoryType (uint32_t type_filter, VkMemoryPropertyFlags properties);

	void DestroyBuffer (VkBuffer buffer, VkDeviceMemory memory);
	void DestroyTexture (TextureData *texture);

	// Configuration from vkQuake
	VulkanConfig m_config;

	// Current frame state
	VkCommandBuffer m_current_cmd;
	int				m_viewport_width;
	int				m_viewport_height;
	bool			m_scissor_enabled;
	VkRect2D		m_scissor_rect;
	Rml::Matrix4f	m_transform;
	bool			m_transform_enabled;
	uint32_t		m_frame_draw_calls;
	uint32_t		m_frame_indices;

	// Vulkan resources
	VkPipeline			  m_pipeline_textured;
	VkPipeline			  m_pipeline_untextured;
	VkPipelineLayout	  m_pipeline_layout;
	VkDescriptorPool	  m_descriptor_pool;
	VkDescriptorSetLayout m_texture_set_layout;
	VkSampler			  m_sampler;

	// Default white texture for untextured geometry
	TextureData *m_white_texture;

	// Resource tracking
	std::unordered_map<Rml::CompiledGeometryHandle, GeometryData *> m_geometries;
	std::unordered_map<Rml::TextureHandle, TextureData *>			m_textures;
	Rml::CompiledGeometryHandle										m_next_geometry_handle;
	Rml::TextureHandle												m_next_texture_handle;

	bool m_initialized;

	// Memory pools
	BufferPool		m_buffer_pool;
	ImageMemoryPool m_image_pool;

	// Async texture uploads pending GPU completion
	std::vector<PendingUpload> m_pending_uploads;

	// Garbage collection for deferred resource destruction
	// Uses double-buffering: resources queued in slot N are destroyed when slot N is processed
	// (which happens after the GPU fence for that frame has been waited on)
	static constexpr int		GARBAGE_SLOTS = 2;
	int							m_garbage_index;
	std::vector<GeometryData *> m_geometry_garbage[GARBAGE_SLOTS];
	std::vector<TextureData *>	m_texture_garbage[GARBAGE_SLOTS];
};

} // namespace QRmlUI

#endif // QRMLUI_RENDER_INTERFACE_VK_H
