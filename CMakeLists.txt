cmake_minimum_required(VERSION 3.20)
project(PropulsionMKI VERSION 1.0.0 LANGUAGES CXX)

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

# PropulsionScript: scripts (.prop files in a project's Scripts folder) are run by the PropulsionScript
# interpreter, a DLL the editor loads. Scripts are read when Play starts, so changing one needs no
# rebuild. Build the DLL once with `cargo build --release -p propulsion-ffi` in the PropulsionScript folder
# (inside this repo), or point PROPULSIONSCRIPT_DIR at it.
set(PROPULSIONSCRIPT_DIR "${PROJECT_SOURCE_DIR}/PropulsionScript" CACHE PATH "The PropulsionScript folder")
find_file(PROPULSION_FFI_IMPLIB propulsion_ffi.dll.lib
    HINTS ${PROPULSIONSCRIPT_DIR}/target/release ${PROPULSIONSCRIPT_DIR}/target/debug NO_DEFAULT_PATH)
find_file(PROPULSION_FFI_DLL propulsion_ffi.dll
    HINTS ${PROPULSIONSCRIPT_DIR}/target/release ${PROPULSIONSCRIPT_DIR}/target/debug NO_DEFAULT_PATH)
if(NOT PROPULSION_FFI_IMPLIB OR NOT PROPULSION_FFI_DLL OR NOT EXISTS ${PROPULSIONSCRIPT_DIR}/propulsion-ffi/include/propulsion_ffi.h)
    message(FATAL_ERROR "The PropulsionScript interpreter library was not found in ${PROPULSIONSCRIPT_DIR}. "
                        "Run `cargo build --release -p propulsion-ffi` there, or pass -DPROPULSIONSCRIPT_DIR=<path>.")
endif()
message(STATUS "PropulsionScript interpreter: ${PROPULSION_FFI_DLL}")
include_directories(${PROPULSIONSCRIPT_DIR}/propulsion-ffi/include)

# Windows icon resource
if(WIN32)
    set(ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/icon.rc")
    file(WRITE ${ICON_RC} "IDI_ICON1 ICON \"${CMAKE_SOURCE_DIR}/resources/icons/favicon.ico\"\n")
    list(APPEND SOURCES ${ICON_RC})
endif()

# Main executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})
target_link_libraries(${PROJECT_NAME} ${PROPULSION_FFI_IMPLIB})
# The DLL sits next to the executable so it is found at startup.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROPULSION_FFI_DLL} $<TARGET_FILE_DIR:${PROJECT_NAME}>
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
# Then run:  <build>/tests/ScriptGraphTest.exe <path to the PropulsionScript checkout>
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
target_link_libraries(ScriptGraphTest imgui glfw ${PROPULSION_FFI_IMPLIB} comdlg32 shell32 ole32 user32 gdi32)
set_target_properties(ScriptGraphTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)
add_custom_command(TARGET ScriptGraphTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROPULSION_FFI_DLL} $<TARGET_FILE_DIR:ScriptGraphTest>
)

# Tests for input as data: the key table, Input Actions and Mappings turning keys into values, their files (header only).
# Not part of the normal build:  cmake --build . --target InputMapTest
# Then run:  <build>/tests/InputMapTest.exe
add_executable(InputMapTest EXCLUDE_FROM_ALL tests/input_map_test.cpp)
target_link_libraries(InputMapTest glfw)
set_target_properties(InputMapTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)

# Tests for scripts and the physics together: traces, touches and hits, through the real interpreter (no window or GPU).
# Not part of the normal build:  cmake --build . --target PlayModeTest
# Then run:  <build>/tests/PlayModeTest.exe
add_executable(PlayModeTest EXCLUDE_FROM_ALL tests/play_mode_test.cpp src/scripting/PlayMode.cpp)
target_link_libraries(PlayModeTest glfw ${PROPULSION_FFI_IMPLIB})
# The test plays the repo's own behaviors/player.prop.
target_compile_definitions(PlayModeTest PRIVATE PROPULSION_BEHAVIORS_DIR="${PROPULSIONSCRIPT_DIR}/behaviors")
set_target_properties(PlayModeTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)
add_custom_command(TARGET PlayModeTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROPULSION_FFI_DLL} $<TARGET_FILE_DIR:PlayModeTest>
)

# Tests for the Object asset file format (header only: no window, GPU or ImGui).
# Not part of the normal build:  cmake --build . --target ObjectFileTest
# Then run:  <build>/tests/ObjectFileTest.exe
add_executable(ObjectFileTest EXCLUDE_FROM_ALL tests/object_file_test.cpp)
set_target_properties(ObjectFileTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)

# Tests for the camera component: the frustum outline, where a camera is in the world, and how Play points the
# viewport camera at it (no window or GPU).
# Not part of the normal build:  cmake --build . --target CameraFrustumTest
# Then run:  <build>/tests/CameraFrustumTest.exe
add_executable(CameraFrustumTest EXCLUDE_FROM_ALL tests/camera_frustum_test.cpp src/core/Camera.cpp)
set_target_properties(CameraFrustumTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)

# Tests for mouse look: how mouse movement becomes the lookX / lookY axes (header only).
# Not part of the normal build:  cmake --build . --target MouseLookTest
# Then run:  <build>/tests/MouseLookTest.exe
add_executable(MouseLookTest EXCLUDE_FROM_ALL tests/mouse_look_test.cpp)
set_target_properties(MouseLookTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)

# Tests for collision: shapes from mesh bounds, and the character mover (walk, fall, jump, slide) (header only).
# Not part of the normal build:  cmake --build . --target CollisionTest
# Then run:  <build>/tests/CollisionTest.exe
add_executable(CollisionTest EXCLUDE_FROM_ALL tests/collision_test.cpp)
set_target_properties(CollisionTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)

# Tests for the collision wireframe: View > Collision Shapes draws the same shapes the physics uses (header only).
# Not part of the normal build:  cmake --build . --target CollisionOutlineTest
# Then run:  <build>/tests/CollisionOutlineTest.exe
add_executable(CollisionOutlineTest EXCLUDE_FROM_ALL tests/collision_outline_test.cpp)
set_target_properties(CollisionOutlineTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)

# Tests for the spring arm in Play: the camera pulling in at walls, and the arm's lag (header only).
# Not part of the normal build:  cmake --build . --target SpringArmTest
# Then run:  <build>/tests/SpringArmTest.exe
add_executable(SpringArmTest EXCLUDE_FROM_ALL tests/spring_arm_test.cpp)
set_target_properties(SpringArmTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)

# Tests for the scene history: duplicating, creating and deleting objects, and undo and redo of them (header only).
# Not part of the normal build:  cmake --build . --target SceneHistoryTest
# Then run:  <build>/tests/SceneHistoryTest.exe
add_executable(SceneHistoryTest EXCLUDE_FROM_ALL tests/scene_history_test.cpp)
target_link_libraries(SceneHistoryTest glfw)
set_target_properties(SceneHistoryTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)

# Tests for asset IDs: a Mesh asset's ID is worked out from its path, so it is the same after a reload.
# Not part of the normal build:  cmake --build . --target AssetIdTest
# Then run:  <build>/tests/AssetIdTest.exe
add_executable(AssetIdTest EXCLUDE_FROM_ALL tests/asset_id_test.cpp src/utils/Utils.cpp)
set_target_properties(AssetIdTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)

# Tests for the Output Log's store: levels, categories, one entry per line, trimming, sinks, several threads logging
# at once, and capturing what is printed to stdout and stderr (no window or GPU).
# Not part of the normal build:  cmake --build . --target OutputLogTest
# Then run:  <build>/tests/OutputLogTest.exe
add_executable(OutputLogTest EXCLUDE_FROM_ALL tests/output_log_test.cpp src/core/OutputLog.cpp)
set_target_properties(OutputLogTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)

# Tests for the project file: asset-editor tabs remember their asset, older files still open.
# Not part of the normal build:  cmake --build . --target ProjectFileTest
# Then run:  <build>/tests/ProjectFileTest.exe tests/fixtures
add_executable(ProjectFileTest EXCLUDE_FROM_ALL tests/project_file_test.cpp src/project/ProjectSerializer.cpp src/core/Camera.cpp src/core/OutputLog.cpp src/utils/Utils.cpp)
set_target_properties(ProjectFileTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)

# Tests for lights and environments: where a direction lands in an HDRI, the diffuse lighting a sky gives, the
# reflection chain, half floats, and the block of lights the shader reads (header only: no window or GPU).
# Not part of the normal build:  cmake --build . --target LightingTest
# Then run:  <build>/tests/LightingTest.exe
add_executable(LightingTest EXCLUDE_FROM_ALL tests/lighting_test.cpp)
set_target_properties(LightingTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)

# Tests for shadows: where the sun's shadow map goes and that it holds still, a point light's six faces, and which
# lights are given shadows (header only: no window or GPU).
# Not part of the normal build:  cmake --build . --target ShadowTest
# Then run:  <build>/tests/ShadowTest.exe
add_executable(ShadowTest EXCLUDE_FROM_ALL tests/shadow_test.cpp)
set_target_properties(ShadowTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)

# Tests for material transparency settings and how they are kept at the end of a material file (header only: no window or GPU).
# Not part of the normal build:  cmake --build . --target MaterialSettingsTest
# Then run:  <build>/tests/MaterialSettingsTest.exe
add_executable(MaterialSettingsTest EXCLUDE_FROM_ALL tests/material_settings_test.cpp)
set_target_properties(MaterialSettingsTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)

# Tests for the ray tracing foundation's pure parts: what makes a GPU ray-tracing capable, which GPU is chosen, the
# transform layout for acceleration structures, and the instance table's layout (no window or GPU).
# Not part of the normal build:  cmake --build . --target RayTracingTest
# Then run:  <build>/tests/RayTracingTest.exe
add_executable(RayTracingTest EXCLUDE_FROM_ALL tests/ray_tracing_test.cpp)
set_target_properties(RayTracingTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests)
