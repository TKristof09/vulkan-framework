#pragma once

#include "VulkanContext.hpp"


struct SamplerConfig
{
    VkFilter minFilter = VK_FILTER_LINEAR;
    VkFilter magFilter = VK_FILTER_LINEAR;

    VkSamplerMipmapMode mipFilter = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    VkCompareOp depthCompareOp = VK_COMPARE_OP_NEVER;

    uint32_t anisotropy = 16;

    VkBorderColor borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    bool operator==(const SamplerConfig& other) const
    {
        return minFilter == other.minFilter && magFilter == other.magFilter && mipFilter == other.mipFilter && addressMode == other.addressMode && depthCompareOp == other.depthCompareOp && anisotropy == other.anisotropy && borderColor == other.borderColor;
    }
};


class Sampler
{
public:
    Sampler(SamplerConfig config = {})
    {
        VkSamplerCreateInfo createInfo     = {};
        createInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        createInfo.magFilter               = config.minFilter;
        createInfo.minFilter               = config.magFilter;
        createInfo.addressModeU            = config.addressMode;
        createInfo.addressModeV            = config.addressMode;
        createInfo.addressModeW            = config.addressMode;
        createInfo.anisotropyEnable        = config.anisotropy != 0;
        createInfo.maxAnisotropy           = static_cast<float>(config.anisotropy);
        createInfo.borderColor             = config.borderColor;
        createInfo.unnormalizedCoordinates = VK_FALSE;
        createInfo.compareEnable           = config.depthCompareOp != VK_COMPARE_OP_NEVER;
        createInfo.compareOp               = config.depthCompareOp;
        createInfo.mipmapMode              = config.mipFilter;
        createInfo.mipLodBias              = 0.0f;
        createInfo.minLod                  = 0.0f;
        createInfo.maxLod                  = VK_LOD_CLAMP_NONE;

        VK_CHECK(vkCreateSampler(VulkanContext::GetDevice(), &createInfo, nullptr, &m_sampler), "Failed to create sampler");
    }
    ~Sampler()
    {
        vkDestroySampler(VulkanContext::GetDevice(), m_sampler, nullptr);
    }

    Sampler(const Sampler&)            = delete;
    Sampler& operator=(const Sampler&) = delete;

    Sampler(Sampler&& other) noexcept
        : m_sampler(other.m_sampler)
    {
        other.m_sampler = VK_NULL_HANDLE;
    }

    Sampler& operator=(Sampler&& other) noexcept
    {
        if(this != &other)
        {
            if(m_sampler != VK_NULL_HANDLE)
            {
                vkDestroySampler(VulkanContext::GetDevice(), m_sampler, nullptr);
            }
            m_sampler       = other.m_sampler;
            other.m_sampler = VK_NULL_HANDLE;
        }
        return *this;
    }

    [[nodiscard]] VkSampler GetVkSampler() const { return m_sampler; }

private:
    VkSampler m_sampler = VK_NULL_HANDLE;
};


template<class T>
inline void hashCombine(std::size_t& s, const T& v)
{
    std::hash<T> h;
    s ^= h(v) + 0x9e3779b9 + (s << 6) + (s >> 2);
}
namespace std
{
template<>
struct hash<SamplerConfig>
{
    std::size_t operator()(const SamplerConfig& c) const
    {
        std::size_t result = 0;
        hashCombine(result, c.minFilter);
        hashCombine(result, c.mipFilter);
        hashCombine(result, c.addressMode);
        hashCombine(result, c.depthCompareOp);
        hashCombine(result, c.anisotropy);
        hashCombine(result, c.borderColor);

        return result;
    }
};
}
