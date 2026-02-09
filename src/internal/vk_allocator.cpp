/*
 * vkQuake RmlUI - Vulkan Suballocator Implementation
 *
 * FreeListAllocator: pure offset/size bookkeeping with first-fit and coalesce.
 * BufferPool: suballocates HOST_VISIBLE geometry buffers from large chunks.
 * ImageMemoryPool: suballocates DEVICE_LOCAL memory for texture images.
 */

#include "vk_allocator.h"
#include <RmlUi/Core/Log.h>
#include <algorithm>
#include <cassert>
#include <cstring>

#include "engine_bridge.h"

namespace QRmlUI {

// ---------------------------------------------------------------------------
// FindVulkanMemoryType
// ---------------------------------------------------------------------------

uint32_t FindVulkanMemoryType(const VkPhysicalDeviceMemoryProperties& mem_props,
                              uint32_t type_filter, VkMemoryPropertyFlags properties)
{
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

// ---------------------------------------------------------------------------
// FreeListAllocator
// ---------------------------------------------------------------------------

void FreeListAllocator::Reset(VkDeviceSize capacity)
{
    m_capacity = capacity;
    m_free_blocks.clear();
    m_free_blocks.push_back({0, capacity});
}

std::optional<AllocationResult> FreeListAllocator::Allocate(VkDeviceSize size, VkDeviceSize alignment)
{
    if (size == 0 || alignment == 0)
        return std::nullopt;

    for (size_t i = 0; i < m_free_blocks.size(); ++i) {
        FreeBlock& block = m_free_blocks[i];

        // Align the offset within this block
        VkDeviceSize aligned_offset = (block.offset + alignment - 1) & ~(alignment - 1);
        VkDeviceSize padding = aligned_offset - block.offset;
        VkDeviceSize total_needed = padding + size;

        if (total_needed > block.size)
            continue;

        AllocationResult result;
        result.offset = aligned_offset;
        result.size = size;

        // Carve out the allocation
        if (padding == 0 && total_needed == block.size) {
            // Exact fit — remove entire block
            m_free_blocks.erase(m_free_blocks.begin() + static_cast<ptrdiff_t>(i));
        } else if (padding == 0) {
            // Allocation at start of block — shrink from front
            block.offset += size;
            block.size -= size;
        } else if (padding + size == block.size) {
            // Allocation at end of block — shrink from back
            block.size = padding;
        } else {
            // Split: keep front padding as free, insert remainder after allocation
            VkDeviceSize remainder_offset = aligned_offset + size;
            VkDeviceSize remainder_size = block.size - total_needed;
            block.size = padding;
            m_free_blocks.insert(m_free_blocks.begin() + static_cast<ptrdiff_t>(i) + 1,
                                 {remainder_offset, remainder_size});
        }

#ifndef NDEBUG
        ValidateFreeList();
#endif
        return result;
    }

    return std::nullopt;
}

void FreeListAllocator::Free(VkDeviceSize offset, VkDeviceSize size)
{
    if (size == 0)
        return;

    // Find insertion point to maintain sorted order by offset
    auto it = std::lower_bound(m_free_blocks.begin(), m_free_blocks.end(), offset,
        [](const FreeBlock& block, VkDeviceSize off) { return block.offset < off; });

    auto idx = it - m_free_blocks.begin();
    m_free_blocks.insert(it, {offset, size});

    // Coalesce with next block
    if (idx + 1 < static_cast<ptrdiff_t>(m_free_blocks.size())) {
        FreeBlock& current = m_free_blocks[static_cast<size_t>(idx)];
        FreeBlock& next = m_free_blocks[static_cast<size_t>(idx) + 1];
        if (current.offset + current.size == next.offset) {
            current.size += next.size;
            m_free_blocks.erase(m_free_blocks.begin() + idx + 1);
        }
    }

    // Coalesce with previous block
    if (idx > 0) {
        FreeBlock& prev = m_free_blocks[static_cast<size_t>(idx) - 1];
        FreeBlock& current = m_free_blocks[static_cast<size_t>(idx)];
        if (prev.offset + prev.size == current.offset) {
            prev.size += current.size;
            m_free_blocks.erase(m_free_blocks.begin() + idx);
        }
    }

#ifndef NDEBUG
    ValidateFreeList();
#endif
}

VkDeviceSize FreeListAllocator::GetFreeSpace() const
{
    VkDeviceSize total = 0;
    for (const auto& block : m_free_blocks)
        total += block.size;
    return total;
}

#ifndef NDEBUG
void FreeListAllocator::ValidateFreeList() const
{
    for (size_t i = 0; i < m_free_blocks.size(); ++i) {
        const auto& block = m_free_blocks[i];
        // Block must be within capacity
        assert(block.offset + block.size <= m_capacity);
        // Block must have nonzero size
        assert(block.size > 0);
        // Blocks must be sorted and non-overlapping
        if (i > 0) {
            const auto& prev = m_free_blocks[i - 1];
            assert(prev.offset + prev.size <= block.offset);
        }
    }
}
#endif

// ---------------------------------------------------------------------------
// BufferPool
// ---------------------------------------------------------------------------

void BufferPool::Initialize(VkDevice device, const VkPhysicalDeviceMemoryProperties& mem_props,
                            VkDeviceSize chunk_size)
{
    m_device = device;
    m_mem_props = mem_props;
    m_chunk_size = chunk_size;
}

bool BufferPool::CreateChunk()
{
    Chunk chunk;

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = m_chunk_size;
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &buffer_info, nullptr, &chunk.buffer) != VK_SUCCESS) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "BufferPool: Failed to create chunk buffer (%llu bytes)",
                          static_cast<unsigned long long>(m_chunk_size));
        return false;
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(m_device, chunk.buffer, &mem_reqs);

    uint32_t mem_type = FindVulkanMemoryType(m_mem_props, mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mem_type == UINT32_MAX) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "BufferPool: No suitable HOST_VISIBLE memory type");
        vkDestroyBuffer(m_device, chunk.buffer, nullptr);
        return false;
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = mem_type;

    if (vkAllocateMemory(m_device, &alloc_info, nullptr, &chunk.memory) != VK_SUCCESS) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "BufferPool: Failed to allocate chunk memory (%llu bytes)",
                          static_cast<unsigned long long>(mem_reqs.size));
        vkDestroyBuffer(m_device, chunk.buffer, nullptr);
        return false;
    }

    vkBindBufferMemory(m_device, chunk.buffer, chunk.memory, 0);

    // Persistently map
    if (vkMapMemory(m_device, chunk.memory, 0, m_chunk_size, 0, &chunk.mapped) != VK_SUCCESS) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "BufferPool: Failed to map chunk memory");
        vkFreeMemory(m_device, chunk.memory, nullptr);
        vkDestroyBuffer(m_device, chunk.buffer, nullptr);
        return false;
    }

    chunk.allocator.Reset(m_chunk_size);
    m_chunks.push_back(chunk);

    Con_DPrintf("BufferPool: Created chunk %u (%llu bytes)\n",
                static_cast<unsigned>(m_chunks.size() - 1),
                static_cast<unsigned long long>(m_chunk_size));
    return true;
}

bool BufferPool::Allocate(VkDeviceSize size, VkDeviceSize alignment, BufferAllocation& out)
{
    // Try existing chunks
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_chunks.size()); ++i) {
        auto result = m_chunks[i].allocator.Allocate(size, alignment);
        if (result.has_value()) {
            out.buffer = m_chunks[i].buffer;
            out.offset = result->offset;
            out.size = result->size;
            out.mapped_ptr = static_cast<char*>(m_chunks[i].mapped) + result->offset;
            out.chunk_index = i;
            ++m_active_allocations;
            return true;
        }
    }

    // No existing chunk has space — grow
    if (!CreateChunk()) {
        Rml::Log::Message(Rml::Log::LT_ERROR,
            "BufferPool: Allocation failed (requested %llu bytes, %u chunks active, %u allocations)",
            static_cast<unsigned long long>(size),
            static_cast<unsigned>(m_chunks.size()),
            m_active_allocations);
        return false;
    }

    uint32_t new_idx = static_cast<uint32_t>(m_chunks.size()) - 1;
    auto result = m_chunks[new_idx].allocator.Allocate(size, alignment);
    if (!result.has_value()) {
        Rml::Log::Message(Rml::Log::LT_ERROR,
            "BufferPool: Allocation failed in fresh chunk (requested %llu bytes, chunk size %llu)",
            static_cast<unsigned long long>(size),
            static_cast<unsigned long long>(m_chunk_size));
        return false;
    }

    out.buffer = m_chunks[new_idx].buffer;
    out.offset = result->offset;
    out.size = result->size;
    out.mapped_ptr = static_cast<char*>(m_chunks[new_idx].mapped) + result->offset;
    out.chunk_index = new_idx;
    ++m_active_allocations;
    return true;
}

void BufferPool::Free(const BufferAllocation& alloc)
{
    if (alloc.chunk_index >= static_cast<uint32_t>(m_chunks.size()))
        return;

    m_chunks[alloc.chunk_index].allocator.Free(alloc.offset, alloc.size);
    --m_active_allocations;
}

void BufferPool::Shutdown()
{
    if (m_active_allocations > 0) {
        Rml::Log::Message(Rml::Log::LT_WARNING,
            "BufferPool: Shutdown with %u active allocations", m_active_allocations);
    }

    for (auto& chunk : m_chunks) {
        if (chunk.mapped) {
            vkUnmapMemory(m_device, chunk.memory);
        }
        if (chunk.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, chunk.buffer, nullptr);
        }
        if (chunk.memory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, chunk.memory, nullptr);
        }
    }
    m_chunks.clear();
    m_active_allocations = 0;
}

// ---------------------------------------------------------------------------
// ImageMemoryPool
// ---------------------------------------------------------------------------

void ImageMemoryPool::Initialize(VkDevice device, const VkPhysicalDeviceMemoryProperties& mem_props,
                                 VkDeviceSize buffer_image_granularity, VkDeviceSize page_size)
{
    m_device = device;
    m_mem_props = mem_props;
    m_buffer_image_granularity = buffer_image_granularity;
    m_page_size = page_size;
}

bool ImageMemoryPool::CreatePage(uint32_t memory_type_index)
{
    Page page;
    page.memory_type_index = memory_type_index;

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = m_page_size;
    alloc_info.memoryTypeIndex = memory_type_index;

    if (vkAllocateMemory(m_device, &alloc_info, nullptr, &page.memory) != VK_SUCCESS) {
        Rml::Log::Message(Rml::Log::LT_ERROR,
            "ImageMemoryPool: Failed to allocate page (%llu bytes, mem type %u)",
            static_cast<unsigned long long>(m_page_size), memory_type_index);
        return false;
    }

    page.allocator.Reset(m_page_size);
    m_pages.push_back(page);

    Con_DPrintf("ImageMemoryPool: Created page %u (%llu bytes, mem type %u)\n",
                static_cast<unsigned>(m_pages.size() - 1),
                static_cast<unsigned long long>(m_page_size),
                memory_type_index);
    return true;
}

bool ImageMemoryPool::Allocate(const VkMemoryRequirements& mem_reqs,
                               VkMemoryPropertyFlags properties,
                               ImageMemoryAllocation& out)
{
    uint32_t mem_type = FindVulkanMemoryType(m_mem_props, mem_reqs.memoryTypeBits, properties);
    if (mem_type == UINT32_MAX) {
        Rml::Log::Message(Rml::Log::LT_ERROR,
            "ImageMemoryPool: No suitable memory type (filter=0x%x, props=0x%x)",
            mem_reqs.memoryTypeBits, static_cast<unsigned>(properties));
        return false;
    }

    VkDeviceSize alignment = std::max(mem_reqs.alignment, m_buffer_image_granularity);

    // Oversized images get dedicated allocations
    if (mem_reqs.size > m_page_size) {
        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex = mem_type;

        VkDeviceMemory memory;
        if (vkAllocateMemory(m_device, &alloc_info, nullptr, &memory) != VK_SUCCESS) {
            Rml::Log::Message(Rml::Log::LT_ERROR,
                "ImageMemoryPool: Failed dedicated allocation (%llu bytes)",
                static_cast<unsigned long long>(mem_reqs.size));
            return false;
        }

        out.memory = memory;
        out.offset = 0;
        out.size = mem_reqs.size;
        out.page_index = UINT32_MAX;
        out.dedicated = true;
        ++m_active_allocations;

        Con_DPrintf("ImageMemoryPool: Dedicated allocation (%llu bytes)\n",
                    static_cast<unsigned long long>(mem_reqs.size));
        return true;
    }

    // Try existing pages with matching memory type
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_pages.size()); ++i) {
        if (m_pages[i].memory_type_index != mem_type)
            continue;

        auto result = m_pages[i].allocator.Allocate(mem_reqs.size, alignment);
        if (result.has_value()) {
            out.memory = m_pages[i].memory;
            out.offset = result->offset;
            out.size = result->size;
            out.page_index = i;
            out.dedicated = false;
            ++m_active_allocations;
            return true;
        }
    }

    // No existing page has space — create new one
    if (!CreatePage(mem_type)) {
        Rml::Log::Message(Rml::Log::LT_ERROR,
            "ImageMemoryPool: Allocation failed (requested %llu bytes, %u pages active, %u allocations)",
            static_cast<unsigned long long>(mem_reqs.size),
            static_cast<unsigned>(m_pages.size()),
            m_active_allocations);
        return false;
    }

    uint32_t new_idx = static_cast<uint32_t>(m_pages.size()) - 1;
    auto result = m_pages[new_idx].allocator.Allocate(mem_reqs.size, alignment);
    if (!result.has_value()) {
        Rml::Log::Message(Rml::Log::LT_ERROR,
            "ImageMemoryPool: Allocation failed in fresh page (requested %llu bytes, page size %llu)",
            static_cast<unsigned long long>(mem_reqs.size),
            static_cast<unsigned long long>(m_page_size));
        return false;
    }

    out.memory = m_pages[new_idx].memory;
    out.offset = result->offset;
    out.size = result->size;
    out.page_index = new_idx;
    out.dedicated = false;
    ++m_active_allocations;
    return true;
}

void ImageMemoryPool::Free(const ImageMemoryAllocation& alloc)
{
    if (alloc.dedicated) {
        if (alloc.memory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, alloc.memory, nullptr);
        }
        --m_active_allocations;
        return;
    }

    if (alloc.page_index >= static_cast<uint32_t>(m_pages.size()))
        return;

    m_pages[alloc.page_index].allocator.Free(alloc.offset, alloc.size);
    --m_active_allocations;
}

void ImageMemoryPool::Shutdown()
{
    if (m_active_allocations > 0) {
        Rml::Log::Message(Rml::Log::LT_WARNING,
            "ImageMemoryPool: Shutdown with %u active allocations", m_active_allocations);
    }

    for (auto& page : m_pages) {
        if (page.memory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, page.memory, nullptr);
        }
    }
    m_pages.clear();
    m_active_allocations = 0;
}

} // namespace QRmlUI
