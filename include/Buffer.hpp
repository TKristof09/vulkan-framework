#pragma once
#include "VulkanContext.hpp"
#include "CommandBuffer.hpp"
#include <vk_mem_alloc.h>


class Buffer
{
public:
    enum class Type
    {
        VERTEX,
        INDEX,
        UNIFORM,

        TRANSFER
    };
    Buffer();
    Buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool mappable = false);
    ~Buffer();
    Buffer(const Buffer& other) = delete;

    Buffer(Buffer&& other) noexcept
        : m_type(other.m_type),
          m_buffer(other.m_buffer),
          m_size(other.m_size),
          m_nonCoherentAtomeSize(other.m_nonCoherentAtomeSize),
          m_allocation(other.m_allocation)
    {
        other.m_buffer = VK_NULL_HANDLE;
    }

    Buffer& operator=(const Buffer& other) = delete;

    Buffer& operator=(Buffer&& other) noexcept
    {
        if(this == &other)
            return *this;

        if(m_buffer != VK_NULL_HANDLE)
            Free();

        m_type                 = other.m_type;
        m_buffer               = other.m_buffer;
        m_size                 = other.m_size;
        m_nonCoherentAtomeSize = other.m_nonCoherentAtomeSize;
        m_allocation           = other.m_allocation;

        other.m_buffer = VK_NULL_HANDLE;
        return *this;
    }

    void Allocate(VkDeviceSize size, VkBufferUsageFlags usage, bool mappable = false);
    void Free();
    void Copy(Buffer* dst, VkDeviceSize size);
    void CopyToImage(VkImage image, uint32_t width, uint32_t height);
    void Fill(const void* data, uint64_t size, uint64_t offset = 0);

    // offsets must be sorted in ascending order
    void Fill(const std::vector<const void*>& datas, const std::vector<uint64_t>& sizes, const std::vector<uint64_t>& offsets);
    void ZeroFill();
    void Bind(const CommandBuffer& commandBuffer);
    [[nodiscard]] const VkBuffer& GetVkBuffer() const { return m_buffer; }
    [[nodiscard]] VkDeviceSize GetSize() const { return m_size; }
    [[nodiscard]] uint64_t GetDeviceAddress() const
    {
        VkBufferDeviceAddressInfo info = {};
        info.sType                     = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.buffer                    = m_buffer;

        return vkGetBufferDeviceAddress(VulkanContext::GetDevice(), &info);
    }

protected:
    Type m_type;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    uint64_t m_size;

    uint64_t m_nonCoherentAtomeSize;

    VmaAllocation m_allocation;
    void* m_mappedMemory = nullptr;
};
