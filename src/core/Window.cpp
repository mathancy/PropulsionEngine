#include "core/Window.h"
#include <iostream>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

// STB Image for icon loading (implementation in Engine.cpp)
#include "stb_image.h"
#include <vector>

namespace Forge {

Window::Window(int width, int height, const std::string& title)
    : m_width(width), m_height(height) {
    
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        exit(EXIT_FAILURE);
    }

    // Configure GLFW for Vulkan
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    glfwSetWindowUserPointer(m_window, this);
    
    // Load and set window icon
    loadIcon();
    
    // Enable dark mode for title bar
    enableDarkMode();

    // Set callbacks
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
    glfwSetCursorPosCallback(m_window, cursorPosCallback);
    glfwSetScrollCallback(m_window, scrollCallback);
    glfwSetWindowCloseCallback(m_window, windowCloseCallback);
    glfwSetWindowContentScaleCallback(m_window, contentScaleCallback);
}

Window::~Window() {
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void Window::setShouldClose(bool shouldClose) {
    glfwSetWindowShouldClose(m_window, shouldClose ? GLFW_TRUE : GLFW_FALSE);
}

void Window::pollEvents() {
    glfwPollEvents();
}

void Window::setResizeCallback(std::function<void(int, int)> callback) {
    m_resizeCallback = callback;
}

float Window::getContentScale() const {
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(m_window, &xscale, &yscale);
    return xscale > 0.0f ? xscale : 1.0f;
}

void Window::setContentScaleCallback(std::function<void(float)> callback) {
    m_contentScaleCallback = callback;
}

void Window::contentScaleCallback(GLFWwindow* window, float xscale, float /*yscale*/) {
    Window* win = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win->m_contentScaleCallback) {
        win->m_contentScaleCallback(xscale);
    }
}

void Window::setKeyCallback(std::function<void(int, int, int, int)> callback) {
    m_keyCallback = callback;
}

void Window::setMouseButtonCallback(std::function<void(int, int, int)> callback) {
    m_mouseButtonCallback = callback;
}

void Window::setMouseMoveCallback(std::function<void(double, double)> callback) {
    m_mouseMoveCallback = callback;
}

void Window::setScrollCallback(std::function<void(double, double)> callback) {
    m_scrollCallback = callback;
}

void Window::setCloseCallback(std::function<void()> callback) {
    m_closeCallback = callback;
}

void Window::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    Window* win = static_cast<Window*>(glfwGetWindowUserPointer(window));
    win->m_width = width;
    win->m_height = height;
    if (win->m_resizeCallback) {
        win->m_resizeCallback(width, height);
    }
}

void Window::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    Window* win = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win->m_keyCallback) {
        win->m_keyCallback(key, scancode, action, mods);
    }
}

void Window::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    Window* win = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win->m_mouseButtonCallback) {
        win->m_mouseButtonCallback(button, action, mods);
    }
}

void Window::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    Window* win = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win->m_mouseMoveCallback) {
        win->m_mouseMoveCallback(xpos, ypos);
    }
}

void Window::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    Window* win = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win->m_scrollCallback) {
        win->m_scrollCallback(xoffset, yoffset);
    }
}

void Window::windowCloseCallback(GLFWwindow* window) {
    Window* win = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win->m_closeCallback) {
        // Prevent default close behavior - let the application handle it
        glfwSetWindowShouldClose(window, GLFW_FALSE);
        win->m_closeCallback();
    }
    // If no callback is set, window will close normally
}

void Window::loadIcon() {
    // Try to load icon from resources folder
    const char* iconPaths[] = {
        "resources/icons/favicon.png",
        "../resources/icons/favicon.png",
        "../../resources/icons/favicon.png"
    };
    
    for (const char* path : iconPaths) {
        int width, height, channels;
        unsigned char* pixels = stbi_load(path, &width, &height, &channels, 4);
        
        if (pixels) {
            GLFWimage icon;
            icon.width = width;
            icon.height = height;
            icon.pixels = pixels;
            
            glfwSetWindowIcon(m_window, 1, &icon);
            stbi_image_free(pixels);
            
            std::cout << "Loaded window icon from: " << path << std::endl;
            return;
        }
    }
    
    std::cerr << "Warning: Could not load window icon" << std::endl;
}

void Window::enableDarkMode() {
#ifdef _WIN32
    HWND hwnd = glfwGetWin32Window(m_window);
    if (hwnd) {
        // Windows 10 Build 19041+ (20H1) dark mode support
        BOOL useDarkMode = TRUE;
        
        // Try Windows 11 method (DWMWA_USE_IMMERSIVE_DARK_MODE = 20)
        HRESULT hr = DwmSetWindowAttribute(hwnd, 20, &useDarkMode, sizeof(useDarkMode));
        
        // If that fails, try Windows 10 method (DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 = 19)
        if (FAILED(hr)) {
            DwmSetWindowAttribute(hwnd, 19, &useDarkMode, sizeof(useDarkMode));
        }
    }
#endif
}

} // namespace Forge
