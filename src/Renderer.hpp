#pragma once

#include "VulkanContext.hpp"
#include "CommandBuffer.hpp"

#include "Image.hpp"
#include "Window.hpp"
#include <functional>
#include <memory>


class Renderer
{
public:
    Renderer(const std::shared_ptr<Window>& window);
    ~Renderer();
    void Render(float dt);
    // Func(Commandbuffer& cb, Image& frambuffer, uint32_t frameIndex, float dt)
    // They get called in order of insertion, no synchronization is added between them
    void Enqueue(const std::function<void(CommandBuffer&, Image&, uint32_t, float)>& func) { m_renderCommands.push_back(func); }

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

private:
    void CreateInstance();
    void CreateDevice();
    void CreateSwapchain();
    void RecreateSwapchain();
    void CleanupSwapchain();
    void CreateCommandPool();
    void CreateCommandBuffers();
    void CreateSyncObjects();
    void CreateDescriptorPool();

    std::shared_ptr<Window> m_window;
    VkSurfaceKHR m_surface;
    VkSwapchainKHR m_swapchain;

    uint32_t m_currentFrame = 0;

    std::vector<Image> m_swapchainImages;
    std::vector<std::shared_ptr<Image>> m_framebuffers;
    std::vector<VkSemaphore> m_imageAvailable;
    std::vector<VkSemaphore> m_renderFinished;
    std::vector<VkFence> m_inFlightFences;
    std::vector<VkFence> m_imagesInFlight;

    std::vector<CommandBuffer> m_mainCommandBuffers;

    std::vector<std::function<void(CommandBuffer&, Image&, uint32_t, float)>> m_renderCommands;
};
