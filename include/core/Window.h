#pragma once

#include <GLFW/glfw3.h>
#include <string>
#include <functional>

namespace Propulsion {

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    bool shouldClose() const;
    void setShouldClose(bool shouldClose);
    void pollEvents();
    
    GLFWwindow* getNativeWindow() const { return m_window; }
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }

    // OS display scale for the monitor the window is on (1.0 = 100%, 2.0 = 200%, ...)
    float getContentScale() const;

    void setResizeCallback(std::function<void(int, int)> callback);
    void setContentScaleCallback(std::function<void(float)> callback);
    void setKeyCallback(std::function<void(int, int, int, int)> callback);
    void setMouseButtonCallback(std::function<void(int, int, int)> callback);
    void setMouseMoveCallback(std::function<void(double, double)> callback);
    void setScrollCallback(std::function<void(double, double)> callback);
    void setCloseCallback(std::function<void()> callback);

private:
    void loadIcon();
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void windowCloseCallback(GLFWwindow* window);
    static void contentScaleCallback(GLFWwindow* window, float xscale, float yscale);

    void enableDarkMode();

    GLFWwindow* m_window;
    int m_width;
    int m_height;
    
    std::function<void(int, int)> m_resizeCallback;
    std::function<void(int, int, int, int)> m_keyCallback;
    std::function<void(int, int, int)> m_mouseButtonCallback;
    std::function<void(double, double)> m_mouseMoveCallback;
    std::function<void(double, double)> m_scrollCallback;
    std::function<void()> m_closeCallback;
    std::function<void(float)> m_contentScaleCallback;
};

} // namespace Propulsion
