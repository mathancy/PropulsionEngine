// ForgeEngine Platform-Specific File Dialogs
// Windows implementation of native file/folder dialogs

#pragma once

#include <string>
#include <vector>

// Forward declaration to avoid GLFW header dependency
struct GLFWwindow;

namespace ForgeEngine {
namespace Platform {

/**
 * @brief Shows a folder browser dialog for saving/creating a new project
 * @param window The parent GLFW window
 * @param projectName The name of the project to create
 * @return The selected folder path, or empty string if cancelled
 */
std::string showNewProjectFolderDialog(GLFWwindow* window, const std::string& projectName);

/**
 * @brief Shows an "Open File" dialog for .fproj project files
 * @param window The parent GLFW window
 * @return The selected file path, or empty string if cancelled
 */
std::string showOpenFileDialog(GLFWwindow* window);

/**
 * @brief Shows a multi-select file dialog for importing assets
 * @param window The parent GLFW window
 * @return Vector of selected file paths, or empty vector if cancelled
 */
std::vector<std::string> showImportFileDialog(GLFWwindow* window);

/**
 * @brief Shows a folder browser dialog for selecting an existing project folder
 * @param window The parent GLFW window
 * @return The selected folder path, or empty string if cancelled
 */
std::string showProjectFolderDialog(GLFWwindow* window);

/**
 * @brief Opens a file in the program the user has chosen for that file type
 * @param path The file to open
 * @return true if the system accepted the request
 */
bool openWithDefaultApp(const std::string& path);

} // namespace Platform
} // namespace ForgeEngine
