#include "Image.hpp"
#include "Buffer.hpp"
#include "CommandBuffer.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <array>
#include "Log.hpp"
#include <stb_image.h>
#include <vulkan/utility/vk_format_utils.h>

bool hasStencilComponent(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}


Image::Image(uint32_t width, uint32_t height, ImageCreateInfo createInfo) : m_width(width),
                                                                            m_height(height),
                                                                            m_image(createInfo.image),
                                                                            m_format(createInfo.format),
                                                                            m_layout(VK_IMAGE_LAYOUT_UNDEFINED),
                                                                            m_aspect(createInfo.aspectFlags),
                                                                            m_onlyHandleImageView(createInfo.image != VK_NULL_HANDLE)
{
    if(width == 0 && height == 0)
        return;

    if(createInfo.isCubeMap)
        createInfo.layerCount = 6;

    m_layerCount = createInfo.layerCount;
    m_isCubeMap  = createInfo.isCubeMap;

    if(createInfo.image == VK_NULL_HANDLE)
    {
        if(createInfo.msaaSamples == VK_SAMPLE_COUNT_1_BIT && createInfo.useMips)
            m_mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(m_width, m_height)))) + 1;
        else
            m_mipLevels = 1;

        VkImageCreateInfo ci = {};
        ci.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType         = height > 1 ? VK_IMAGE_TYPE_2D : VK_IMAGE_TYPE_1D;
        ci.extent.width      = width;
        ci.extent.height     = height;
        ci.extent.depth      = 1;
        ci.mipLevels         = m_mipLevels;
        ci.arrayLayers       = createInfo.layerCount;
        ci.format            = createInfo.format;
        ci.tiling            = createInfo.tiling;
        ci.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
        m_usage              = createInfo.useMips ? createInfo.usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT : createInfo.usage;
        ci.usage             = m_usage;
        ci.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
        ci.samples           = createInfo.msaaSamples;
        ci.flags             = createInfo.isCubeMap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;


        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO;
        vmaCreateImage(VulkanContext::GetVmaAllocator(), &ci, &allocInfo, &m_image, &m_allocation, nullptr);
    }


    VkImageViewCreateInfo viewCreateInfo
        = {};
    viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCreateInfo.image = m_image;
    if(createInfo.isCubeMap)
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    else
    {
        if(height > 1)
            viewCreateInfo.viewType = createInfo.layerCount > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        else
            viewCreateInfo.viewType = createInfo.layerCount > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
    }
    viewCreateInfo.format       = m_format;
    // stick to default color mapping(probably could leave this as default)
    viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    viewCreateInfo.subresourceRange.aspectMask     = createInfo.aspectFlags;
    viewCreateInfo.subresourceRange.baseMipLevel   = 0;
    viewCreateInfo.subresourceRange.levelCount     = VK_REMAINING_MIP_LEVELS;
    viewCreateInfo.subresourceRange.baseArrayLayer = 0;
    viewCreateInfo.subresourceRange.layerCount     = createInfo.layerCount;

    m_imageViews.push_back({});
    VK_CHECK(vkCreateImageView(VulkanContext::GetDevice(), &viewCreateInfo, nullptr, &m_imageViews[0]), "Failed to create image views!");

    if(createInfo.layout != VK_IMAGE_LAYOUT_UNDEFINED)
        TransitionLayout(createInfo.layout);

#ifdef VDEBUG
    if(!createInfo.debugName.empty())
    {
        m_debugName = createInfo.debugName;
        VK_SET_DEBUG_NAME(m_image, VK_OBJECT_TYPE_IMAGE, createInfo.debugName.c_str());

        createInfo.debugName += " image view 0";
        VK_SET_DEBUG_NAME(m_imageViews[0], VK_OBJECT_TYPE_IMAGE_VIEW, createInfo.debugName.c_str());
    }
#endif
}

Image::Image(VkExtent2D extent, ImageCreateInfo createInfo) : Image(extent.width, extent.height, createInfo)
{
}

Image::Image(std::pair<uint32_t, uint32_t> widthHeight, ImageCreateInfo createInfo) : Image(widthHeight.first, widthHeight.second, createInfo)
{
}
Image Image::FromFile(std::filesystem::path path, VkFormat format)
{
    int width, height, channels;

    void* pixels = nullptr;

    if(vkuFormatIsSFLOAT(format) || vkuFormatIsUFLOAT(format))
    {
        pixels = stbi_loadf(std::filesystem::absolute(path).string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    }
    else
    {
        pixels = stbi_load(std::filesystem::absolute(path).string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    }

    if(channels != 4)
        Log::Warn("Texture {} has {} channels, but is loaded with 4 channels", path.filename().string(), channels);
    if(!pixels)
        throw std::runtime_error("Failed to load texture image!");


    ImageCreateInfo imageCI = {};
    imageCI.format          = format;
    imageCI.usage           = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageCI.aspectFlags     = VK_IMAGE_ASPECT_COLOR_BIT;
    imageCI.debugName       = path.filename().string();


    Image texture(width, height, imageCI);

    Buffer stagingBuffer(texture.GetMemorySize(),
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         true);

    stagingBuffer.Fill(pixels, texture.GetMemorySize());
    stbi_image_free(pixels);

    texture.TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    stagingBuffer.CopyToImage(texture, width, height, texture.GetBytesPerPixel());
    texture.GenerateMipmaps(VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

    return texture;
}


Image Image::CubemapFromFile(std::filesystem::path dirPath)
{
    const std::array<std::string, 6> faceNames = {
        // "top", "bottom", "front", "back", "left", "right"};
        "right", "left", "top", "bottom", "front", "back"};

    if(!std::filesystem::exists(dirPath) || !std::filesystem::is_directory(dirPath))
        throw std::runtime_error("CubemapFromFile: path is not a directory: " + dirPath.string());

    std::unordered_map<std::string, std::filesystem::path> found;
    for(const auto& e : std::filesystem::directory_iterator(dirPath))
    {
        if(!e.is_regular_file())
            continue;
        auto stem = e.path().stem().string();
        std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
        found.emplace(stem, e.path());
    }

    // ensure all required faces exist
    for(auto& name : faceNames)
    {
        if(found.find(name) == found.end())
            throw std::runtime_error("CubemapFromFile: missing face file: " + name);
    }

    int width = 0, height = 0, channels = 0;
    std::vector<unsigned char> combined;

    for(const auto& name : faceNames)
    {
        auto p = found.at(name);

        int w, h, c;
        stbi_uc* pixels = stbi_load(std::filesystem::absolute(p).string().c_str(), &w, &h, &c, STBI_rgb_alpha);
        if(!pixels)
        {
            throw std::runtime_error("CubemapFromFile: failed to load image: " + p.string());
        }

        if(channels == 0)
            channels = c;

        if(width == 0 && height == 0)
        {
            width  = w;
            height = h;
            combined.reserve(static_cast<size_t>(width) * height * 4 * 6);
        }
        else if(width != w || height != h)
        {
            stbi_image_free(pixels);
            throw std::runtime_error("CubemapFromFile: face sizes differ: " + p.string());
        }

        if(c != 4)
            Log::Warn("Texture {} has {} channels, but is loaded with 4 channels", p.filename().string(), c);

        size_t faceBytes = static_cast<size_t>(width) * height * 4;
        combined.insert(combined.end(), pixels, pixels + faceBytes);

        stbi_image_free(pixels);
    }

    if(width == 0 || height == 0)
        throw std::runtime_error("CubemapFromFile: no images loaded.");

    ImageCreateInfo imageCI = {};
    imageCI.format          = VK_FORMAT_R8G8B8A8_SRGB;
    imageCI.usage           = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageCI.aspectFlags     = VK_IMAGE_ASPECT_COLOR_BIT;
    imageCI.debugName       = dirPath.filename().string();
    imageCI.isCubeMap       = true;

    Image cubemap(width, height, imageCI);

    VkDeviceSize totalSize = cubemap.GetMemorySize();

    Buffer stagingBuffer(totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    stagingBuffer.Fill(combined.data(), totalSize);

    cubemap.TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    stagingBuffer.CopyToImage(cubemap, width, height, cubemap.GetBytesPerPixel(), 6);

    cubemap.GenerateMipmaps(VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

    return cubemap;
}

Image::~Image()
{
    Free();
}

void Image::Free()
{
    if(m_image == VK_NULL_HANDLE || m_imageViews[0] == VK_NULL_HANDLE)
        return;

    for(auto& imageView : m_imageViews)
    {
        vkDestroyImageView(VulkanContext::GetDevice(), imageView, nullptr);
        imageView = VK_NULL_HANDLE;
    }

    if(!m_onlyHandleImageView)
    {
        vmaDestroyImage(VulkanContext::GetVmaAllocator(), m_image, m_allocation);
        m_image = VK_NULL_HANDLE;
    }
}

VkImageView Image::CreateImageView(uint32_t mip)
{
    VkImageViewCreateInfo viewCreateInfo = {};
    viewCreateInfo.sType                 = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCreateInfo.image                 = m_image;
    if(m_isCubeMap)
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    else
        viewCreateInfo.viewType = m_layerCount > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    viewCreateInfo.format       = m_format;
    // stick to default color mapping(probably could leave this as default)
    viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    viewCreateInfo.subresourceRange.aspectMask     = m_aspect;
    viewCreateInfo.subresourceRange.baseMipLevel   = mip;
    viewCreateInfo.subresourceRange.levelCount     = 1;
    viewCreateInfo.subresourceRange.baseArrayLayer = 0;
    viewCreateInfo.subresourceRange.layerCount     = m_layerCount;

    m_imageViews.push_back({});
    VK_CHECK(vkCreateImageView(VulkanContext::GetDevice(), &viewCreateInfo, nullptr, &m_imageViews.back()), "Failed to create image views!");

#ifdef VDEBUG
    if(!m_debugName.empty())
    {
        std::string name = m_debugName + " image view " + std::to_string(m_imageViews.size() - 1);

        VK_SET_DEBUG_NAME(m_imageViews.back(), VK_OBJECT_TYPE_IMAGE_VIEW, name.c_str());
    }
#endif


    return m_imageViews.back();
}


// TODO add layer support for other functions

void Image::TransitionLayout(VkImageLayout newLayout)
{
    CommandBuffer commandBuffer;
    commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VkImageMemoryBarrier barrier = {};
    barrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout            = m_layout;
    barrier.newLayout            = newLayout;
    barrier.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;  // these are for transfering
    barrier.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;  // queue family ownership but we dont want to

    barrier.image = m_image;

    barrier.subresourceRange.aspectMask = m_aspect;

    if(hasStencilComponent(m_format))
    {
        barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = VK_REMAINING_ARRAY_LAYERS;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    sourceStage           = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    destinationStage      = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    vkCmdPipelineBarrier(commandBuffer.GetCommandBuffer(),
                         sourceStage, destinationStage,
                         0,
                         0, nullptr, 0, nullptr,  // these are for other types of barriers
                         1, &barrier);

    commandBuffer.SubmitIdle();
    m_layout = newLayout;
}

void Image::GenerateMipmaps(VkImageLayout newLayout)
{
    if(m_mipLevels == 1)
    {
        Log::Warn("Image::GenerateMipmaps called on an image that has only one mip level");
        TransitionLayout(newLayout);
        return;
    }
    // Check if image format supports linear blitting
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(VulkanContext::GetPhysicalDevice(), m_format, &formatProperties);
    if(!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        throw std::runtime_error("Texture image format does not support linear blitting!");
    }


    CommandBuffer commandBuffer;
    commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VkImageMemoryBarrier barrier            = {};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image                           = m_image;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = m_layerCount;
    barrier.subresourceRange.levelCount     = 1;

    // Transition first mip level to transfer source for read during blit
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.oldLayout                     = m_layout;
    barrier.newLayout                     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask                 = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer.GetCommandBuffer(),
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);

    int32_t mipWidth  = m_width;
    int32_t mipHeight = m_height;

    for(uint32_t i = 1; i < m_mipLevels; i++)
    {
        barrier.subresourceRange.baseMipLevel = i;
        barrier.oldLayout                     = m_layout;
        barrier.newLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask                 = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer.GetCommandBuffer(),
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);

        VkImageBlit blit                   = {};  // blit means copy image and possibly resize
        blit.srcOffsets[0]                 = {0, 0, 0};
        blit.srcOffsets[1]                 = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel       = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount     = m_layerCount;

        blit.dstOffsets[0]                 = {0, 0, 0};
        blit.dstOffsets[1]                 = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
        blit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel       = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount     = m_layerCount;

        vkCmdBlitImage(commandBuffer.GetCommandBuffer(),
                       m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, VK_FILTER_LINEAR);

        barrier.subresourceRange.baseMipLevel = i;
        barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout                     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask                 = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer.GetCommandBuffer(),
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);


        if(mipWidth > 1)
            mipWidth /= 2;
        if(mipHeight > 1)
            mipHeight /= 2;
    }

    // transition the mip images to new layout
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount   = m_mipLevels;
    barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout                     = newLayout;
    barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask                 = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer.GetCommandBuffer(),
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);

    commandBuffer.SubmitIdle();

    m_layout = newLayout;
}

VkImageMemoryBarrier2 Image::GetBarrier(VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags2 srcStage, VkAccessFlagBits2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlagBits2 dstAccess)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.oldLayout                       = oldLayout;
    barrier.newLayout                       = newLayout;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = m_image;
    barrier.subresourceRange.aspectMask     = m_aspect;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;
    barrier.subresourceRange.levelCount     = 1;
    barrier.srcAccessMask                   = srcAccess;
    barrier.dstAccessMask                   = dstAccess;
    barrier.srcStageMask                    = srcStage;
    barrier.dstStageMask                    = dstStage;
    return barrier;
}

uint32_t Image::GetBytesPerPixel() const
{
    uint32_t componentSize = 0;

    const struct VKU_FORMAT_INFO format_info = vkuGetFormatInfo(m_format);
    for(size_t i = 0; i < VKU_FORMAT_MAX_COMPONENTS; i++)
    {
        componentSize += format_info.components[i].size / 8;
    }
    return componentSize;
}

uint64_t Image::GetMemorySize() const
{
    uint32_t componentSize = GetBytesPerPixel();
    if(m_isCubeMap)
        return m_width * m_height * componentSize * 6;
    else
        return m_width * m_height * componentSize;
}
