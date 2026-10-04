// ForgeEngine UI Theme
// Provides theming functions for ImGui styling

#pragma once

namespace ForgeEngine {
namespace UI {

/**
 * @brief Sets up the Frosted Glass ImGui theme
 * 
 * This theme features:
 * - Dark translucent glass-like backgrounds
 * - Orange ember accent colors for selections and highlights
 * - Modern rounded corners and smooth styling
 * - Optimized readability with high contrast text
 */
void setupFrostedGlassTheme();

/**
 * @brief Applies a global UI scale (fonts + spacing/padding/rounding) on top of the theme.
 *
 * Call after setupFrostedGlassTheme(). Safe to call repeatedly (e.g. when the window moves
 * to a monitor with a different DPI) - each call rescales from the unscaled theme, so
 * values never compound or drift.
 *
 * @param scale 1.0 = 96 DPI / 100%. Clamped to [0.5, 4.0].
 */
void setUIScale(float scale);

/** @brief Current global UI scale set by setUIScale() (1.0 if never called). */
float getUIScale();

/** @brief Converts a design-time pixel value (authored at 1x) to the current UI scale. */
inline float scaled(float pixels) { return pixels * getUIScale(); }

/**
 * @brief Shows a hover tooltip (printf-style) in place of ImGui::SetTooltip.
 *
 * Gives the text room on the left and right, and keeps the text at the UI scale even where the caller
 * has switched the font's UI scale off (the node graph canvas does, and does its own zooming).
 */
void tooltip(const char* fmt, ...);

/**
 * @brief Pushes the theme's window padding for a popup menu; the caller pops it with ImGui::PopStyleVar().
 *
 * A popup opened from inside a window that pushed zero padding (the node graph canvas does) would otherwise
 * inherit it and have its items flush against the edges. Keep it pushed until after the matching EndPopup()
 * so submenus opened from the popup get the same padding.
 */
void pushPopupPadding();

} // namespace UI
} // namespace ForgeEngine
