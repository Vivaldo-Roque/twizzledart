#pragma once
/// @file Camera.hpp
/// @brief Spherical orbit camera — GPU-backend-agnostic.
///
/// Depends only on GLM (via math_types.hpp), not on any OpenGL/Vulkan/Metal
/// headers. This makes it reusable across all renderer backends.
#include "math_types.hpp"

namespace twizzle::renderer {

/**
 * @brief Spherical orbit camera with cubing.js-style drag inertia.
 *
 * Drag movement is "tempered" (logarithmically scaled) and releasing a
 * drag continues rotating with an exponentially-decaying momentum, matching
 * `TwistyOrbitControls.ts` from cubing.js.
 */
class OrbitCamera {
public:
    OrbitCamera();

    /// Updates the aspect ratio used by projection().
    void setViewport(int w, int h);

    /// Call when a drag gesture starts; `timeNow` resets the inertia clock.
    void beginDrag(float timeNow);
    /// Call on every drag-move event with the screen-space pixel delta.
    void drag(float dx, float dy);
    /// Call when the drag gesture ends; may trigger momentum/inertia.
    void endDrag(float timeNow);

    /// Moves the camera closer/further; `delta` > 0 zooms in.
    void zoom(float delta);

    /// Advances inertia decay. Call once per frame even when not dragging.
    void update(float dt, float timeNow);

    /// Resets to the default orbit position.
    void reset();

    /// Sets the spherical coordinates directly.
    void setPosition(float latitude, float longitude, float radius);

    glm::mat4 view()       const;
    glm::mat4 projection() const;
    glm::vec3 position()   const;

    /// Sets whether vertical rotation is restricted to [-89, 89] degrees.
    void setPitchLock(bool locked);

    float latitude()  const { return latitude_; }
    float longitude() const { return longitude_; }
    float radius()    const { return radius_; }

    float zoomSensitivity = 0.3f;
    float minRadius       = 3.0f;
    float maxRadius       = 14.0f;

private:
    bool  pitchLock_ = true;
    float latitude_;   ///< degrees, clamped if pitchLock_ is true
    float longitude_;  ///< degrees
    float radius_;
    int   width_, height_;

    static constexpr float kFov           = 45.0f;
    static constexpr float kNear          = 0.1f;
    static constexpr float kFar           = 100.0f;
    static constexpr float kVerticalScale = 0.75f; ///< matches cubing.js VERTICAL_MOVEMENT_BASE_SCALE

    /// Post-drag momentum decay state.
    struct InertiaState {
        bool  active        = false;
        float startTime     = 0;
        float lastTime      = 0;
        float dragStartTime = 0;
        float momentumX     = 0;
        float momentumY     = 0;
        float lastTemperedX = 0;
        float lastTemperedY = 0;
    } inertia_;
};

} // namespace twizzle::renderer
