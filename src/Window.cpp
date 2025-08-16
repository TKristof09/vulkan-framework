#include "Window.hpp"
#include "Log.hpp"
static void frambufferResizeCallback(GLFWwindow* window, int width, int height)
{
    auto w = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    w->_Resized(width, height);
}

Window::Window(uint32_t width, uint32_t height, const std::string& title) : m_width(width), m_height(height), m_title(title)
{
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    if(!glfwInit())
    {
        const char* err;
        glfwGetError(&err);
        Log::Error("Failed to initialise GLFW: {}", err);
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    m_window = glfwCreateWindow(width, height, title.data(), nullptr, nullptr);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, frambufferResizeCallback);
}

Window::~Window()
{
    glfwDestroyWindow(m_window);
    glfwTerminate();
}
