cmake_minimum_required(VERSION 3.20)
project(ForgeEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add /FS flag for MSVC to allow parallel PDB writes
if(MSVC)
    add_compile_options(/FS)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Force single output directory for all configurations (no Debug/Release subdirs)
foreach(CONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG} CONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${PROJECT_SOURCE_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_BINARY_DIR}/lib)
endforeach()

# Source files
file(GLOB_RECURSE SOURCES 
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/*.c"
)

# Shader_new.cpp is the current Shader implementation (dynamic uniform buffers, resetObjectIndex).
# The older Shader.cpp defines the same class and is kept only for reference, so it isn't built.
list(FILTER SOURCES EXCLUDE REGEX "/src/rendering/Shader\\.cpp$")

file(GLOB_RECURSE HEADERS
    "${PROJECT_SOURCE_DIR}/include/*.h"
    "${PROJECT_SOURCE_DIR}/include/*.hpp"
)

# Find Vulkan SDK
find_package(Vulkan REQUIRED)

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/external/glfw/include
    ${PROJECT_SOURCE_DIR}/external/glm
    ${PROJECT_SOURCE_DIR}/external/imgui
    ${PROJECT_SOURCE_DIR}/external/imgui/backends
    ${PROJECT_SOURCE_DIR}/external/stb
    ${PROJECT_SOURCE_DIR}/external/lunasvg/include
    ${Vulkan_INCLUDE_DIRS}
)

# GLFW (with Vulkan support)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_VULKAN_STATIC OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

# Dear ImGui
set(IMGUI_DIR ${PROJECT_SOURCE_DIR}/external/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC 
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${PROJECT_SOURCE_DIR}/external/glfw/include
    ${Vulkan_INCLUDE_DIRS}
)
target_link_libraries(imgui ${Vulkan_LIBRARIES})

# LunaSVG
set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(external/lunasvg)

# ForgeScript: scripts (.forge files in a project's Scripts folder) are run by the ForgeScript
# interpreter, a DLL the editor loads. Scripts are read when Play starts, so changing one needs no
# rebuild. Build the DLL once with `cargo build --release -p forge-ffi` in the ForgeScript checkout
# (next to this folder), or point FORGESCRIPT_DIR at it.
set(FORGESCRIPT_DIR "${PROJECT_SOURCE_DIR}/../ForgeScript" CACHE PATH "The ForgeScript checkout")
find_file(FORGE_FFI_IMPLIB forge_ffi.dll.lib
    HINTS ${FORGESCRIPT_DIR}/target/release ${FORGESCRIPT_DIR}/target/debug NO_DEFAULT_PATH)
find_file(FORGE_FFI_DLL forge_ffi.dll
    HINTS ${FORGESCRIPT_DIR}/target/release ${FORGESCRIPT_DIR}/target/debug NO_DEFAULT_PATH)
if(NOT FORGE_FFI_IMPLIB OR NOT FORGE_FFI_DLL OR NOT EXISTS ${FORGESCRIPT_DIR}/forge-ffi/include/forge_ffi.h)
    message(FATAL_ERROR "The ForgeScript interpreter library was not found in ${FORGESCRIPT_DIR}. "
                        "Run `cargo build --release -p forge-ffi` there, or pass -DFORGESCRIPT_DIR=<path>.")
endif()
message(STATUS "ForgeScript interpreter: ${FORGE_FFI_DLL}")
include_directories(${FORGESCRIPT_DIR}/forge-ffi/include)

# Windows icon resource
if(WIN32)
    set(ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/icon.rc")
    file(WRITE ${ICON_RC} "IDI_ICON1 ICON \"${CMAKE_SOURCE_DIR}/resources/icons/favicon.ico\"\n")
    list(APPEND SOURCES ${ICON_RC})
endif()

# Main executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})
target_link_libraries(${PROJECT_NAME} ${FORGE_FFI_IMPLIB})
# The DLL sits next to the executable so it is found at startup.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FORGE_FFI_DLL} $<TARGET_FILE_DIR:${PROJECT_NAME}>
)

# Link libraries
target_link_libraries(${PROJECT_NAME} 
    glfw
    imgui
    lunasvg
    ${Vulkan_LIBRARIES}
)

# Copy assets to build directory (if exists)
if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
    )
endif()

# Tests for the script graph editor (headless: a real ImGui context, no window or GPU).
# Not part of the normal build:  cmake --build . --target ScriptGraphTest
# Then run:  <build>/tests/ScriptGraphTest.exe <path to the ForgeScript checkout>
add_executable(ScriptGraphTest EXCLUDE_FROM_ALL
    tests/script_graph_test.cpp
    src/scripting/ScriptGraphEditor.cpp
    src/scripting/ScriptGraphPanels.cpp
    src/scripting/CodeBuffer.cpp
    src/nodegraph/NodeGraph.cpp
    src/nodegraph/NodeGraphClipboard.cpp
    src/nodegraph/NodeGraphSelection.cpp
    src/nodegraph/ConnectionRenderer.cpp
    src/nodegraph/ScriptNodeRenderer.cpp
    src/nodegraph/ColorWheelRenderer.cpp
    src/nodegraph/ImageNodeRenderer.cpp
    src/nodes/ColorWheelNode.cpp
    src/nodes/ImageNode.cpp
    src/nodes/MaterialOutputNode.cpp
    src/platform/FileDialogs.cpp
    src/ui/ImGuiTheme.cpp
)
target_link_libraries(ScriptGraphTest imgui glfw ${FORGE_FFI_IMPLIB} comdlg32 shell32 ole32 user32 gdi32)
set_target_properties(ScriptGraphTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)
add_custom_command(TARGET ScriptGraphTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FORGE_FFI_DLL} $<TARGET_FILE_DIR:ScriptGraphTest>
)
