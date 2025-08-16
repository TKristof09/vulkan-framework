#include "Buffer.hpp"
#include <cstring>
#include "Log.hpp"

Buffer::Buffer() : m_size(0) {}

Buffer::Buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool mappable)
{
    Allocate(size, usage, mappable);
}

Buffer::~Buffer()
{
    Free();
}

void Buffer::Free()
{
    if(m_buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(VulkanContext::GetVmaAllocator(), m_buffer, m_allocation);
        m_buffer = VK_NULL_HANDLE;
    }
}
uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for(uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if(typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    throw std::runtime_error("Failed to find suitable memory type");
}
void Buffer::Allocate(VkDeviceSize size, VkBufferUsageFlags usage, bool mappable)
{
    m_size = size;

    m_type = Buffer::Type::TRANSFER;
    if(usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
        m_type = Buffer::Type::VERTEX;
    if(usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
        m_type = Buffer::Type::INDEX;
    if(usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        m_type = Buffer::Type::UNIFORM;

    VkBufferCreateInfo createInfo = {};
    createInfo.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createInfo.size               = size;
    createInfo.usage              = usage;

    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;


    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage                   = VMA_MEMORY_USAGE_AUTO;
    if(mappable)
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;  // TODO: how to chose between sequential and random access
    VmaAllocationInfo allocInfo;
    VK_CHECK(vmaCreateBuffer(VulkanContext::GetVmaAllocator(), &createInfo, &allocCreateInfo, &m_buffer, &m_allocation, &allocInfo), "Failed to create buffer");
    m_mappedMemory = allocInfo.pMappedData;
    VkMemoryPropertyFlags memPropFlags;
    vmaGetAllocationMemoryProperties(VulkanContext::GetVmaAllocator(), m_allocation, &memPropFlags);
}

void Buffer::Copy(Buffer* dst, VkDeviceSize size)
{
    CommandBuffer commandBuffer;
    commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset    = 0;
    copyRegion.dstOffset    = 0;
    copyRegion.size         = size;
    vkCmdCopyBuffer(commandBuffer.GetCommandBuffer(), m_buffer, dst->GetVkBuffer(), 1, &copyRegion);

    commandBuffer.SubmitIdle();  // TODO better to submit with a fence instead of waiting until idle
}

void Buffer::CopyToImage(VkImage image, uint32_t width, uint32_t height)
{
    CommandBuffer commandBuffer;
    commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VkBufferImageCopy region = {};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer.GetCommandBuffer(), m_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);


    commandBuffer.SubmitIdle();
}

void Buffer::Fill(const void* data, uint64_t size, uint64_t offset)
{
    if(m_mappedMemory)
    {
        std::memcpy((void*)((uint8_t*)m_mappedMemory + offset), data, (size_t)size);
        return;
    }

    void* memory = nullptr;
    VK_CHECK(vmaMapMemory(VulkanContext::GetVmaAllocator(), m_allocation, &memory), "Failed to map memory");

    std::memcpy((void*)((uint8_t*)memory + offset), data, (size_t)size);

    /* On desktop hardware host visible memory is also always host coherent
    if(!m_isHostCoherent)
        VK_CHECK(vmaFlushAllocation(VulkanContext::GetVmaAllocator(), m_allocation, offset, size), "Failed to flush memory");
*/
    vmaUnmapMemory(VulkanContext::GetVmaAllocator(), m_allocation);
}

void Buffer::Fill(const std::vector<const void*>& datas, const std::vector<uint64_t>& sizes, const std::vector<uint64_t>& offsets)
{
    if(m_mappedMemory)
    {
        uint32_t i = 0;
        for(auto offset : offsets)
        {
            std::memcpy((void*)((uintptr_t)m_mappedMemory + offset), datas[i], (size_t)sizes[i]);

            i++;
        }
        return;
    }


    void* memory = nullptr;
    VK_CHECK(vmaMapMemory(VulkanContext::GetVmaAllocator(), m_allocation, &memory), "Failed to map memory");

    uint32_t i = 0;
    for(auto offset : offsets)
    {
        std::memcpy((void*)((uintptr_t)memory + offset), datas[i], (size_t)sizes[i]);

        i++;
    }

    /* On desktop hardware host visible memory is also always host coherent
    if(!m_isHostCoherent)
    {
        std::vector<VkMappedMemoryRange> ranges(offsets.size());
        uint32_t i = 0;
        for(auto offset : offsets)
        {
            diff                 = offset % nonCoherentAtomSize;
            correctOffset        = offset - diff;
            uint64_t correctSize = (size + diff) + nonCoherentAtomSize - ((size + diff) % nonCoherentAtomSize);


            ranges[i].sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            ranges[i].memory = m_memory;
            ranges[i].size   = correctSize;
            ranges[i].offset = correctOffset;

            i++;
        }

        vkFlushMappedMemoryRanges(VulkanContext::GetDevice(), ranges.size(), ranges.data());
    }
    */
    vmaUnmapMemory(VulkanContext::GetVmaAllocator(), m_allocation);
}

void Buffer::ZeroFill()
{
    if(m_mappedMemory)
    {
        std::memset(m_mappedMemory, 0, (size_t)m_size);
        return;
    }

    void* memory = nullptr;
    VK_CHECK(vmaMapMemory(VulkanContext::GetVmaAllocator(), m_allocation, &memory), "Failed to map memory");

    std::memset(memory, 0, (size_t)m_size);

    /* On desktop hardware host visible memory is also always host coherent
    if(!m_isHostCoherent)
        VK_CHECK(vmaFlushAllocation(VulkanContext::GetVmaAllocator(), m_allocation, offset, size), "Failed to flush memory");
*/
    vmaUnmapMemory(VulkanContext::GetVmaAllocator(), m_allocation);
}
void Buffer::Bind(const CommandBuffer& commandBuffer)
{
    switch(m_type)
    {
    case Type::VERTEX:
        {
            VkBuffer buffers[]     = {m_buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer.GetCommandBuffer(), 0, 1, buffers, offsets);
            break;
        }
    case Type::INDEX:
        {
            vkCmdBindIndexBuffer(commandBuffer.GetCommandBuffer(), m_buffer, 0, VK_INDEX_TYPE_UINT32);
            break;
        }
    default:
        Log::Error("Bufffer type not handled");
        break;
    }
}
