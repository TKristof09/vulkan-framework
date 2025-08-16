#include "Application.hpp"

#include "Time.hpp"
#include "Log.hpp"

#include "VulkanContext.hpp"
#include "Window.hpp"
#include "Renderer.hpp"

Application* Application::s_instance = nullptr;

Application::Application(uint32_t width, uint32_t height, uint32_t frameRate, const std::string& title) : m_frameTime(1.0 / frameRate)
{
    s_instance = this;


    m_window   = std::make_shared<Window>(width, height, title);
    m_renderer = std::make_unique<Renderer>(m_window);
}

Application::~Application()
{
    vkDeviceWaitIdle(VulkanContext::GetDevice());
    m_renderer.reset();
    glfwTerminate();
}


void Application::EnqueueRenderCommand(const std::function<void(CommandBuffer&, Image&, uint32_t, float)>& func)
{
    m_renderer->Enqueue(func);
}
void Application::Run()
{
    GLFWwindow* w = m_window->GetWindow();

    double lastTime = Time::GetTime();

    while(!m_window->ShouldClose())
    {
        double startTime = Time::GetTime();
        double deltaTime = startTime - lastTime;
        lastTime         = startTime;


        glfwPollEvents();  // TODO this blocks while you hold the title bar(or the resize cursor), only fix seems to be to render on another thread

        if(glfwGetKey(w, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            break;

        Time::SetDelta(deltaTime);

        m_renderer->Render(deltaTime);
    }

    vkDeviceWaitIdle(VulkanContext::GetDevice());
}
