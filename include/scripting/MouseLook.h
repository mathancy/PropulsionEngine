#pragma once

/**
 * MouseLook.h
 * Turns mouse movement into the `lookX` / `lookY` input axes that scripts read
 * (`axis lookX of Input`). Header-only so it can be tested without a window.
 *
 * The axes are a *rate*, like the stick of a gamepad: scripts scale them by a turn speed and `per second`
 * (`TurnAmount Turn times TurnSpeed`, then `add TurnAmount per second to ...`), so the result doesn't change with
 * the frame rate. Moving the mouse at 1000 pixels per second reads 1.0; faster than that reads more than 1, because
 * unlike a stick a mouse has no stop.
 *
 *   lookX: positive when the mouse moves right
 *   lookY: positive when the mouse moves up (the screen's y runs down, so this is flipped)
 */
namespace Propulsion {

struct MouseLook {
    static constexpr float PixelsPerSecondForOne = 1000.0f;

    float x = 0.0f;
    float y = 0.0f;

    /// Forget where the cursor was: the next `update` only records a position. Call this whenever the cursor was
    /// somewhere else (just captured, or the window was out of focus), so the jump doesn't count as a flick.
    void reset() {
        m_hasLast = false;
        x = y = 0.0f;
    }

    /// The cursor is at (cursorX, cursorY) pixels now, `deltaTime` seconds after the previous call.
    void update(double cursorX, double cursorY, float deltaTime) {
        if (!m_hasLast || deltaTime <= 0.0f) {
            m_hasLast = true;
            m_lastX = cursorX;
            m_lastY = cursorY;
            x = y = 0.0f;
            return;
        }
        const float pixelsX = static_cast<float>(cursorX - m_lastX);
        const float pixelsY = static_cast<float>(m_lastY - cursorY);
        m_lastX = cursorX;
        m_lastY = cursorY;
        x = pixelsX / (deltaTime * PixelsPerSecondForOne);
        y = pixelsY / (deltaTime * PixelsPerSecondForOne);
    }

private:
    bool m_hasLast = false;
    double m_lastX = 0.0, m_lastY = 0.0;
};

} // namespace Propulsion
