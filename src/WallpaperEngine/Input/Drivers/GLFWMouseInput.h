#pragma once

#include "WallpaperEngine/Input/MouseInput.h"

#include <glm/vec2.hpp>

namespace WallpaperEngine::Render::Drivers {
class GLFWOpenGLDriver;
}

namespace WallpaperEngine::Input::Drivers {
/**
 * Handles mouse input for the background
 */
class GLFWMouseInput final : public MouseInput {
public:
    explicit GLFWMouseInput (const Render::Drivers::GLFWOpenGLDriver& driver);

    /**
     * Takes current mouse position and updates it
     */
    void update () override;

    /**
     * The virtual pointer's position
     */
    [[nodiscard]] glm::dvec2 position () const override;

    void setPositionOverride (std::optional<glm::dvec2> position) override;

    /**
     * @return The status of the mouse's left click
     */
    [[nodiscard]] MouseClickStatus leftClick () const override;

    /**
     * @return The status of the mouse's right click
     */
    [[nodiscard]] MouseClickStatus rightClick () const override;

private:
    const Render::Drivers::GLFWOpenGLDriver& m_driver;

    /**
     * The current mouse position
     */
    glm::dvec2 m_mousePosition = {};
    glm::dvec2 m_reportedPosition = {};
    /** When set, overrides the device position (control-interface pointer forwarding) */
    std::optional<glm::dvec2> m_positionOverride = std::nullopt;
    MouseClickStatus m_leftClick = Released;
    MouseClickStatus m_rightClick = Released;
};
} // namespace WallpaperEngine::Input::Drivers
