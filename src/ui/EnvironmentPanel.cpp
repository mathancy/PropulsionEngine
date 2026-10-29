// The scene's lights and sky: loading an HDRI, keeping the lighting block up to date each frame, and the Environment
// controls shown in the Objects panel. (What the GPU does with them is rendering/SceneLighting.cpp; the math is
// rendering/EnvironmentMap.h and LightingData.h, which have tests.)

#include "Engine.h"
#include "core/CrashHandler.h"
#include "platform/FileDialogs.h"
#include "ui/ImGuiTheme.h"
#include "stb_image.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>

namespace Propulsion {

namespace fs = std::filesystem;

// An HDRI wider than this is refused: decoding it needs a few hundred megabytes, and 8k is as sharp as a sky can use.
static constexpr long long kMaxHdriPixels = 8192LL * 4096LL;

Engine::EnvironmentLoad Engine::loadEnvironment(const std::string& hdriPath, const std::string& fullPath) {
    EnvironmentLoad result;
    result.hdriPath = hdriPath;

    if (hdriPath.empty()) {
        result.prepared = Environment::prepare(Environment::makeGradientSky(256));
        result.ok = true;
        return result;
    }

    int width = 0, height = 0, channels = 0;
    if (!stbi_info(fullPath.c_str(), &width, &height, &channels) || width <= 0 || height <= 0) {
        result.error = "Couldn't read " + fs::path(fullPath).filename().string() + " as an image. Is the file there, and a Radiance .hdr?";
        return result;
    }
    if (static_cast<long long>(width) * static_cast<long long>(height) > kMaxHdriPixels) {
        result.error = fs::path(fullPath).filename().string() + " is " + std::to_string(width) + "x" + std::to_string(height) +
                       ": too big. Use an HDRI of 8192x4096 or smaller.";
        return result;
    }

    float* pixels = stbi_loadf(fullPath.c_str(), &width, &height, &channels, 3);
    if (!pixels) {
        result.error = "Couldn't decode " + fs::path(fullPath).filename().string() + ".";
        return result;
    }

    const auto started = std::chrono::steady_clock::now();
    Environment::Image image(width, height);
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
    for (size_t i = 0; i < count; i++) {
        const float v = pixels[i];
        // A bad pixel (NaN, negative) is black; the very brightest are held to what a half float can hold.
        image.rgb[i] = v >= 0.0f ? (std::min)(v, 65000.0f) : 0.0f;
    }
    stbi_image_free(pixels);

    result.prepared = Environment::prepare(image);
    result.ok = true;
    std::ostringstream timing;
    timing << width << "x" << height << " prepared in "
           << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count() << " ms";
    result.info = timing.str();
    return result;
}

void Engine::startEnvironmentLoad() {
    const std::string path = m_environment.hdriPath;
    std::string fullPath;
    if (!path.empty()) {
        if (m_currentProjectPath.empty()) {
            m_failedHdriPath = path;
            m_environmentStatus = "Save the project first: an HDRI is found relative to it.";
            return;
        }
        fullPath = (fs::path(m_currentProjectPath).parent_path() / fs::path(path)).string();
    }
    m_environmentStatus = path.empty() ? "" : "Loading...";
    m_environmentLoad = std::async(std::launch::async, &Engine::loadEnvironment, path, fullPath);
}

void Engine::updateEnvironment() {
    if (!m_lighting) return;
    m_environment.sanitize();

    if (m_environmentLoad.valid() && m_environmentLoad.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        EnvironmentLoad loaded;
        try {
            loaded = m_environmentLoad.get();
        } catch (const std::exception& e) {
            // (Running out of memory on a huge HDRI, say.) It fails like any other bad file, and the editor carries on.
            loaded.hdriPath = m_environment.hdriPath;
            loaded.error = std::string("Couldn't prepare the HDRI: ") + e.what();
        }
        // (If the scene has moved on to another sky while this one was loading, it is no use: the next one starts below.)
        if (loaded.hdriPath == m_environment.hdriPath) {
            if (loaded.ok) {
                m_lighting->setEnvironment(loaded.prepared);
                m_environmentSmall = std::move(loaded.prepared.diffuseSource);
                m_environmentShDirty = true;
                m_loadedHdriPath = loaded.hdriPath;
                m_failedHdriPath.clear();
                m_environmentStatus.clear();
                if (!loaded.hdriPath.empty()) CrashHandler::logInfo("Environment loaded: " + loaded.hdriPath + " (" + loaded.info + ")");
            } else {
                m_failedHdriPath = loaded.hdriPath;
                m_environmentStatus = loaded.error;
                OutputLog::error(LogCategory::Render, loaded.error);
                CrashHandler::logError("Environment: " + loaded.error);
            }
        }
    }

    if (!m_environmentLoad.valid() && m_environment.hdriPath != m_loadedHdriPath && m_environment.hdriPath != m_failedHdriPath) {
        startEnvironmentLoad();
    }
}

void Engine::updateLightingBlocks(uint32_t frame) {
    if (!m_lighting) return;

    // The sky's diffuse lighting depends on how the sky is turned.
    const float yaw = glm::radians(m_environment.rotationDegrees);
    if (m_environmentShDirty || yaw != m_environmentShYaw) {
        m_environmentSh = Environment::projectDiffuseSh(m_environmentSmall, yaw);
        m_environmentShYaw = yaw;
        m_environmentShDirty = false;
    }

    EnvironmentLighting env;
    env.intensity = m_environment.intensity;
    env.yawRadians = yaw;
    env.maxLod = m_lighting->maxLod();
    env.sh = m_environmentSh;

    // The level is lit by its own lights (or the default rig when it has none). Lights that cast shadows are given maps.
    ShadowAssignment assignment;
    int dropped = 0;
    const bool hybrid = activeRenderMode() == RenderMode::HybridRT;
    GpuLighting scene = buildLighting(collectSceneLights(), env, true, &dropped, (m_environment.shadows && !hybrid) ? &assignment : nullptr,
                                      m_environment.shadows && hybrid);

    // What casts shadows, and where the camera is looking (the sun's map is fitted around it, out to the shadow distance).
    m_shadowCasters.clear();
    Shadows::Aabb casterBounds;
    if (m_environment.shadows && !hybrid) {
        for (const SceneObject& obj : m_sceneObjects) {
            if (!obj.visible) continue;
            Mesh* mesh = getMeshForObject(obj);
            if (!mesh) continue;
            glm::mat4 model = glm::translate(glm::mat4(1.0f), obj.position) * glm::mat4_cast(obj.quaternion);
            model = glm::scale(model, obj.scale);
            const Shadows::Aabb box = Shadows::Aabb::transformed(mesh->getBoundsMin(), mesh->getBoundsMax(), model);
            ShadowRenderer::Caster caster;
            caster.mesh = mesh;
            caster.model = model;
            caster.center = (box.min + box.max) * 0.5f;
            caster.radius = glm::length(box.max - box.min) * 0.5f;
            casterBounds.expand(box);
            m_shadowCasters.push_back(caster);
        }
    }
    const float aspect = (m_viewportSize.x > 0 && m_viewportSize.y > 0) ? m_viewportSize.x / m_viewportSize.y : 1.0f;
    m_shadowFrame = applyShadowFrame(scene, assignment, m_camera->getViewMatrix(), m_camera->getProjectionMatrix(aspect), 0.1f,
                                     m_environment.shadowDistance, casterBounds);
    applyRayTracedSettings(scene, m_environment.rtShadowRays, m_environment.rtAoRays, m_environment.aoRadius,
                           m_environment.ambientOcclusion ? m_environment.aoStrength : 0.0f);
    m_lighting->update(frame, SceneLighting::Slot::Scene, scene);

    // The scene is shaded with the ambient occlusion made this frame (or none).
    const bool occlusion = m_ambientOcclusion && m_environment.ambientOcclusion && activeRenderMode() == RenderMode::Raster;
    m_lighting->setSceneAmbientOcclusion(frame, occlusion ? m_ambientOcclusion->getResultView() : VK_NULL_HANDLE,
                                         occlusion ? m_ambientOcclusion->getSampler() : VK_NULL_HANDLE);
    if (dropped > 0 && !m_warnedTooManyLights) {
        m_warnedTooManyLights = true;
        OutputLog::warning(LogCategory::Render, "The scene has more than " + std::to_string(kMaxLights) + " lights: the extra " + std::to_string(dropped) +
                                  " are ignored.");
    } else if (dropped == 0) {
        m_warnedTooManyLights = false;
    }

    // Previews (the Object and Material editors) are lit by the default lights, in the same sky.
    m_lighting->update(frame, SceneLighting::Slot::Preview, buildLighting({}, env, true));
}

VkDescriptorSet Engine::hdriThumbnail(const std::string& fullPath) {
    auto cached = m_hdriThumbnails.find(fullPath);
    if (cached != m_hdriThumbnails.end()) return cached->second;
    const VkDescriptorSet thumbnail = generateTextureThumbnail(fullPath);
    if (thumbnail != VK_NULL_HANDLE) m_hdriThumbnails[fullPath] = thumbnail;
    return thumbnail;
}

void Engine::drawLightIcon(VkCommandBuffer commandBuffer, const LightSettings& light, const glm::mat4& place, const glm::mat4& view,
                           const glm::mat4& projection, const glm::vec3& cameraPos, uint32_t frame, uint32_t& drawIndex) {
    if (!m_sphere) return;
    const glm::vec3 sphereExtents = m_sphere->getBoundsExtents();
    const float toUnitSphere = 1.0f / (std::max)(0.0001f, (std::max)(sphereExtents.x, (std::max)(sphereExtents.y, sphereExtents.z)));

    // The picture glows in the light's own color (dark base, so the lights in the scene hardly change it).
    auto draw = [&](Mesh* mesh, const glm::mat4& model) {
        if (!mesh || drawIndex >= Shader::MAX_OBJECTS) return;
        UniformBufferObject ubo = {};
        ubo.model = model;
        ubo.view = view;
        ubo.projection = projection;
        ubo.colorAndExposure = glm::vec4(0.02f, 0.02f, 0.02f, m_exposure);
        ubo.materialParams = glm::vec4(0.0f, 0.6f, 1.0f, 0.0f);
        ubo.textureFlags = glm::vec4(0.0f);
        ubo.textureFlags2 = glm::vec4(0.0f);
        ubo.invertFlags = glm::vec4(0.0f);
        ubo.textureStrengths = glm::vec4(1.0f);
        ubo.emissiveColor = glm::vec4(light.color * 3.0f, 1.0f);
        ubo.heightParams = glm::vec4(0.0f);
        ubo.cameraPos = cameraPos;
        m_offscreenShader->updateUniformBufferDynamic(frame, ubo);
        m_offscreenShader->bindWithDynamicOffset(commandBuffer, frame, drawIndex);
        mesh->draw(commandBuffer);
        drawIndex++;
    };

    const bool sun = light.type == LightSettings::Type::Directional;
    draw(m_sphere.get(), glm::scale(place, glm::vec3((sun ? 0.3f : 0.2f) * toUnitSphere)));
    if (sun && m_gizmoCone) {
        // The cone points along +Y, the way the light shines: it is 1.2 long, starting 0.3 out.
        draw(m_gizmoCone.get(), glm::scale(glm::translate(place, glm::vec3(0.0f, 0.9f, 0.0f)), glm::vec3(0.3f, 1.2f, 0.3f)));
    }
}

void Engine::refreshProjectHdris() {
    m_projectHdris.clear();
    if (m_currentProjectPath.empty()) return;
    const fs::path projectDir = fs::path(m_currentProjectPath).parent_path();
    const fs::path assets = projectDir / "Assets";
    std::error_code ec;
    if (!fs::exists(assets, ec)) return;
    for (fs::recursive_directory_iterator it(assets, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string extension = it->path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        if (extension != ".hdr") continue;
        const fs::path relative = fs::relative(it->path(), projectDir, ec);
        if (!ec) m_projectHdris.push_back(relative.generic_string());
    }
    std::sort(m_projectHdris.begin(), m_projectHdris.end());
}

bool Engine::drawRenderModeSelector() {
    // Only the modes that exist so far. A mode this GPU can't do is listed but can't be picked.
    constexpr int kListedModes = 2;
    const bool canTrace = m_rayTracing && m_rayTracing->available();
    const RenderMode shown = activeRenderMode();

    const float width = 140.0f;
    ImGui::SetCursorScreenPos(ImVec2(m_viewportPos.x + m_viewportSize.x - width - 12.0f, m_viewportPos.y + 12.0f));
    ImGui::SetNextItemWidth(width);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.05f, 0.05f, 0.06f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.15f, 0.15f, 0.17f, 0.85f));
    if (ImGui::BeginCombo("##RenderMode", renderModeName(shown))) {
        for (int i = 0; i < kListedModes; i++) {
            const RenderMode mode = renderModeFromInt(i);
            const bool available = renderModeAvailable(mode, canTrace);
            if (!available) ImGui::BeginDisabled();
            if (ImGui::Selectable(renderModeName(mode), mode == shown) && mode != m_environment.renderMode) {
                m_environment.renderMode = mode;
                markProjectModified();
            }
            if (!available) ImGui::EndDisabled();
            if (!available && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("This GPU can't trace rays.");
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleColor(2);
    const bool onIt = ImGui::IsItemHovered();
    if (onIt) {
        ImGui::SetTooltip("How the viewport is drawn.\nRaster: shadow maps and screen-space occlusion.\nHybrid RT: the same picture with ray-traced shadows and occlusion.");
    }
    return onIt;
}

void Engine::useHdriAsEnvironment(const ContentItem& item) {
    if (m_currentProjectPath.empty()) {
        OutputLog::error(LogCategory::Render, "Save the project first: an HDRI is found relative to it.");
        return;
    }
    const fs::path projectDir = fs::path(m_currentProjectPath).parent_path();
    fs::path file(item.filepath);
    if (file.is_relative()) file = projectDir / file;
    std::error_code ec;
    const fs::path relative = fs::relative(file, projectDir, ec);
    if (ec || relative.empty() || *relative.begin() == "..") {
        OutputLog::error(LogCategory::Render, item.name + " isn't inside the project folder, so it can't be the environment.");
        return;
    }
    m_environment.hdriPath = relative.generic_string();
    m_failedHdriPath.clear();
    markProjectModified();
    OutputLog::info(LogCategory::Render, "Environment: " + item.name);
}

void Engine::renderEnvironmentSection() {
    if (!ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) return;

    bool changed = false;

    // Each row is a label on the left and a control that fills the rest (the Objects panel can be narrow).
    auto row = [](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        ImGui::SameLine(PropulsionMKI::UI::scaled(92.0f));
        ImGui::SetNextItemWidth(-1.0f);
    };

    // The sky: the default gradient, or one of the project's HDRIs.
    const std::string shown = m_environment.hdriPath.empty() ? std::string("Default Sky") : fs::path(m_environment.hdriPath).filename().string();
    static bool pickerWasOpen = false;
    row("Sky");
    const bool pickerOpen = ImGui::BeginCombo("##EnvironmentSky", shown.c_str());
    if (pickerOpen) {
        if (!pickerWasOpen) refreshProjectHdris();  // look again each time the list is opened
        if (ImGui::Selectable("Default Sky", m_environment.hdriPath.empty())) {
            m_environment.hdriPath.clear();
            changed = true;
        }
        for (const std::string& path : m_projectHdris) {
            if (ImGui::Selectable(fs::path(path).filename().string().c_str(), path == m_environment.hdriPath)) {
                m_environment.hdriPath = path;
                m_failedHdriPath.clear();
                changed = true;
            }
        }
        if (m_projectHdris.empty()) ImGui::TextDisabled("No HDRIs in the project yet.");
        ImGui::EndCombo();
    }
    pickerWasOpen = pickerOpen;

    if (ImGui::SmallButton("Import HDRI...")) {
        const std::vector<std::string> paths = PropulsionMKI::Platform::showImportFileDialog(m_window->getNativeWindow());
        for (const std::string& path : paths) {
            std::string extension = fs::path(path).extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
            if (extension != ".hdr") {
                OutputLog::error(LogCategory::Render, fs::path(path).filename().string() + " isn't an HDRI (.hdr).");
                continue;
            }
            const size_t before = m_contentItems.size();
            importFile(path);
            if (m_contentItems.size() > before && m_contentItems.back().type == "HDRI") useHdriAsEnvironment(m_contentItems.back());
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Copy a Radiance .hdr panorama (the free ones on Polyhaven work well) into the project and use it as the sky. "
                          "You can also drop one into the Content Browser by importing it there, and double-click it to use it.");
    }
    if (!m_environment.hdriPath.empty()) {
        if (ImGui::SmallButton("Reload")) {
            m_loadedHdriPath = "\n";  // not what the scene asks for, so it is read again
            m_failedHdriPath.clear();
        }
    }
    if (!m_environmentStatus.empty()) {
        const bool loading = m_environmentStatus == "Loading...";
        ImGui::PushTextWrapPos(0.0f);
        if (loading) ImGui::TextDisabled("%s", m_environmentStatus.c_str());
        else ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "%s", m_environmentStatus.c_str());
        ImGui::PopTextWrapPos();
    }

    row("Intensity");
    changed |= ImGui::DragFloat("##EnvironmentIntensity", &m_environment.intensity, 0.01f, 0.0f, EnvironmentSettings::kMaxIntensity, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How bright the sky is, both as light and as the background. 1 is the HDRI as it was shot.");
    row("Rotation");
    changed |= ImGui::SliderFloat("##EnvironmentRotation", &m_environment.rotationDegrees, -180.0f, 180.0f, "%.0f deg");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turns the sky about the vertical (Z) axis. The middle of the HDRI looks along +Y at 0.");
    changed |= ImGui::Checkbox("Sky Background", &m_environment.showBackground);

    changed |= ImGui::Checkbox("Shadows", &m_environment.shadows);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shadows from the sun and from the first few point lights that have Cast Shadows on. Their softness comes from "
                          "each light's size (Angular Diameter for the sun, Radius for a bulb): sharp where things touch, softer farther away.");
    }
    if (m_environment.shadows) {
        row("Distance");
        changed |= ImGui::DragFloat("##ShadowDistance", &m_environment.shadowDistance, 0.5f, EnvironmentSettings::kMinShadowDistance,
                                    EnvironmentSettings::kMaxShadowDistance, "%.0f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How far from the camera the sun's shadows reach. The sun's shadow is drawn in four layers: the closest is the "
                              "sharpest and covers least, so shadows get sharper as you move toward them. A shorter distance makes every layer sharper.");
        }
    }
    changed |= ImGui::Checkbox("Ambient Occlusion", &m_environment.ambientOcclusion);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Darkens the sky's light where nearby surfaces block it: in corners, and where things meet the ground.");
    if (m_environment.ambientOcclusion) {
        row("AO Radius");
        changed |= ImGui::DragFloat("##AoRadius", &m_environment.aoRadius, 0.01f, EnvironmentSettings::kMinAoRadius, EnvironmentSettings::kMaxAoRadius, "%.2f");
        row("AO Strength");
        changed |= ImGui::SliderFloat("##AoStrength", &m_environment.aoStrength, 0.0f, 1.0f, "%.2f");
    }

    if (changed) {
        m_environment.sanitize();
        markProjectModified();
    }

    // Ray tracing foundation (phase 2): what this GPU supports, and a developer view of primary rays through the scene.
    ImGui::Spacing();
    ImGui::SeparatorText("Ray Tracing");
    const RayTracingCaps& caps = m_vulkanContext->getRayTracingCaps();
    ImGui::PushTextWrapPos(0.0f);  // the panel is narrow
    ImGui::TextDisabled("%s", m_vulkanContext->getGpuName().c_str());
    ImGui::TextDisabled("%s", caps.canTracePipeline() ? "Ray queries and ray tracing pipelines: available"
                              : caps.canQuery()       ? "Ray queries: available (no ray tracing pipelines)"
                                                      : "Ray tracing is not available on this GPU");
    ImGui::PopTextWrapPos();
    if (!m_rayTracing) ImGui::BeginDisabled();
    row("Debug View");
    static const char* kDebugViews[] = {"Off", "Hit Normals", "Objects"};
    ImGui::Combo("##RayDebugView", &m_rayDebugView, kDebugViews, 3);
    if (!m_rayTracing) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Replaces the picture with what a ray from the camera hits in each pixel, found through the ray tracing "
                          "structures: the surface's normal as a colour, or a flat colour per object. It should match the normal view's outlines exactly.");
    }
    ImGui::Spacing();
}

} // namespace Propulsion
