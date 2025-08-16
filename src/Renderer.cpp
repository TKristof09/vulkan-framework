#include "Renderer.hpp"

#include "VulkanContext.hpp"
#include <GLFW/glfw3.h>

#include "Application.hpp"
#include "Window.hpp"
#include "Log.hpp"

#include <iostream>
#include <sstream>

#include <glm/glm.hpp>

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_glfw.h>


VkBool32 debugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData, void* /*pUserData*/);
VkSurfaceFormatKHR ChooseSwapchainFormat(VkPhysicalDevice device, VkSurfaceKHR surface);
VkPresentModeKHR ChooseSwapchainPresentMode(VkPhysicalDevice device, VkSurfaceKHR surface);
VkExtent2D ChooseSwapchainExtent(VkPhysicalDevice device, VkSurfaceKHR surface, GLFWwindow* window);


std::vector<const char*> GetExtensions()
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = nullptr;
    glfwExtensions              = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
#ifdef VDEBUG
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
    return extensions;
}

Renderer::Renderer(const std::shared_ptr<Window>& window) : m_window(window)
{
    CreateInstance();
    CreateDevice();

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice         = VulkanContext::GetPhysicalDevice();
    allocatorInfo.device                 = VulkanContext::GetDevice();
    allocatorInfo.instance               = VulkanContext::GetInstance();
    allocatorInfo.vulkanApiVersion       = VK_API_VERSION_1_3;
    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &VulkanContext::m_vmaAllocator), "Failed to create vma allocator");
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    CreateCommandPool();
    CreateDescriptorPool();
    CreateSwapchain();

    CreateCommandBuffers();
    CreateSyncObjects();

    SetupImgui();
}

Renderer::~Renderer()
{
    vkDeviceWaitIdle(VulkanContext::GetDevice());


    for(size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(VulkanContext::GetDevice(), m_imageAvailable[i], nullptr);
        vkDestroyFence(VulkanContext::GetDevice(), m_inFlightFences[i], nullptr);
    }
    for(size_t i = 0; i < m_renderFinished.size(); i++)
    {
        vkDestroySemaphore(VulkanContext::GetDevice(), m_renderFinished[i], nullptr);
    }

    CleanupSwapchain();


    vkDestroyDescriptorPool(VulkanContext::GetDevice(), VulkanContext::GetDescriptorPool(), nullptr);

    vkDestroySurfaceKHR(VulkanContext::GetInstance(), m_surface, nullptr);

    vmaDestroyAllocator(VulkanContext::GetVmaAllocator());

    vkDestroyCommandPool(VulkanContext::GetDevice(), VulkanContext::GetCommandPool(), nullptr);

    vkDestroyDevice(VulkanContext::GetDevice(), nullptr);

#ifdef VDEBUG
    VulkanContext::DestroyDebugUtilsMessengerEXT(VulkanContext::GetInstance(), VulkanContext::m_messenger, nullptr);
#endif

    vkDestroyInstance(VulkanContext::GetInstance(), nullptr);
}

void Renderer::CreateInstance()
{
    VkApplicationInfo appinfo = {};
    appinfo.sType             = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appinfo.pApplicationName  = "VukanApplication";
    appinfo.apiVersion        = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType                = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo     = &appinfo;

    auto extensions                    = GetExtensions();
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());

    Log::Info("Requested extensions");
    for(auto ext : extensions)
    {
        Log::Info("    {}", ext);
    }


    // The VK_LAYER_KHRONOS_validation contains all current validation functionality.
    const char* validationLayerName = "VK_LAYER_KHRONOS_validation";
    // Check if this layer is available at instance level

    createInfo.ppEnabledLayerNames = &validationLayerName;
    createInfo.enabledLayerCount   = 1;


    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};
    debugCreateInfo.sType                              = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugCreateInfo.messageSeverity                    = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugCreateInfo.messageType                        = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugCreateInfo.pfnUserCallback                    = debugUtilsMessengerCallback;


    createInfo.pNext = &debugCreateInfo;

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &VulkanContext::m_instance), "Failed to create instance");

    VK_CHECK(glfwCreateWindowSurface(VulkanContext::m_instance, Application::GetInstance()->GetWindow()->GetWindow(), nullptr, &m_surface), "Failed to create window surface!");


    VkDebugUtilsMessengerCreateInfoEXT dcreateInfo = {};
    dcreateInfo.sType                              = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dcreateInfo.messageSeverity                    = /*VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |*/ VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dcreateInfo.messageType                        = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dcreateInfo.pfnUserCallback                    = debugUtilsMessengerCallback;

    VulkanContext::CreateDebugUtilsMessengerEXT(VulkanContext::GetInstance(), &dcreateInfo, nullptr, &VulkanContext::m_messenger);


    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> actual_extensions(extCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, actual_extensions.data());

    Log::Info("Available extensions:");
    for(auto& ext : actual_extensions)
    {
        Log::Info("     {}", ext.extensionName);
    }
}

void Renderer::CreateDevice()
{
    std::vector<const char*> deviceExtensions;
    deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
#ifdef VDEBUG
    deviceExtensions.push_back(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
#endif

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(VulkanContext::GetInstance(), &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(VulkanContext::GetInstance(), &deviceCount, devices.data());

    for(auto device : devices)
    {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);
        if(deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            VulkanContext::m_gpu = device;
        }
    }


    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

    uint32_t queueFamilyIndex = -1;
    uint32_t count;
    vkGetPhysicalDeviceQueueFamilyProperties(VulkanContext::GetPhysicalDevice(), &count, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(count);
    vkGetPhysicalDeviceQueueFamilyProperties(VulkanContext::GetPhysicalDevice(), &count, queueFamilies.data());
    uint32_t i = 0;
    for(const auto& queueFamily : queueFamilies)
    {
        if(queueFamily.queueCount > 0 && queueFamily.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
        {
            VkBool32 presentationSupport;
            vkGetPhysicalDeviceSurfaceSupportKHR(VulkanContext::GetPhysicalDevice(), i, m_surface, &presentationSupport);
            if(queueFamily.queueCount > 0 && presentationSupport)
            {
                queueFamilyIndex = i;
                break;
            }
        }
    }

    float queuePriority                     = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex        = queueFamilyIndex;
    queueCreateInfo.queueCount              = 1;
    queueCreateInfo.pQueuePriorities        = &queuePriority;
    queueCreateInfos.push_back(queueCreateInfo);

    VkPhysicalDeviceFeatures deviceFeatures             = {};
    deviceFeatures.samplerAnisotropy                    = VK_TRUE;
    deviceFeatures.sampleRateShading                    = VK_TRUE;
    deviceFeatures.shaderInt64                          = VK_TRUE;
    deviceFeatures.multiDrawIndirect                    = VK_TRUE;
    deviceFeatures.shaderStorageImageReadWithoutFormat  = VK_TRUE;
    deviceFeatures.shaderStorageImageWriteWithoutFormat = VK_TRUE;
    deviceFeatures.pipelineStatisticsQuery              = VK_TRUE;

    // deviceFeatures.depthBounds = VK_TRUE; //doesnt work on my surface 2017

    VkPhysicalDeviceVulkan11Features device11Features = {};
    device11Features.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    device11Features.shaderDrawParameters             = VK_TRUE;
    device11Features.multiview                        = VK_TRUE;

    VkPhysicalDeviceVulkan12Features device12Features             = {};
    device12Features.sType                                        = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    // these are for dynamic descriptor indexing
    deviceFeatures.shaderSampledImageArrayDynamicIndexing         = VK_TRUE;
    deviceFeatures.shaderStorageBufferArrayDynamicIndexing        = VK_TRUE;
    device12Features.shaderSampledImageArrayNonUniformIndexing    = VK_TRUE;
    device12Features.runtimeDescriptorArray                       = VK_TRUE;
    device12Features.descriptorBindingPartiallyBound              = VK_TRUE;
    device12Features.descriptorBindingUpdateUnusedWhilePending    = VK_TRUE;
    device12Features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    device12Features.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
    device12Features.bufferDeviceAddress                          = VK_TRUE;
    device12Features.drawIndirectCount                            = VK_TRUE;

    VkPhysicalDeviceVulkan13Features device13Features = {};
    device13Features.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    device13Features.dynamicRendering                 = VK_TRUE;
    device13Features.maintenance4                     = VK_TRUE;  // need it because the spirv compiler uses localsizeid even though it doesnt need to for now, but i might switch to spec constants in the future anyway
    device13Features.synchronization2                 = VK_TRUE;


    VkDeviceCreateInfo createInfo      = {};
    createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.enabledExtensionCount   = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    createInfo.pEnabledFeatures        = &deviceFeatures;
    createInfo.pQueueCreateInfos       = queueCreateInfos.data();
    createInfo.queueCreateInfoCount    = static_cast<uint32_t>(queueCreateInfos.size());

    createInfo.pNext       = &device11Features;
    device11Features.pNext = &device12Features;
    device12Features.pNext = &device13Features;

    VK_CHECK(vkCreateDevice(VulkanContext::GetPhysicalDevice(), &createInfo, nullptr, &VulkanContext::m_device), "Failed to create device");
    vkGetDeviceQueue(VulkanContext::GetDevice(), queueFamilyIndex, 0, &VulkanContext::m_queue);
    VulkanContext::m_queueIndex = queueFamilyIndex;
}

void Renderer::CreateSwapchain()
{
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VulkanContext::GetPhysicalDevice(), m_surface, &capabilities);

    auto surfaceFormat = ChooseSwapchainFormat(VulkanContext::GetPhysicalDevice(), m_surface);
    auto extent        = ChooseSwapchainExtent(VulkanContext::GetPhysicalDevice(), m_surface, Application::GetInstance()->GetWindow()->GetWindow());
    auto presentMode   = ChooseSwapchainPresentMode(VulkanContext::GetPhysicalDevice(), m_surface);

    VulkanContext::m_swapchainExtent = extent;

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface                  = m_surface;
    createInfo.imageFormat              = surfaceFormat.format;
    createInfo.minImageCount            = capabilities.minImageCount + 1;
    createInfo.imageColorSpace          = surfaceFormat.colorSpace;
    createInfo.imageExtent              = extent;
    createInfo.presentMode              = presentMode;
    createInfo.imageUsage               = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    createInfo.imageArrayLayers         = 1;


    createInfo.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;  // this is the better performance
    createInfo.queueFamilyIndexCount = 0;                          // Optional
    createInfo.pQueueFamilyIndices   = nullptr;                    // Optional
    createInfo.compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.preTransform          = capabilities.currentTransform;
    createInfo.clipped               = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(VulkanContext::GetDevice(), &createInfo, nullptr, &m_swapchain), "Failed to create swapchain");

    uint32_t imageCount = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(VulkanContext::GetDevice(), m_swapchain, &imageCount, nullptr), "Failed to get swapchain images");
    std::vector<VkImage> swapchainImages(imageCount);
    VK_CHECK(vkGetSwapchainImagesKHR(VulkanContext::GetDevice(), m_swapchain, &imageCount, swapchainImages.data()), "Failed to get swapchain images");
    VulkanContext::m_swapchainExtent      = extent;
    VulkanContext::m_swapchainImageFormat = surfaceFormat.format;

    for(const auto& img : swapchainImages)
    {
        ImageCreateInfo ci = {};
        ci.image           = img;
        ci.aspectFlags     = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.format          = VulkanContext::GetSwapchainImageFormat();

        m_swapchainImages.emplace_back(VulkanContext::GetSwapchainExtent(), ci);
    }
    m_framebuffers.reserve(m_swapchainImages.size());
    for(size_t i = 0; i < m_swapchainImages.size(); i++)
    {
        ImageCreateInfo ci = {};
        ci.layout          = VK_IMAGE_LAYOUT_GENERAL;
        ci.format          = VulkanContext::GetSwapchainImageFormat();
        ci.aspectFlags     = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.usage           = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        ci.debugName       = "Framebuffer " + std::to_string(i);
        m_framebuffers.emplace_back(std::make_shared<Image>(extent, ci));
    }
}

void Renderer::RecreateSwapchain()
{
    while(m_window->GetWidth() == 0 || m_window->GetHeight() == 0)
        glfwWaitEvents();


    vkDeviceWaitIdle(VulkanContext::GetDevice());


    CleanupSwapchain();

    vkDeviceWaitIdle(VulkanContext::GetDevice());

    CreateSwapchain();


    CreateCommandBuffers();

    SetupImgui();
}

void Renderer::CleanupSwapchain()
{
    vkDeviceWaitIdle(VulkanContext::GetDevice());


    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    m_mainCommandBuffers.clear();
    m_framebuffers.clear();


    vkDestroySwapchainKHR(VulkanContext::GetDevice(), m_swapchain, nullptr);
    m_swapchainImages.clear();
}
void Renderer::CreateCommandPool()
{
    VkCommandPoolCreateInfo createInfo = {};
    createInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createInfo.queueFamilyIndex        = VulkanContext::GetQueueIndex();
    createInfo.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VK_CHECK(vkCreateCommandPool(VulkanContext::GetDevice(), &createInfo, nullptr, &VulkanContext::m_commandPool), "Failed to create command pool");
}

void Renderer::CreateCommandBuffers()
{
    for(size_t i = 0; i < m_swapchainImages.size(); i++)
    {
        m_mainCommandBuffers.emplace_back();
    }
}

void Renderer::CreateSyncObjects()
{
    m_imageAvailable.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinished.resize(m_swapchainImages.size());
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_imagesInFlight.resize(m_swapchainImages.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags             = VK_FENCE_CREATE_SIGNALED_BIT;

    for(size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VK_CHECK(vkCreateSemaphore(VulkanContext::GetDevice(), &semaphoreInfo, nullptr, &m_imageAvailable[i]), "Failed to create semaphore");

        VK_CHECK(vkCreateFence(VulkanContext::GetDevice(), &fenceInfo, nullptr, &m_inFlightFences[i]), "Failed to create fence");
    }
    for(size_t i = 0; i < m_swapchainImages.size(); ++i)
    {
        VK_CHECK(vkCreateSemaphore(VulkanContext::GetDevice(), &semaphoreInfo, nullptr, &m_renderFinished[i]), "Failed to create semaphore");
    }
}

void Renderer::CreateDescriptorPool()
{
    std::array<VkDescriptorPoolSize, 4> poolSizes = {};
    poolSizes[0].type                             = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount                  = NUM_DESCRIPTORS;
    poolSizes[1].type                             = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount                  = NUM_DESCRIPTORS;
    poolSizes[2].type                             = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount                  = NUM_DESCRIPTORS;
    poolSizes[3].type                             = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[3].descriptorCount                  = NUM_DESCRIPTORS;


    VkDescriptorPoolCreateInfo createInfo = {};
    createInfo.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    createInfo.poolSizeCount              = static_cast<uint32_t>(poolSizes.size());
    createInfo.pPoolSizes                 = poolSizes.data();
    createInfo.maxSets                    = 100;
    createInfo.flags                      = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    VK_CHECK(vkCreateDescriptorPool(VulkanContext::GetDevice(), &createInfo, nullptr, &VulkanContext::m_descriptorPool), "Failed to create descriptor pool");
}

void Renderer::SetupImgui()
{
    ImGui::CreateContext();
    ImGuiIO& io     = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(m_window->GetWindow(), true);


    VkPipelineRenderingCreateInfo pipelineRendering{};
    pipelineRendering.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipelineRendering.colorAttachmentCount    = 1;
    VkFormat colorAttachment                  = VulkanContext::GetSwapchainImageFormat();
    pipelineRendering.pColorAttachmentFormats = &colorAttachment;


    ImGui_ImplVulkan_InitInfo imguiInitInfo   = {};
    imguiInitInfo.Instance                    = VulkanContext::GetInstance();
    imguiInitInfo.PhysicalDevice              = VulkanContext::GetPhysicalDevice();
    imguiInitInfo.Device                      = VulkanContext::GetDevice();
    imguiInitInfo.QueueFamily                 = VulkanContext::GetQueueIndex();
    imguiInitInfo.Queue                       = VulkanContext::GetQueue();
    imguiInitInfo.PipelineCache               = VK_NULL_HANDLE;
    imguiInitInfo.DescriptorPool              = VulkanContext::GetDescriptorPool();
    imguiInitInfo.Allocator                   = nullptr;
    imguiInitInfo.MinImageCount               = MAX_FRAMES_IN_FLIGHT;
    imguiInitInfo.ImageCount                  = m_swapchainImages.size();
    imguiInitInfo.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
    imguiInitInfo.UseDynamicRendering         = true;
    imguiInitInfo.PipelineRenderingCreateInfo = pipelineRendering;

    // clang-format off
    // : idk why clang-format puts the lambda body on a new line
    imguiInitInfo.CheckVkResultFn = [](VkResult result) { VK_CHECK(result, "Error in imgui"); };
    // clang-format on
    ImGui_ImplVulkan_Init(&imguiInitInfo);
}

void Renderer::Render(float dt)
{
    VkDevice device            = VulkanContext::GetDevice();
    VkExtent2D swapchainExtent = VulkanContext::GetSwapchainExtent();

    uint32_t imageIndex;
    VkResult result;
    vkWaitForFences(device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    result = vkAcquireNextImageKHR(device, m_swapchain, UINT64_MAX, m_imageAvailable[m_currentFrame], VK_NULL_HANDLE, &imageIndex);


    if(result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RecreateSwapchain();
        return;
    }
    else if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("Failed to acquire swap chain image");

    // Check if a previous frame is using this image (i.e. there is its fence to wait on)
    if(m_imagesInFlight[imageIndex] != VK_NULL_HANDLE)
        vkWaitForFences(device, 1, &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    // Mark the image as now being in use by this frame
    m_imagesInFlight[imageIndex] = m_inFlightFences[m_currentFrame];


    VK_CHECK(vkResetFences(device, 1, &m_inFlightFences[m_currentFrame]), "Failed to reset in flight fences");


    CommandBuffer& cb = m_mainCommandBuffers[imageIndex];
    cb.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    {
        std::array<VkImageMemoryBarrier2, 2> barriers{};

        // transfer swapchain image to transfer dst layout because we will blit to it
        barriers[0].sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barriers[0].oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[0].newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[0].srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].image                           = m_swapchainImages[imageIndex].GetImage();
        barriers[0].subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[0].subresourceRange.baseMipLevel   = 0;
        barriers[0].subresourceRange.baseArrayLayer = 0;
        barriers[0].subresourceRange.layerCount     = 1;
        barriers[0].subresourceRange.levelCount     = 1;
        barriers[0].srcAccessMask                   = 0;
        barriers[0].dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[0].srcStageMask                    = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        barriers[0].dstStageMask                    = VK_PIPELINE_STAGE_TRANSFER_BIT;


        barriers[1].sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barriers[1].oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[1].newLayout                       = VK_IMAGE_LAYOUT_GENERAL;
        barriers[1].srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].image                           = m_framebuffers[imageIndex]->GetImage();
        barriers[1].subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[1].subresourceRange.baseMipLevel   = 0;
        barriers[1].subresourceRange.baseArrayLayer = 0;
        barriers[1].subresourceRange.layerCount     = 1;
        barriers[1].subresourceRange.levelCount     = 1;
        barriers[1].srcAccessMask                   = 0;
        barriers[1].dstAccessMask                   = 0;
        barriers[1].srcStageMask                    = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        barriers[1].dstStageMask                    = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;


        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.dependencyFlags          = 0;
        dependencyInfo.bufferMemoryBarrierCount = 0;
        dependencyInfo.pBufferMemoryBarriers    = nullptr;
        dependencyInfo.imageMemoryBarrierCount  = 2;
        dependencyInfo.pImageMemoryBarriers     = barriers.data();
        dependencyInfo.memoryBarrierCount       = 0;
        dependencyInfo.pMemoryBarriers          = nullptr;


        vkCmdPipelineBarrier2(cb.GetCommandBuffer(), &dependencyInfo);
    }

    VkClearColorValue clearColor = {};
    clearColor.float32[3]        = 1.0f;

    VkImageSubresourceRange range = {};
    range.layerCount              = 1;
    range.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount              = 1;
    vkCmdClearColorImage(cb.GetCommandBuffer(), m_framebuffers[imageIndex]->GetImage(), VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &range);
    {
        VkImageMemoryBarrier2 barrier{};

        // wait for the clear to finish before running the enqueued commands
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = m_framebuffers[imageIndex]->GetImage();
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.subresourceRange.levelCount     = 1;
        barrier.srcAccessMask                   = 0;
        barrier.dstAccessMask                   = 0;
        barrier.srcStageMask                    = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        barrier.dstStageMask                    = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.dependencyFlags          = 0;
        dependencyInfo.bufferMemoryBarrierCount = 0;
        dependencyInfo.pBufferMemoryBarriers    = nullptr;
        dependencyInfo.imageMemoryBarrierCount  = 1;
        dependencyInfo.pImageMemoryBarriers     = &barrier;
        dependencyInfo.memoryBarrierCount       = 0;
        dependencyInfo.pMemoryBarriers          = nullptr;


        vkCmdPipelineBarrier2(cb.GetCommandBuffer(), &dependencyInfo);
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    for(auto& command : m_renderCommands)
        command(cb, *m_framebuffers[imageIndex], m_currentFrame, dt);

    VkImageMemoryBarrier2 barrier{};

    // add a full synch in case any of the commands we ran wrote to the framebuffer
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.oldLayout                       = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout                       = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = m_framebuffers[imageIndex]->GetImage();
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;
    barrier.subresourceRange.levelCount     = 1;
    barrier.srcAccessMask                   = 0;
    barrier.dstAccessMask                   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.srcStageMask                    = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    barrier.dstStageMask                    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.dependencyFlags          = 0;
    dependencyInfo.bufferMemoryBarrierCount = 0;
    dependencyInfo.pBufferMemoryBarriers    = nullptr;
    dependencyInfo.imageMemoryBarrierCount  = 1;
    dependencyInfo.pImageMemoryBarriers     = &barrier;
    dependencyInfo.memoryBarrierCount       = 0;
    dependencyInfo.pMemoryBarriers          = nullptr;


    vkCmdPipelineBarrier2(cb.GetCommandBuffer(), &dependencyInfo);
    VkRenderingAttachmentInfo uiColorAttachment = {};
    uiColorAttachment.sType                     = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    uiColorAttachment.loadOp                    = VK_ATTACHMENT_LOAD_OP_LOAD;
    uiColorAttachment.storeOp                   = VK_ATTACHMENT_STORE_OP_STORE;
    uiColorAttachment.imageView                 = m_framebuffers[imageIndex]->GetImageView();
    uiColorAttachment.imageLayout               = VK_IMAGE_LAYOUT_GENERAL;


    VkRenderingInfo uiRenderingInfo      = {};
    uiRenderingInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    uiRenderingInfo.colorAttachmentCount = 1;
    uiRenderingInfo.pColorAttachments    = &uiColorAttachment;
    uiRenderingInfo.layerCount           = 1;
    uiRenderingInfo.renderArea.offset    = {0, 0};
    uiRenderingInfo.renderArea.extent    = swapchainExtent;

    vkCmdBeginRendering(cb.GetCommandBuffer(), &uiRenderingInfo);


    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb.GetCommandBuffer());

    vkCmdEndRendering(cb.GetCommandBuffer());

    {
        VkImageMemoryBarrier2 barrier{};

        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = m_framebuffers[imageIndex]->GetImage();
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.subresourceRange.levelCount     = 1;
        barrier.srcAccessMask                   = 0;
        barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.srcStageMask                    = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        barrier.dstStageMask                    = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.dependencyFlags          = 0;
        dependencyInfo.bufferMemoryBarrierCount = 0;
        dependencyInfo.pBufferMemoryBarriers    = nullptr;
        dependencyInfo.imageMemoryBarrierCount  = 1;
        dependencyInfo.pImageMemoryBarriers     = &barrier;
        dependencyInfo.memoryBarrierCount       = 0;
        dependencyInfo.pMemoryBarriers          = nullptr;


        vkCmdPipelineBarrier2(cb.GetCommandBuffer(), &dependencyInfo);
    }


    VkImageBlit region                   = {};
    region.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.mipLevel       = 0;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount     = 1;
    region.srcOffsets[0]                 = {0, 0, 0};
    region.srcOffsets[1]                 = {static_cast<int32_t>(swapchainExtent.width), static_cast<int32_t>(swapchainExtent.height), 1};
    region.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.mipLevel       = 0;
    region.dstSubresource.baseArrayLayer = 0;
    region.dstSubresource.layerCount     = 1;
    region.dstOffsets[0]                 = {0, 0, 0};
    region.dstOffsets[1]                 = {static_cast<int32_t>(swapchainExtent.width), static_cast<int32_t>(swapchainExtent.height), 1};
    vkCmdBlitImage(cb.GetCommandBuffer(), m_framebuffers[imageIndex]->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_swapchainImages[imageIndex].GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);


    // transition swapchain image to present leayout
    {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = m_swapchainImages[imageIndex].GetImage();
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.subresourceRange.levelCount     = 1;
        barrier.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_MEMORY_READ_BIT;
        barrier.srcStageMask                    = VK_PIPELINE_STAGE_TRANSFER_BIT;
        barrier.dstStageMask                    = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers    = &barrier;

        vkCmdPipelineBarrier2(cb.GetCommandBuffer(), &dependencyInfo);
    }

    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    cb.Submit(m_imageAvailable[m_currentFrame], wait, m_renderFinished[imageIndex], m_inFlightFences[m_currentFrame]);

    VkPresentInfoKHR presentInfo   = {};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &m_renderFinished[imageIndex];


    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains    = &m_swapchain;
    presentInfo.pImageIndices  = &imageIndex;
    result                     = vkQueuePresentKHR(VulkanContext::GetQueue(), &presentInfo);

    if(result == VK_ERROR_DEVICE_LOST)
        VK_CHECK(result, "Queue present failed");
    if(result == VK_SUBOPTIMAL_KHR || m_window->IsResized())
    {
        m_window->SetResized(false);
        RecreateSwapchain();
    }
    else
        VK_CHECK(result, "Failed to present the swapchain image");

    // LOG_INFO("GPU took {0} ms", m_queryResults[m_currentFrame] * 1e-6);

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

VkSurfaceFormatKHR ChooseSwapchainFormat(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    uint32_t count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> availableFormats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, availableFormats.data());
    // if the surface has no preferred format vulkan returns one entity of Vk_FORMAT_UNDEFINED
    // we can then chose whatever we want
    if(availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED)
    {
        VkSurfaceFormatKHR format;
        format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        format.format     = VK_FORMAT_B8G8R8A8_UNORM;
        return format;
    }
    for(const auto& format : availableFormats)
    {
        if(format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return format;
    }

    // if we can't find one that we like we could rank them based on how good they are
    // but we will just settle for the first one(apparently in most cases it's okay)
    return availableFormats[0];
}

VkPresentModeKHR ChooseSwapchainPresentMode(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    uint32_t count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> availablePresentModes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, availablePresentModes.data());

    for(const auto& presentMode : availablePresentModes)
    {
        if(presentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            return presentMode;
        if(presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
            return presentMode;
    }

    // FIFO is guaranteed to be available
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D ChooseSwapchainExtent(VkPhysicalDevice device, VkSurfaceKHR surface, GLFWwindow* window)
{
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &capabilities);

    // if we can set an extent manually the width and height values will be uint32t max
    // else we can't set it so just return it
    if(capabilities.currentExtent.width != (std::numeric_limits<uint32_t>::max)())  // () around max to prevent macro expansion by windows.h max macro
        return capabilities.currentExtent;
    else
    {
        // choose an extent within the minImageExtent and maxImageExtent bounds
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
        actualExtent.width      = glm::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height     = glm::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }
}
VkBool32 debugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                     VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData, void* /*pUserData*/)
{
    std::ostringstream message;
    message << "\n";

    std::string messageidName = "";
    if(pCallbackData->pMessageIdName)
        messageidName = pCallbackData->pMessageIdName;

    message << "\t"
            << "messageIDName   = <" << messageidName << ">\n";


    message << "\t"
            << "messageIdNumber = " << pCallbackData->messageIdNumber << "\n";

    message << "\t"
            << "messageType     = <" << string_VkDebugUtilsMessageTypeFlagsEXT(messageTypes) << ">\n";

    if(pCallbackData->pMessage)
        message << "\t"
                << "message         = <" << pCallbackData->pMessage << ">\n";


    if(0 < pCallbackData->queueLabelCount)
    {
        message << "\t"
                << "Queue Labels:\n";
        for(uint8_t i = 0; i < pCallbackData->queueLabelCount; i++)
        {
            message << "\t\t"
                    << "labelName = <" << pCallbackData->pQueueLabels[i].pLabelName << ">\n";
        }
    }
    if(0 < pCallbackData->cmdBufLabelCount)
    {
        message << "\t"
                << "CommandBuffer Labels:\n";
        for(uint8_t i = 0; i < pCallbackData->cmdBufLabelCount; i++)
        {
            message << "\t\t"
                    << "labelName = <" << pCallbackData->pCmdBufLabels[i].pLabelName << ">\n";
        }
    }
    if(0 < pCallbackData->objectCount)
    {
        message << "\t"
                << "Objects:\n";
        for(uint8_t i = 0; i < pCallbackData->objectCount; i++)
        {
            message << "\t\t"
                    << "Object " << i << "\n";
            message << "\t\t\t"
                    << "objectType   = " << string_VkObjectType(pCallbackData->pObjects[i].objectType) << "\n";
            message << "\t\t\t"
                    << "objectHandle = " << pCallbackData->pObjects[i].objectHandle << "\n";
            if(pCallbackData->pObjects[i].pObjectName)
            {
                message << "\t\t\t"
                        << "objectName   = <" << pCallbackData->pObjects[i].pObjectName << ">\n";
            }
        }
    }

    if(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
    {
        Log::Info("{}", message.str());
    }
    else if(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
    {
        Log::Info("{}", message.str());
    }
    else if(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        Log::Warn("{}", message.str());
    }
    else if(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        Log::Error("{}", message.str());
    }


    return VK_TRUE;
}
