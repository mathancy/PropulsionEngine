#include "platform/FileDialogs.h"
#include <filesystem>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>
#include <ShlObj.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

namespace ForgeEngine {
namespace Platform {

#ifdef _WIN32

// Callback to set initial directory for folder browser
static int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData) {
    if (uMsg == BFFM_INITIALIZED) {
        SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);
    }
    return 0;
}

std::string showSaveFolderDialog(GLFWwindow* window, const std::string& suggestedName) {
    HWND hwndOwner = glfwGetWin32Window(window);
    
    // First, show a simple message box for project name
    std::string projectName = suggestedName.empty() ? "NewProject" : suggestedName;
    
    // For simplicity, use the folder browser with project name in title
    // (A full input dialog would require more complex Win32 code)
    
    // Get default Projects directory
    char documentsPath[MAX_PATH];
    std::string defaultDir;
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, documentsPath))) {
        defaultDir = std::string(documentsPath) + "\\Forgeworks Interactive\\Forge Engine\\Projects";
        std::filesystem::create_directories(defaultDir);
    }
    
    std::string dialogTitle = "Project will be named: \"" + projectName + "\" - Select parent folder";
    
    // Use folder browser dialog with default directory
    BROWSEINFOA bi = { 0 };
    bi.hwndOwner = hwndOwner;
    bi.lpszTitle = dialogTitle.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = BrowseCallbackProc;
    bi.lParam = (LPARAM)defaultDir.c_str();
    
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != 0) {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path)) {
            CoTaskMemFree(pidl);
            
            // Return the full path: selected folder + project name
            return std::string(path) + "\\" + projectName;
        }
        CoTaskMemFree(pidl);
    }
    
    return ""; // User cancelled
}

std::string showOpenFileDialog(GLFWwindow* window) {
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = glfwGetWin32Window(window);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Forge Project Files\0*.fproj\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    
    // Set default directory to Documents/Forgeworks Interactive/Forge Engine/Projects
    char documentsPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, documentsPath))) {
        std::string defaultDir = std::string(documentsPath) + "\\Forgeworks Interactive\\Forge Engine\\Projects";
        ofn.lpstrInitialDir = defaultDir.c_str();
    }
    
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    
    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(ofn.lpstrFile);
    }
    
    return ""; // User cancelled
}

std::vector<std::string> showImportFileDialog(GLFWwindow* window) {
    OPENFILENAMEA ofn;
    // Use a larger buffer for multiple file selection
    char szFile[8192] = { 0 };
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = glfwGetWin32Window(window);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    
    // Filter for various importable file types
    ofn.lpstrFilter = 
        "All Supported Files\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.hdr;*.fbx;*.obj;*.gltf;*.glb;*.mp4;*.avi;*.mov;*.mkv;*.wav;*.mp3;*.ogg\0"
        "Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.hdr\0"
        "3D Model Files\0*.fbx;*.obj;*.gltf;*.glb\0"
        "Video Files\0*.mp4;*.avi;*.mov;*.mkv\0"
        "Audio Files\0*.wav;*.mp3;*.ogg\0"
        "All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.lpstrTitle = "Import Files";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    
    std::vector<std::string> result;
    
    if (GetOpenFileNameA(&ofn) == TRUE) {
        // When multiple files are selected with OFN_ALLOWMULTISELECT:
        // The buffer contains: directory\0file1\0file2\0file3\0\0
        // When single file is selected: fullpath\0\0
        
        char* p = szFile;
        std::string directory = p;
        p += directory.length() + 1;  // Move past directory + null
        
        if (*p == '\0') {
            // Single file selected - directory IS the full path
            result.push_back(directory);
        } else {
            // Multiple files selected
            while (*p != '\0') {
                std::string filename = p;
                result.push_back(directory + "\\" + filename);
                p += filename.length() + 1;
            }
        }
    }
    
    return result;
}

std::string getDefaultProjectsDirectory() {
    char documentsPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, documentsPath))) {
        std::string defaultDir = std::string(documentsPath) + "\\Forgeworks Interactive\\Forge Engine\\Projects";
        std::filesystem::create_directories(defaultDir);
        return defaultDir;
    }
    return "";
}

std::string showNewProjectFolderDialog(GLFWwindow* window, const std::string& projectName) {
    HWND hwndOwner = glfwGetWin32Window(window);
    
    std::string parentDir = getDefaultProjectsDirectory();
    std::string dialogTitle = "Select folder for project: " + projectName;
    
    BROWSEINFOA bi = { 0 };
    bi.hwndOwner = hwndOwner;
    bi.lpszTitle = dialogTitle.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = BrowseCallbackProc;
    bi.lParam = (LPARAM)parentDir.c_str();
    
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != 0) {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path)) {
            CoTaskMemFree(pidl);
            return std::string(path);
        }
        CoTaskMemFree(pidl);
    }
    
    return ""; // User cancelled
}
#else
// Stub implementations for non-Windows platforms
std::string showSaveFolderDialog(GLFWwindow* window, const std::string& suggestedName) {
    return "";
}

std::string showOpenFileDialog(GLFWwindow* window) {
    return "";
}

std::vector<std::string> showImportFileDialog(GLFWwindow* window) {
    return {};
}

std::string showNewProjectFolderDialog(GLFWwindow* window, const std::string& projectName) {
    return "";
}

std::string getDefaultProjectsDirectory() {
    return "";
}
#endif

bool openWithDefaultApp(const std::string& path) {
#ifdef _WIN32
    // ShellExecute with the "open" verb: the editor the user has chosen for this kind of file.
    // For a type with no editor chosen (like .forge), Windows asks which program to use.
    HINSTANCE result = ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
#else
    (void)path;
    return false;
#endif
}

} // namespace Platform
} // namespace ForgeEngine
