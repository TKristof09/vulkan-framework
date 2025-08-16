#pragma once


#include "CommandBuffer.hpp"
#include "Image.hpp"
#include <functional>
#include <memory>
class Renderer;
class Window;
class Application
{
public:
    static Application* GetInstance() { return s_instance; }

    Application(uint32_t width, uint32_t height, uint32_t frameRate,
                const std::string& title = "Vulkan Application");

    ~Application();

    void Run();

    void EnqueueRenderCommand(const std::function<void(CommandBuffer&, Image&, uint32_t, float)>& func);
    std::shared_ptr<Window> GetWindow() const { return m_window; }

private:
    std::shared_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
    double m_frameTime;

    static Application* s_instance;
};
