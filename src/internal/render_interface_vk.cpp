/*
 * vkQuake RmlUI - RmlUI Vulkan Render Interface Implementation
 *
 * Integrates RmlUI with vkQuake's Vulkan context for UI rendering.
 */

#include "render_interface_vk.h"
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <cstdint>
#include <cstring>

// stb_image for texture loading (implementation is in Quake/image.c)
// Use extern "C" because the implementation is compiled as C code
#define STBI_NO_STDIO
extern "C" {
#include "stb_image.h"
}

// Embedded SPIR-V shaders (generated from GLSL)
#include "rmlui_shaders_embedded.h"

namespace QRmlUI {

RenderInterface_VK::RenderInterface_VK()
    : m_config{}
    , m_current_cmd(VK_NULL_HANDLE)
    , m_viewport_width(0)
    , m_viewport_height(0)
    , m_scissor_enabled(false)
    , m_scissor_rect{}
    , m_transform_enabled(false)
    , m_pipeline_textured(VK_NULL_HANDLE)
    , m_pipeline_untextured(VK_NULL_HANDLE)
    , m_pipeline_layout(VK_NULL_HANDLE)
    , m_descriptor_pool(VK_NULL_HANDLE)
    , m_texture_set_layout(VK_NULL_HANDLE)
    , m_sampler(VK_NULL_HANDLE)
    , m_white_texture(nullptr)
    , m_next_geometry_handle(1)
    , m_next_texture_handle(1)
    , m_initialized(false)
    , m_garbage_index(0)
{
    m_transform = Rml::Matrix4f::Identity();
}

RenderInterface_VK::~RenderInterface_VK()
{
    if (m_initialized) {
        Shutdown();
    }
}

bool RenderInterface_VK::Initialize(const VulkanConfig& config)
{
    m_config = config;

    if (!CreateDescriptorSetLayout()) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to create descriptor set layout");
        return false;
    }

    if (!CreateDescriptorPool()) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to create descriptor pool");
        return false;
    }

    if (!CreateSampler()) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to create sampler");
        return false;
    }

    if (!CreatePipeline()) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to create pipeline");
        return false;
    }

    // Initialize memory pools
    m_buffer_pool.Initialize(m_config.device, m_config.memory_properties);

    VkPhysicalDeviceProperties device_props;
    vkGetPhysicalDeviceProperties(m_config.physical_device, &device_props);
    m_image_pool.Initialize(m_config.device, m_config.memory_properties,
                            device_props.limits.bufferImageGranularity);

    // Create default white texture for untextured geometry
    Rml::byte white_pixel[] = {255, 255, 255, 255};
    auto white_handle = GenerateTexture(Rml::Span<const Rml::byte>(white_pixel, 4), {1, 1});
    if (white_handle) {
        m_white_texture = m_textures[white_handle];
    }

    m_initialized = true;
    return true;
}

void RenderInterface_VK::Shutdown()
{
    if (!m_initialized) return;

    vkDeviceWaitIdle(m_config.device);

    // Flush pending texture uploads (all fences guaranteed signaled after vkDeviceWaitIdle)
    for (auto& upload : m_pending_uploads) {
        vkDestroyFence(m_config.device, upload.fence, nullptr);
        vkDestroyCommandPool(m_config.device, upload.cmd_pool, nullptr);
        DestroyBuffer(upload.staging_buffer, upload.staging_memory);
    }
    m_pending_uploads.clear();

    // Release all geometries
    for (auto& pair : m_geometries) {
        m_buffer_pool.Free(pair.second->vertex_alloc);
        m_buffer_pool.Free(pair.second->index_alloc);
        delete pair.second;
    }
    m_geometries.clear();

    // Release all textures
    for (auto& pair : m_textures) {
        DestroyTexture(pair.second);
        m_image_pool.Free(pair.second->memory_alloc);
        delete pair.second;
    }
    m_textures.clear();
    m_white_texture = nullptr;

    // Clean up any pending garbage (safe since we called vkDeviceWaitIdle)
    for (int slot = 0; slot < GARBAGE_SLOTS; ++slot) {
        for (GeometryData* geometry : m_geometry_garbage[slot]) {
            m_buffer_pool.Free(geometry->vertex_alloc);
            m_buffer_pool.Free(geometry->index_alloc);
            delete geometry;
        }
        m_geometry_garbage[slot].clear();

        for (TextureData* texture : m_texture_garbage[slot]) {
            DestroyTexture(texture);
            m_image_pool.Free(texture->memory_alloc);
            delete texture;
        }
        m_texture_garbage[slot].clear();
    }

    // Shutdown pools before destroying pipeline resources
    m_buffer_pool.Shutdown();
    m_image_pool.Shutdown();

    DestroyPipelines();

    m_initialized = false;
}

void RenderInterface_VK::DestroyPipelines()
{
    // Destroy Vulkan pipeline resources only (not geometry/textures)
    if (m_pipeline_textured != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_config.device, m_pipeline_textured, nullptr);
        m_pipeline_textured = VK_NULL_HANDLE;
    }
    if (m_pipeline_untextured != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_config.device, m_pipeline_untextured, nullptr);
        m_pipeline_untextured = VK_NULL_HANDLE;
    }
    if (m_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_config.device, m_pipeline_layout, nullptr);
        m_pipeline_layout = VK_NULL_HANDLE;
    }
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_config.device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
    if (m_descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_config.device, m_descriptor_pool, nullptr);
        m_descriptor_pool = VK_NULL_HANDLE;
    }
    if (m_texture_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_config.device, m_texture_set_layout, nullptr);
        m_texture_set_layout = VK_NULL_HANDLE;
    }
}

bool RenderInterface_VK::Reinitialize(const VulkanConfig& config)
{
    // Reinitialize with a new render pass while preserving geometry
    // This is called when vkQuake's render resources are recreated

    if (!m_initialized) {
        return Initialize(config);
    }

    vkDeviceWaitIdle(m_config.device);

    // Destroy old pipelines (they reference the old render pass)
    if (m_pipeline_textured != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_config.device, m_pipeline_textured, nullptr);
        m_pipeline_textured = VK_NULL_HANDLE;
    }
    if (m_pipeline_untextured != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_config.device, m_pipeline_untextured, nullptr);
        m_pipeline_untextured = VK_NULL_HANDLE;
    }
    if (m_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_config.device, m_pipeline_layout, nullptr);
        m_pipeline_layout = VK_NULL_HANDLE;
    }

    // Update config with new render pass
    m_config = config;

    // Recreate pipeline with new render pass
    if (!CreatePipeline()) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to recreate pipeline");
        return false;
    }

    return true;
}

void RenderInterface_VK::BeginFrame(VkCommandBuffer cmd, int width, int height)
{
    m_current_cmd = cmd;
    m_viewport_width = width;
    m_viewport_height = height;

    // Set viewport
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    if (m_config.cmd_set_viewport) {
        m_config.cmd_set_viewport(cmd, 0, 1, &viewport);
    } else {
        vkCmdSetViewport(cmd, 0, 1, &viewport);
    }

    // Reset scissor to full viewport
    m_scissor_rect = {{0, 0}, {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}};
    m_scissor_enabled = false;
}

void RenderInterface_VK::EndFrame()
{
    m_current_cmd = VK_NULL_HANDLE;
}

void RenderInterface_VK::CollectGarbage()
{
    // Poll pending texture uploads — destroy signaled entries
    for (auto it = m_pending_uploads.begin(); it != m_pending_uploads.end(); ) {
        if (vkGetFenceStatus(m_config.device, it->fence) == VK_SUCCESS) {
            vkDestroyFence(m_config.device, it->fence, nullptr);
            vkDestroyCommandPool(m_config.device, it->cmd_pool, nullptr);
            DestroyBuffer(it->staging_buffer, it->staging_memory);
            it = m_pending_uploads.erase(it);
        } else {
            ++it;
        }
    }

    // Toggle to the other slot - resources queued there are from 2 frames ago
    // and are now safe to destroy (GPU fence for that frame has been waited on)
    m_garbage_index = (m_garbage_index + 1) % GARBAGE_SLOTS;

    // Destroy all geometries in this slot
    for (GeometryData* geometry : m_geometry_garbage[m_garbage_index]) {
        m_buffer_pool.Free(geometry->vertex_alloc);
        m_buffer_pool.Free(geometry->index_alloc);
        delete geometry;
    }
    m_geometry_garbage[m_garbage_index].clear();

    // Destroy all textures in this slot
    for (TextureData* texture : m_texture_garbage[m_garbage_index]) {
        DestroyTexture(texture);
        m_image_pool.Free(texture->memory_alloc);
        delete texture;
    }
    m_texture_garbage[m_garbage_index].clear();
}

void RenderInterface_VK::SetCommandBuffer(VkCommandBuffer cmd)
{
    m_current_cmd = cmd;
}

Rml::CompiledGeometryHandle RenderInterface_VK::CompileGeometry(
    Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
    auto* geometry = new GeometryData();
    geometry->num_indices = static_cast<int>(indices.size());

    VkDeviceSize vertex_size = vertices.size() * sizeof(Rml::Vertex);
    VkDeviceSize index_size = indices.size() * sizeof(int);

    // Allocate vertex buffer from pool
    if (!m_buffer_pool.Allocate(vertex_size, 16, geometry->vertex_alloc)) {
        delete geometry;
        return 0;
    }

    // Copy vertex data directly into persistently mapped memory
    memcpy(geometry->vertex_alloc.mapped_ptr, vertices.data(), vertex_size);

    // Allocate index buffer from pool
    if (!m_buffer_pool.Allocate(index_size, 4, geometry->index_alloc)) {
        m_buffer_pool.Free(geometry->vertex_alloc);
        delete geometry;
        return 0;
    }

    // Copy index data directly into persistently mapped memory
    memcpy(geometry->index_alloc.mapped_ptr, indices.data(), index_size);

    Rml::CompiledGeometryHandle handle = m_next_geometry_handle++;
    m_geometries[handle] = geometry;
    return handle;
}

void RenderInterface_VK::RenderGeometry(Rml::CompiledGeometryHandle geometry_handle,
                                         Rml::Vector2f translation,
                                         Rml::TextureHandle texture_handle)
{
    if (m_current_cmd == VK_NULL_HANDLE) return;

    auto geom_it = m_geometries.find(geometry_handle);
    if (geom_it == m_geometries.end()) return;

    GeometryData* geometry = geom_it->second;
    TextureData* texture = nullptr;

    if (texture_handle) {
        auto tex_it = m_textures.find(texture_handle);
        if (tex_it != m_textures.end()) {
            texture = tex_it->second;
        }
    }

    // Use white texture if no texture specified
    if (!texture) {
        texture = m_white_texture;
    }

    // Select pipeline based on whether we have a texture
    VkPipeline pipeline = texture ? m_pipeline_textured : m_pipeline_untextured;

    // Bind pipeline
    auto bind_pipeline = m_config.cmd_bind_pipeline ? m_config.cmd_bind_pipeline : vkCmdBindPipeline;
    bind_pipeline(m_current_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Set scissor
    auto set_scissor = m_config.cmd_set_scissor ? m_config.cmd_set_scissor : vkCmdSetScissor;
    if (m_scissor_enabled) {
        set_scissor(m_current_cmd, 0, 1, &m_scissor_rect);
    } else {
        VkRect2D full_scissor = {{0, 0}, {static_cast<uint32_t>(m_viewport_width),
                                          static_cast<uint32_t>(m_viewport_height)}};
        set_scissor(m_current_cmd, 0, 1, &full_scissor);
    }

    // Bind texture descriptor set
    if (texture && texture->descriptor_set) {
        auto bind_desc = m_config.cmd_bind_descriptor_sets ? m_config.cmd_bind_descriptor_sets
                                                           : vkCmdBindDescriptorSets;
        bind_desc(m_current_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout,
                  0, 1, &texture->descriptor_set, 0, nullptr);
    }

    // Push constants with transform and translation
    PushConstants push_constants{};

    // Build orthographic projection matrix
    float L = 0.0f;
    float R = static_cast<float>(m_viewport_width);
    float T = 0.0f;
    float B = static_cast<float>(m_viewport_height);

    Rml::Matrix4f projection = Rml::Matrix4f::FromRows(
        {2.0f / (R - L), 0.0f, 0.0f, (R + L) / (L - R)},
        {0.0f, 2.0f / (B - T), 0.0f, (T + B) / (T - B)},
        {0.0f, 0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f}
    );

    Rml::Matrix4f mvp = projection;
    if (m_transform_enabled) {
        mvp = projection * m_transform;
    }

    memcpy(push_constants.transform, mvp.data(), sizeof(push_constants.transform));
    push_constants.translation[0] = translation.x;
    push_constants.translation[1] = translation.y;

    auto push_const = m_config.cmd_push_constants ? m_config.cmd_push_constants : vkCmdPushConstants;
    push_const(m_current_cmd, m_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
               sizeof(PushConstants), &push_constants);

    // Bind vertex buffer at suballocation offset
    VkDeviceSize vertex_offset = geometry->vertex_alloc.offset;
    auto bind_vb = m_config.cmd_bind_vertex_buffers ? m_config.cmd_bind_vertex_buffers
                                                    : vkCmdBindVertexBuffers;
    bind_vb(m_current_cmd, 0, 1, &geometry->vertex_alloc.buffer, &vertex_offset);

    // Bind index buffer at suballocation offset
    auto bind_ib = m_config.cmd_bind_index_buffer ? m_config.cmd_bind_index_buffer
                                                  : vkCmdBindIndexBuffer;
    bind_ib(m_current_cmd, geometry->index_alloc.buffer, geometry->index_alloc.offset,
            VK_INDEX_TYPE_UINT32);

    // Draw
    auto draw_indexed = m_config.cmd_draw_indexed ? m_config.cmd_draw_indexed : vkCmdDrawIndexed;
    draw_indexed(m_current_cmd, geometry->num_indices, 1, 0, 0, 0);
}

void RenderInterface_VK::ReleaseGeometry(Rml::CompiledGeometryHandle geometry_handle)
{
    auto it = m_geometries.find(geometry_handle);
    if (it == m_geometries.end()) return;

    // Queue for deferred destruction - the geometry may still be referenced
    // by in-flight command buffers
    GeometryData* geometry = it->second;
    m_geometry_garbage[m_garbage_index].push_back(geometry);
    m_geometries.erase(it);
}

Rml::TextureHandle RenderInterface_VK::LoadTexture(Rml::Vector2i& texture_dimensions,
                                                    const Rml::String& source)
{
    Rml::FileInterface* file_interface = Rml::GetFileInterface();
    Rml::FileHandle file = file_interface->Open(source);
    if (!file) {
        Rml::Log::Message(Rml::Log::LT_WARNING, "Failed to open texture file: %s", source.c_str());
        return 0;
    }

    size_t file_size = file_interface->Length(file);
    std::vector<Rml::byte> file_data(file_size);
    file_interface->Read(file_data.data(), file_size, file);
    file_interface->Close(file);

    // Use stb_image to decode the image
    int width, height, channels;
    unsigned char* image_data = stbi_load_from_memory(
        file_data.data(), static_cast<int>(file_size),
        &width, &height, &channels, 4  // Force RGBA output
    );

    if (!image_data) {
        Rml::Log::Message(Rml::Log::LT_WARNING, "Failed to decode texture: %s (%s)",
                          source.c_str(), stbi_failure_reason());
        return 0;
    }

    texture_dimensions.x = width;
    texture_dimensions.y = height;

    // Generate the texture using the decoded image data
    Rml::TextureHandle handle = GenerateTexture(
        Rml::Span<const Rml::byte>(image_data, width * height * 4),
        texture_dimensions
    );

    stbi_image_free(image_data);

    if (handle) {
        Rml::Log::Message(Rml::Log::LT_DEBUG, "Loaded texture: %s (%dx%d)",
                          source.c_str(), width, height);
    }

    return handle;
}

Rml::TextureHandle RenderInterface_VK::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                        Rml::Vector2i source_dimensions)
{
    auto* texture = new TextureData();
    texture->dimensions = source_dimensions;

    VkDeviceSize image_size = source_dimensions.x * source_dimensions.y * 4;

    // Create staging buffer (still uses individual allocation — transient, freed after upload)
    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;
    staging_buffer = CreateBuffer(
        image_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging_memory
    );

    void* data;
    vkMapMemory(m_config.device, staging_memory, 0, image_size, 0, &data);
    memcpy(data, source.data(), image_size);
    vkUnmapMemory(m_config.device, staging_memory);

    // Create image
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = source_dimensions.x;
    image_info.extent.height = source_dimensions.y;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_config.device, &image_info, nullptr, &texture->image) != VK_SUCCESS) {
        DestroyBuffer(staging_buffer, staging_memory);
        delete texture;
        return 0;
    }

    // Allocate image memory from pool
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(m_config.device, texture->image, &mem_reqs);

    if (!m_image_pool.Allocate(mem_reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                               texture->memory_alloc)) {
        vkDestroyImage(m_config.device, texture->image, nullptr);
        DestroyBuffer(staging_buffer, staging_memory);
        delete texture;
        return 0;
    }

    vkBindImageMemory(m_config.device, texture->image,
                      texture->memory_alloc.memory, texture->memory_alloc.offset);

    // Create command buffer for image transfer
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = m_config.queue_family_index;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkCommandPool cmd_pool;
    vkCreateCommandPool(m_config.device, &pool_info, nullptr, &cmd_pool);

    VkCommandBufferAllocateInfo cmd_alloc_info{};
    cmd_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_alloc_info.commandPool = cmd_pool;
    cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_config.device, &cmd_alloc_info, &cmd);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    // Transition image to transfer destination
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(source_dimensions.x),
                          static_cast<uint32_t>(source_dimensions.y), 1};

    vkCmdCopyBufferToImage(cmd, staging_buffer, texture->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition image to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    // Submit with fence for async completion tracking (no vkQueueWaitIdle stall)
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence upload_fence;
    vkCreateFence(m_config.device, &fence_info, nullptr, &upload_fence);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    vkQueueSubmit(m_config.graphics_queue, 1, &submit_info, upload_fence);

    // Defer staging resource cleanup until fence signals
    m_pending_uploads.push_back({upload_fence, cmd_pool, staging_buffer, staging_memory});

    // Create image view (host-side operation, safe immediately)
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = texture->image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_config.device, &view_info, nullptr, &texture->view) != VK_SUCCESS) {
        m_image_pool.Free(texture->memory_alloc);
        vkDestroyImage(m_config.device, texture->image, nullptr);
        delete texture;
        return 0;
    }

    texture->sampler = m_sampler;

    // Allocate descriptor set for this texture
    VkDescriptorSetAllocateInfo desc_alloc_info{};
    desc_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    desc_alloc_info.descriptorPool = m_descriptor_pool;
    desc_alloc_info.descriptorSetCount = 1;
    desc_alloc_info.pSetLayouts = &m_texture_set_layout;

    if (vkAllocateDescriptorSets(m_config.device, &desc_alloc_info, &texture->descriptor_set) != VK_SUCCESS) {
        vkDestroyImageView(m_config.device, texture->view, nullptr);
        m_image_pool.Free(texture->memory_alloc);
        vkDestroyImage(m_config.device, texture->image, nullptr);
        delete texture;
        return 0;
    }

    // Update descriptor set
    VkDescriptorImageInfo image_desc_info{};
    image_desc_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_desc_info.imageView = texture->view;
    image_desc_info.sampler = texture->sampler;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = texture->descriptor_set;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &image_desc_info;

    vkUpdateDescriptorSets(m_config.device, 1, &write, 0, nullptr);

    Rml::TextureHandle handle = m_next_texture_handle++;
    m_textures[handle] = texture;
    return handle;
}

void RenderInterface_VK::ReleaseTexture(Rml::TextureHandle texture_handle)
{
    auto it = m_textures.find(texture_handle);
    if (it == m_textures.end()) return;

    TextureData* texture = it->second;

    // Don't delete the white texture - it's needed for the lifetime of the renderer
    if (texture == m_white_texture) {
        return;
    }

    // Queue for deferred destruction - the texture may still be referenced
    // by in-flight command buffers
    m_texture_garbage[m_garbage_index].push_back(texture);
    m_textures.erase(it);
}

void RenderInterface_VK::EnableScissorRegion(bool enable)
{
    m_scissor_enabled = enable;
}

void RenderInterface_VK::SetScissorRegion(Rml::Rectanglei region)
{
    m_scissor_rect.offset.x = region.Left();
    m_scissor_rect.offset.y = region.Top();
    m_scissor_rect.extent.width = region.Width();
    m_scissor_rect.extent.height = region.Height();
}

void RenderInterface_VK::SetTransform(const Rml::Matrix4f* transform)
{
    if (transform) {
        m_transform = *transform;
        m_transform_enabled = true;
    } else {
        m_transform = Rml::Matrix4f::Identity();
        m_transform_enabled = false;
    }
}

bool RenderInterface_VK::CreateDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;

    return vkCreateDescriptorSetLayout(m_config.device, &layout_info, nullptr,
                                        &m_texture_set_layout) == VK_SUCCESS;
}

bool RenderInterface_VK::CreateDescriptorPool()
{
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1000;  // Support many textures

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = 1000;

    return vkCreateDescriptorPool(m_config.device, &pool_info, nullptr,
                                   &m_descriptor_pool) == VK_SUCCESS;
}

bool RenderInterface_VK::CreateSampler()
{
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.maxAnisotropy = 1.0f;
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    return vkCreateSampler(m_config.device, &sampler_info, nullptr, &m_sampler) == VK_SUCCESS;
}

bool RenderInterface_VK::CreatePipeline()
{
    // Push constant range for transform matrix and translation
    VkPushConstantRange push_constant_range{};
    push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstants);

    // Pipeline layout
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &m_texture_set_layout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_constant_range;

    if (vkCreatePipelineLayout(m_config.device, &layout_info, nullptr, &m_pipeline_layout) != VK_SUCCESS) {
        return false;
    }

    // Create shader modules from embedded SPIR-V
    VkShaderModuleCreateInfo vert_info{};
    vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vert_info.codeSize = rmlui_vert_spv_len;
    vert_info.pCode = reinterpret_cast<const uint32_t*>(rmlui_vert_spv);

    VkShaderModule vert_module;
    if (vkCreateShaderModule(m_config.device, &vert_info, nullptr, &vert_module) != VK_SUCCESS) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to create vertex shader module");
        return false;
    }

    VkShaderModuleCreateInfo frag_info{};
    frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    frag_info.codeSize = rmlui_frag_spv_len;
    frag_info.pCode = reinterpret_cast<const uint32_t*>(rmlui_frag_spv);

    VkShaderModule frag_module;
    if (vkCreateShaderModule(m_config.device, &frag_info, nullptr, &frag_module) != VK_SUCCESS) {
        vkDestroyShaderModule(m_config.device, vert_module, nullptr);
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to create textured fragment shader module");
        return false;
    }

    VkShaderModuleCreateInfo frag_notex_info{};
    frag_notex_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    frag_notex_info.codeSize = rmlui_notex_frag_spv_len;
    frag_notex_info.pCode = reinterpret_cast<const uint32_t*>(rmlui_notex_frag_spv);

    VkShaderModule frag_notex_module;
    if (vkCreateShaderModule(m_config.device, &frag_notex_info, nullptr, &frag_notex_module) != VK_SUCCESS) {
        vkDestroyShaderModule(m_config.device, vert_module, nullptr);
        vkDestroyShaderModule(m_config.device, frag_module, nullptr);
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to create untextured fragment shader module");
        return false;
    }

    // Shader stage info
    VkPipelineShaderStageCreateInfo vert_stage{};
    vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_module;
    vert_stage.pName = "main";

    VkPipelineShaderStageCreateInfo frag_stage{};
    frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_module;
    frag_stage.pName = "main";

    VkPipelineShaderStageCreateInfo frag_notex_stage{};
    frag_notex_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_notex_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_notex_stage.module = frag_notex_module;
    frag_notex_stage.pName = "main";

    VkPipelineShaderStageCreateInfo stages_textured[] = {vert_stage, frag_stage};
    VkPipelineShaderStageCreateInfo stages_untextured[] = {vert_stage, frag_notex_stage};

    // Vertex input - RmlUI vertex format: position (vec2), color (u8vec4), texcoord (vec2)
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Rml::Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[3]{};
    // Position (vec2)
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[0].offset = offsetof(Rml::Vertex, position);

    // Color (u8vec4 normalized) - RmlUI uses RGBA u8
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    attributes[1].offset = offsetof(Rml::Vertex, colour);

    // Texcoord (vec2)
    attributes[2].binding = 0;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[2].offset = offsetof(Rml::Vertex, tex_coord);

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = 3;
    vertex_input.pVertexAttributeDescriptions = attributes;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport and scissor (dynamic state)
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    // Rasterization
    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterization.lineWidth = 1.0f;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = m_config.sample_count;

    // Depth/stencil (disabled for UI)
    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_FALSE;
    depth_stencil.depthWriteEnable = VK_FALSE;
    depth_stencil.stencilTestEnable = VK_FALSE;

    // Color blending - premultiplied alpha
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.blendEnable = VK_TRUE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &blend_attachment;

    // Dynamic state (viewport and scissor)
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    // Create textured pipeline
    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages_textured;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = m_pipeline_layout;
    pipeline_info.renderPass = m_config.render_pass;
    pipeline_info.subpass = m_config.subpass;

    if (vkCreateGraphicsPipelines(m_config.device, VK_NULL_HANDLE, 1, &pipeline_info,
                                   nullptr, &m_pipeline_textured) != VK_SUCCESS) {
        vkDestroyShaderModule(m_config.device, vert_module, nullptr);
        vkDestroyShaderModule(m_config.device, frag_module, nullptr);
        vkDestroyShaderModule(m_config.device, frag_notex_module, nullptr);
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to create textured pipeline");
        return false;
    }

    // Create untextured pipeline
    pipeline_info.pStages = stages_untextured;

    if (vkCreateGraphicsPipelines(m_config.device, VK_NULL_HANDLE, 1, &pipeline_info,
                                   nullptr, &m_pipeline_untextured) != VK_SUCCESS) {
        vkDestroyShaderModule(m_config.device, vert_module, nullptr);
        vkDestroyShaderModule(m_config.device, frag_module, nullptr);
        vkDestroyShaderModule(m_config.device, frag_notex_module, nullptr);
        vkDestroyPipeline(m_config.device, m_pipeline_textured, nullptr);
        m_pipeline_textured = VK_NULL_HANDLE;
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to create untextured pipeline");
        return false;
    }

    // Clean up shader modules (no longer needed after pipeline creation)
    vkDestroyShaderModule(m_config.device, vert_module, nullptr);
    vkDestroyShaderModule(m_config.device, frag_module, nullptr);
    vkDestroyShaderModule(m_config.device, frag_notex_module, nullptr);

    return true;
}

VkBuffer RenderInterface_VK::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                           VkMemoryPropertyFlags properties, VkDeviceMemory& memory)
{
    VkBuffer buffer;

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_config.device, &buffer_info, nullptr, &buffer) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(m_config.device, buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = FindMemoryType(mem_reqs.memoryTypeBits, properties);
    if (alloc_info.memoryTypeIndex == UINT32_MAX) {
        Rml::Log::Message(Rml::Log::LT_WARNING, "CreateBuffer: no suitable memory type");
        vkDestroyBuffer(m_config.device, buffer, nullptr);
        return VK_NULL_HANDLE;
    }

    if (vkAllocateMemory(m_config.device, &alloc_info, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(m_config.device, buffer, nullptr);
        return VK_NULL_HANDLE;
    }

    vkBindBufferMemory(m_config.device, buffer, memory, 0);
    return buffer;
}

uint32_t RenderInterface_VK::FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties)
{
    return FindVulkanMemoryType(m_config.memory_properties, type_filter, properties);
}

void RenderInterface_VK::DestroyBuffer(VkBuffer buffer, VkDeviceMemory memory)
{
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_config.device, buffer, nullptr);
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_config.device, memory, nullptr);
    }
}

void RenderInterface_VK::DestroyTexture(TextureData* texture)
{
    if (texture->descriptor_set != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(m_config.device, m_descriptor_pool, 1, &texture->descriptor_set);
    }
    if (texture->view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_config.device, texture->view, nullptr);
    }
    if (texture->image != VK_NULL_HANDLE) {
        vkDestroyImage(m_config.device, texture->image, nullptr);
    }
    // Note: memory is freed separately via m_image_pool.Free() by the caller
}

} // namespace QRmlUI
