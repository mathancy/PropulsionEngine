// The Material editor's Surface settings: solid, cut out (foliage) or see-through (glass). See core/MaterialSettings.h.

#include "Engine.h"
#include "ui/ImGuiTheme.h"

#include <imgui.h>

namespace Propulsion {

bool Engine::drawMaterialSurfaceSettings(ContentItem& item) {
    MaterialSettings& s = item.materialSettings;
    bool changed = false;

    const float labelWidth = PropulsionMKI::UI::scaled(110.0f);
    const float fieldWidth = PropulsionMKI::UI::scaled(150.0f);
    auto row = [&](const char* label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(fieldWidth);
    };

    ImGui::PushID("MaterialSurface");
    row("Blend Mode");
    if (ImGui::BeginCombo("##BlendMode", blendModeName(s.blendMode))) {
        for (int i = 0; i < kBlendModeCount; i++) {
            const BlendMode mode = blendModeFromInt(i);
            if (ImGui::Selectable(blendModeName(mode), mode == s.blendMode) && mode != s.blendMode) {
                s.blendMode = mode;
                // Foliage is seen from both sides; glass sheets too. Start there, the box below undoes it.
                if (mode == BlendMode::Masked) s.twoSided = true;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        PropulsionMKI::UI::tooltip("Opaque: solid.\nMasked: each point is there or not, by the alpha of the Opacity Mask texture (or of the "
                                 "Base Color texture): leaves, fences, lace.\nTranslucent: light passes through: glass, water, thin plastic.");
    }

    if (s.blendMode == BlendMode::Masked) {
        row("Cutoff");
        changed |= ImGui::SliderFloat("##Cutoff", &s.maskCutoff, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("Where the alpha is below this, the surface isn't there: it isn't drawn, casts no shadow and can be seen through.");
    }
    if (s.blendMode == BlendMode::Translucent) {
        row("Opacity");
        changed |= ImGui::SliderFloat("##Opacity", &s.opacity, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("How much is stopped: 1 is solid, 0 is invisible. Multiplied by the Opacity texture, if there is one.");
        row("Refraction");
        changed |= ImGui::SliderFloat("##Ior", &s.ior, MaterialSettings::kMinIor, 2.5f, "%.2f");
        if (ImGui::IsItemHovered()) {
            PropulsionMKI::UI::tooltip("Index of refraction: how much light bends going in, and how mirror-like the surface is at a glancing "
                                     "angle. Air 1.0, water 1.33, glass 1.5, diamond 2.4. Only the Path Tracer bends light; the other "
                                     "modes show the glancing reflection and see-through.");
        }
        changed |= ImGui::Checkbox("Thin Walled", &s.thinWalled);
        if (ImGui::IsItemHovered()) {
            PropulsionMKI::UI::tooltip("A sheet with no inside, like a window pane or a leaf: light goes straight through instead of bending. "
                                     "Leave off for solid glass (a bottle, a lens).");
        }
        if (!s.thinWalled) {
            row("Tint Distance");
            changed |= ImGui::DragFloat("##Absorption", &s.absorptionDistance, 0.01f, MaterialSettings::kMinDistance,
                                        MaterialSettings::kMaxDistance, "%.2f", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered()) {
                PropulsionMKI::UI::tooltip("Light is tinted by the Base Color after travelling this far inside (Path Tracer). Thicker glass is "
                                         "more tinted; a larger distance is clearer.");
            }
        }
    }
    if (s.blendMode != BlendMode::Opaque) {
        changed |= ImGui::Checkbox("Two Sided", &s.twoSided);
        if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("Draw and light the back of surfaces too (leaves, glass sheets). Off, the back is invisible.");
    }
    ImGui::PopID();

    if (changed) s.sanitize();
    return changed;
}

} // namespace Propulsion
