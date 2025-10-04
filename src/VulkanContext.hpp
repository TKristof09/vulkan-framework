#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>
#include "Log.hpp"

#define NUM_FRAMES_IN_FLIGHT 2
static constexpr uint32_t NUM_DESCRIPTORS = 10000;

class Pipeline;
class VulkanContext
{
public:
    static VkInstance GetInstance() { return m_instance; }
    static VkDevice GetDevice() { return m_device; }
    static VkPhysicalDevice GetPhysicalDevice() { return m_gpu; }
    static VkPhysicalDeviceProperties GetPhysicalDeviceProperties() { return m_gpuProperties; }
    static VkQueue GetQueue() { return m_queue; }
    static uint32_t GetQueueIndex() { return m_queueIndex; }
    static VkCommandPool GetCommandPool() { return m_commandPool; }
    static VkDescriptorPool GetDescriptorPool() { return m_descriptorPool; }
    static VkFormat GetSwapchainImageFormat() { return m_swapchainImageFormat; }
    static VkFormat GetDepthFormat() { return m_depthFormat; }
    static VkFormat GetStencilFormat() { return m_stencilFormat; }
    static VkExtent2D GetSwapchainExtent() { return m_swapchainExtent; }

    static VkPushConstantRange GetGlobalPushConstantRange() { return m_globalPushConstantRange; }

    static VkSampler GetTextureSampler() { return m_textureSampler; }

    static VmaAllocator GetVmaAllocator() { return m_vmaAllocator; }

    static VkViewport GetViewport(uint32_t width, uint32_t height)
    {
        VkViewport viewport = {};
        viewport.width      = static_cast<float>(width);
        viewport.height     = -static_cast<float>(height);
        viewport.x          = 0.0f;
        viewport.y          = static_cast<float>(height);
        viewport.minDepth   = 0.0f;
        viewport.maxDepth   = 1.0f;
        return viewport;
    }


#ifdef VDEBUG
    inline static PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT = nullptr;
    inline static PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT = nullptr;
    inline static PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT     = nullptr;
#endif

private:
    friend class Renderer;


    inline static VkDevice m_device                          = VK_NULL_HANDLE;
    inline static VkPhysicalDevice m_gpu                     = VK_NULL_HANDLE;
    inline static VkInstance m_instance                      = VK_NULL_HANDLE;
    inline static VkPhysicalDeviceProperties m_gpuProperties = {};

    inline static VkQueue m_queue = {};
    inline static uint32_t m_queueIndex;

    inline static VkCommandPool m_commandPool       = VK_NULL_HANDLE;
    inline static VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    inline static VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;

    inline static VkFormat m_swapchainImageFormat = VK_FORMAT_UNDEFINED;
    inline static VkFormat m_depthFormat          = VK_FORMAT_D32_SFLOAT;
    inline static VkFormat m_stencilFormat        = VK_FORMAT_UNDEFINED;
    inline static VkExtent2D m_swapchainExtent    = {};

    inline static VkPushConstantRange m_globalPushConstantRange = {};

    inline static VkSampler m_textureSampler = VK_NULL_HANDLE;

    inline static VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;
};


static inline void VK_CHECK(int result, const std::string& error_message)
{
    if(result != VK_SUCCESS && result != VK_ERROR_VALIDATION_FAILED_EXT)
    {
        Log::Error("{} | {}", string_VkResult((VkResult)result), error_message);
        abort();
    }
}

#ifdef VDEBUG
// NOLINTBEGIN (cppcoreguidelines-pro-type-cstyle-cast)
#define VK_SET_DEBUG_NAME(obj, objType, name)                                                        \
    {                                                                                                \
        if(VulkanContext::vkSetDebugUtilsObjectNameEXT == nullptr)                                   \
        {                                                                                            \
            VulkanContext::vkSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)         \
                vkGetInstanceProcAddr(VulkanContext::GetInstance(), "vkSetDebugUtilsObjectNameEXT"); \
        }                                                                                            \
        VkDebugUtilsObjectNameInfoEXT nameInfo = {};                                                 \
        nameInfo.sType                         = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT; \
        nameInfo.objectType                    = objType;                                            \
        nameInfo.objectHandle                  = (uint64_t)(obj);                                    \
        nameInfo.pObjectName                   = name;                                               \
        VulkanContext::vkSetDebugUtilsObjectNameEXT(VulkanContext::GetDevice(), &nameInfo);          \
    }

#define VK_START_DEBUG_LABEL(cmdBuffer, name)                                                        \
    {                                                                                                \
        if(VulkanContext::vkCmdBeginDebugUtilsLabelEXT == nullptr)                                   \
        {                                                                                            \
            VulkanContext::vkCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)         \
                vkGetInstanceProcAddr(VulkanContext::GetInstance(), "vkCmdBeginDebugUtilsLabelEXT"); \
        }                                                                                            \
        VkDebugUtilsLabelEXT label = {};                                                             \
        label.sType                = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;                        \
        label.pLabelName           = name;                                                           \
        label.color[0]             = 0.0f;                                                           \
        label.color[1]             = 1.0f;                                                           \
        label.color[2]             = 0.0f;                                                           \
        label.color[3]             = 1.0f;                                                           \
        VulkanContext::vkCmdBeginDebugUtilsLabelEXT(cmdBuffer.GetCommandBuffer(), &label);           \
    }

#define VK_END_DEBUG_LABEL(cmdBuffer)                                                              \
    {                                                                                              \
        if(VulkanContext::vkCmdEndDebugUtilsLabelEXT == nullptr)                                   \
        {                                                                                          \
            VulkanContext::vkCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)           \
                vkGetInstanceProcAddr(VulkanContext::GetInstance(), "vkCmdEndDebugUtilsLabelEXT"); \
        }                                                                                          \
        VulkanContext::vkCmdEndDebugUtilsLabelEXT(cmdBuffer.GetCommandBuffer());                   \
    }

// NOLINTEND (cppcoreguidelines-pro-type-cstyle-cast)
#else
#define VK_SET_DEBUG_NAME(obj, objType, name)
#define VK_START_DEBUG_LABEL(cmdBuffer, name)
#define VK_END_DEBUG_LABEL(cmdBuffer)


#endif
