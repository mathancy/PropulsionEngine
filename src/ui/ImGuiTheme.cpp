// PropulsionMKI UI Theme Implementation
// Provides theming functions for ImGui styling

#include "ui/ImGuiTheme.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdarg>
#include <algorithm>

namespace PropulsionMKI {
namespace UI {

namespace {
float s_uiScale = 1.0f;
ImGuiStyle s_baseStyle;       // Unscaled theme, captured on first setUIScale()
bool s_hasBaseStyle = false;
}

void setupFrostedGlassTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;
    
    // Frosted glass base colors with transparency
    ImVec4 glassBase = ImVec4(0.08f, 0.08f, 0.10f, 0.85f);          // Dark translucent base
    ImVec4 glassHighlight = ImVec4(0.12f, 0.12f, 0.15f, 0.90f);    // Lighter glass
    ImVec4 glassBorder = ImVec4(0.35f, 0.35f, 0.40f, 0.50f);       // Subtle borders
    ImVec4 glassAccent = ImVec4(1.0f, 0.42f, 0.00f, 0.80f);        // Orange accent (ember theme)
    ImVec4 glassAccentHover = ImVec4(1.0f, 0.50f, 0.15f, 0.90f);   // Brighter orange
    ImVec4 glassAccentActive = ImVec4(1.0f, 0.35f, 0.00f, 1.00f);  // Deep orange
    ImVec4 glassText = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);         // Almost white text
    ImVec4 glassTextDim = ImVec4(0.65f, 0.65f, 0.70f, 1.00f);      // Dimmed text
    
    // Suppress unused variable warnings (these are here for potential future use)
    (void)glassHighlight;
    (void)glassAccentHover;
    
    // Window styling
    colors[ImGuiCol_WindowBg] = glassBase;
    colors[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.70f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.12f, 0.95f);
    colors[ImGuiCol_Border] = glassBorder;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    
    // Frame backgrounds (inputs, etc)
    colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.10f, 0.12f, 0.80f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.15f, 0.18f, 0.85f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.18f, 0.22f, 0.90f);
    
    // Title bar
    colors[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.95f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.08f, 0.10f, 0.95f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.06f, 0.06f, 0.08f, 0.75f);
    
    // Menu bar
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.08f, 0.08f, 0.10f, 0.90f);
    
    // Scrollbar
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.07f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.25f, 0.30f, 0.80f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.35f, 0.40f, 0.85f);
    colors[ImGuiCol_ScrollbarGrabActive] = glassAccent;
    
    // Checkmark & slider
    colors[ImGuiCol_CheckMark] = glassAccent;
    colors[ImGuiCol_SliderGrab] = glassAccent;
    colors[ImGuiCol_SliderGrabActive] = glassAccentActive;
    
    // Buttons
    colors[ImGuiCol_Button] = ImVec4(0.15f, 0.15f, 0.18f, 0.80f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.20f, 0.25f, 0.90f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    
    // Headers (collapsing headers, tree nodes) - Orange for selections
    colors[ImGuiCol_Header] = ImVec4(1.0f, 0.42f, 0.00f, 0.60f);        // Orange selection
    colors[ImGuiCol_HeaderHovered] = ImVec4(1.0f, 0.50f, 0.15f, 0.70f); // Brighter orange hover
    colors[ImGuiCol_HeaderActive] = ImVec4(1.0f, 0.35f, 0.00f, 0.80f);  // Deep orange active
    
    // Separator
    colors[ImGuiCol_Separator] = glassBorder;
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.45f, 0.45f, 0.50f, 0.60f);
    colors[ImGuiCol_SeparatorActive] = glassAccent;
    
    // Resize grip
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.20f, 0.20f, 0.25f, 0.50f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.30f, 0.30f, 0.35f, 0.70f);
    colors[ImGuiCol_ResizeGripActive] = glassAccent;
    
    // Tabs - Orange for active/selected
    colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.10f, 0.12f, 0.85f);
    colors[ImGuiCol_TabHovered] = ImVec4(1.0f, 0.50f, 0.15f, 0.50f);    // Orange hover
    colors[ImGuiCol_TabActive] = ImVec4(1.0f, 0.42f, 0.00f, 0.80f);     // Orange active
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.08f, 0.08f, 0.10f, 0.80f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.65f, 0.27f, 0.00f, 0.60f); // Dimmed orange
    
    // Docking
    colors[ImGuiCol_DockingPreview] = ImVec4(glassAccent.x, glassAccent.y, glassAccent.z, 0.50f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.05f, 0.05f, 0.07f, 1.00f);
    
    // Table
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.12f, 0.12f, 0.15f, 0.90f);
    colors[ImGuiCol_TableBorderStrong] = glassBorder;
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.25f, 0.25f, 0.30f, 0.40f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
    
    // Text
    colors[ImGuiCol_Text] = glassText;
    colors[ImGuiCol_TextDisabled] = glassTextDim;
    colors[ImGuiCol_TextSelectedBg] = ImVec4(1.0f, 0.42f, 0.00f, 0.35f); // Orange text selection
    
    // Drag drop
    colors[ImGuiCol_DragDropTarget] = ImVec4(glassAccent.x, glassAccent.y, glassAccent.z, 0.70f);
    
    // Nav highlight
    colors[ImGuiCol_NavHighlight] = glassAccent;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    
    // Modal window dim
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);
    
    // Style parameters for modern look
    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    style.IndentSpacing = 22.0f;
    style.ScrollbarSize = 16.0f;
    style.GrabMinSize = 12.0f;
    
    // Rounding for frosted glass effect
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    
    // Borders
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    
    // Misc
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);  // Center title
    style.ColorButtonPosition = ImGuiDir_Left;
    style.AntiAliasedLines = true;
    style.AntiAliasedLinesUseTex = true;
    style.AntiAliasedFill = true;

    // A fresh theme is unscaled; drop any previously captured base so the next setUIScale() re-captures it
    s_hasBaseStyle = false;
}

void setUIScale(float scale) {
    scale = std::clamp(scale, 0.5f, 4.0f);
    ImGuiStyle& style = ImGui::GetStyle();

    if (!s_hasBaseStyle) {
        s_baseStyle = style;
        s_hasBaseStyle = true;
    }

    // Restore the unscaled theme, but keep ImGui's live font state (these are owned by the
    // font system / PushFont, not by the theme)
    const float fontSizeBase = style.FontSizeBase;
    const float fontScaleMain = style.FontScaleMain;
    const float nextFrameFontSizeBase = style._NextFrameFontSizeBase;

    style = s_baseStyle;
    style.FontSizeBase = fontSizeBase;
    style.FontScaleMain = fontScaleMain;
    style._NextFrameFontSizeBase = nextFrameFontSizeBase;

    style.ScaleAllSizes(scale);     // Padding, spacing, rounding, scrollbar, grab sizes, borders
    style.FontScaleDpi = scale;     // Fonts are re-rasterized at the scaled size (stays crisp)
    s_uiScale = scale;
}

float getUIScale() {
    return s_uiScale;
}

void tooltip(const char* fmt, ...) {
    ImGuiStyle& style = ImGui::GetStyle();
    // The theme's padding, not whatever the window drawing this has pushed (the node graph canvas has none),
    // and a little more room at the sides.
    const ImVec2 base = s_hasBaseStyle ? ImVec2(s_baseStyle.WindowPadding.x * s_uiScale, s_baseStyle.WindowPadding.y * s_uiScale) : style.WindowPadding;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(base.x + scaled(6.0f), base.y));
    const bool open = ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePrevious, ImGuiWindowFlags_None);
    ImGui::PopStyleVar();
    if (!open) return;
    // Where the font's UI scale is switched off, the tooltip's text still follows it
    ImGui::SetWindowFontScale(style.FontScaleDpi > 0.0f ? s_uiScale / style.FontScaleDpi : 1.0f);
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
    ImGui::EndTooltip();
}

void pushPopupPadding() {
    const ImVec2 base = s_hasBaseStyle ? ImVec2(s_baseStyle.WindowPadding.x * s_uiScale, s_baseStyle.WindowPadding.y * s_uiScale) : ImGui::GetStyle().WindowPadding;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, base);
}

} // namespace UI
} // namespace PropulsionMKI
