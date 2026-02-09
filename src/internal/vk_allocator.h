/*
 * vkQuake RmlUI - Vulkan Suballocator
 *
 * Pool-based memory allocation for RmlUI geometry buffers and texture images.
 * Reduces Vulkan allocation count from O(objects) to O(pool_chunks).
 */

#ifndef QRMLUI_VK_ALLOCATOR_H
#define QRMLUI_VK_ALLOCATOR_H

#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <cstdint>

namespace QRmlUI {

// Free function extracted from RenderInterface_VK::FindMemoryType
uint32_t FindVulkanMemoryType(const VkPhysicalDeviceMemoryProperties& mem_props,
                              uint32_t type_filter, VkMemoryPropertyFlags properties);

// ---------------------------------------------------------------------------
// FreeListAllocator - pure data structure, no Vulkan types
// ---------------------------------------------------------------------------

struct FreeBlock {
    VkDeviceSize offset;
    VkDeviceSize size;
};

struct AllocationResult {
    VkDeviceSize offset;
    VkDeviceSize size; // actual size including alignment padding
};

class FreeListAllocator {
public:
    FreeListAllocator() = default;

    void Reset(VkDeviceSize capacity);

    // First-fit allocation with alignment. Returns nullopt on failure.
    std::optional<AllocationResult> Allocate(VkDeviceSize size, VkDeviceSize alignment);

    // Return a region to the free list, coalescing adjacent blocks.
    void Free(VkDeviceSize offset, VkDeviceSize size);

    VkDeviceSize GetCapacity() const { return m_capacity; }
    VkDeviceSize GetFreeSpace() const;

#ifndef NDEBUG
    void ValidateFreeList() const;
#endif

private:
    std::vector<FreeBlock> m_free_blocks;
    VkDeviceSize m_capacity = 0;
};

// ---------------------------------------------------------------------------
// BufferPool - suballocates geometry buffers from large HOST_VISIBLE chunks
// ---------------------------------------------------------------------------

struct BufferAllocation {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    void* mapped_ptr = nullptr;
    uint32_t chunk_index = UINT32_MAX;
};

class BufferPool {
public:
    static constexpr VkDeviceSize DEFAULT_CHUNK_SIZE = 2 * 1024 * 1024; // 2MB

    BufferPool() = default;

    void Initialize(VkDevice device, const VkPhysicalDeviceMemoryProperties& mem_props,
                    VkDeviceSize chunk_size = DEFAULT_CHUNK_SIZE);
    void Shutdown();

    bool Allocate(VkDeviceSize size, VkDeviceSize alignment, BufferAllocation& out);
    void Free(const BufferAllocation& alloc);

    uint32_t GetChunkCount() const { return static_cast<uint32_t>(m_chunks.size()); }
    uint32_t GetActiveAllocations() const { return m_active_allocations; }

private:
    struct Chunk {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        FreeListAllocator allocator;
    };

    bool CreateChunk();

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties m_mem_props{};
    VkDeviceSize m_chunk_size = DEFAULT_CHUNK_SIZE;
    std::vector<Chunk> m_chunks;
    uint32_t m_active_allocations = 0;
};

// ---------------------------------------------------------------------------
// ImageMemoryPool - suballocates device memory for texture images
// ---------------------------------------------------------------------------

struct ImageMemoryAllocation {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    uint32_t page_index = UINT32_MAX;
    bool dedicated = false; // oversized images get their own allocation
};

class ImageMemoryPool {
public:
    static constexpr VkDeviceSize DEFAULT_PAGE_SIZE = 16 * 1024 * 1024; // 16MB

    ImageMemoryPool() = default;

    void Initialize(VkDevice device, const VkPhysicalDeviceMemoryProperties& mem_props,
                    VkDeviceSize buffer_image_granularity,
                    VkDeviceSize page_size = DEFAULT_PAGE_SIZE);
    void Shutdown();

    bool Allocate(const VkMemoryRequirements& mem_reqs, VkMemoryPropertyFlags properties,
                  ImageMemoryAllocation& out);
    void Free(const ImageMemoryAllocation& alloc);

    uint32_t GetPageCount() const { return static_cast<uint32_t>(m_pages.size()); }
    uint32_t GetActiveAllocations() const { return m_active_allocations; }

private:
    struct Page {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        uint32_t memory_type_index = UINT32_MAX;
        FreeListAllocator allocator;
    };

    bool CreatePage(uint32_t memory_type_index);

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties m_mem_props{};
    VkDeviceSize m_page_size = DEFAULT_PAGE_SIZE;
    VkDeviceSize m_buffer_image_granularity = 1;
    std::vector<Page> m_pages;
    uint32_t m_active_allocations = 0;
};

} // namespace QRmlUI

#endif // QRMLUI_VK_ALLOCATOR_H
