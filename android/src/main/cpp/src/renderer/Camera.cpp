#include "renderer/Camera.hpp"
#include <algorithm>
#include <cmath>

namespace twizzle::renderer {

// ---------------------------------------------------------------------------
// temperMovement — from TwistyOrbitControls.ts:
//   sign(f) * log(|f*10| + 1) / 6
// Prevents huge jumps on fast drags.
// ---------------------------------------------------------------------------
static float temperMovement(float f) {
    float s = (f > 0) ? 1.0f : (f < 0) ? -1.0f : 0.0f;
    return s * std::log(std::abs(f * 10.0f) + 1.0f) / 6.0f;
}

// ---------------------------------------------------------------------------
// momentumScale — from inertia in TwistyOrbitControls.ts:
//   (exp(1-p) - (1-p)) / (1 - e) + 1
// ---------------------------------------------------------------------------
static float momentumScale(float progress) {
    constexpr float e = 2.71828182845f;
    return (std::exp(1.0f - progress) - (1.0f - progress)) / (1.0f - e) + 1.0f;
}

// ---------------------------------------------------------------------------
OrbitCamera::OrbitCamera()
    : latitude_(35.0f), longitude_(30.0f), radius_(14.0f),
      width_(800), height_(600) {}

void OrbitCamera::setViewport(int w, int h) { width_ = w; height_ = h; }

// ---------------------------------------------------------------------------
// drag — applies tempered movement + updates inertia state
// ---------------------------------------------------------------------------
void OrbitCamera::drag(float dx, float dy) {
    float minDim = float(std::min(width_, height_));
    if (minDim < 1.0f) minDim = 1.0f;

    float tx = temperMovement(dx / minDim);
    float ty = temperMovement((dy / minDim) * kVerticalScale);

    constexpr float DEG_PER_RAD = 360.0f / (2.0f * 3.14159265f);
    longitude_ -= 2.0f * tx * DEG_PER_RAD;
    latitude_  += 2.0f * ty * DEG_PER_RAD;
    latitude_   = std::clamp(latitude_, -89.0f, 89.0f);

    // Track for inertia
    inertia_.lastTemperedX = tx * 10.0f;
    inertia_.lastTemperedY = ty * 10.0f;
}

// ---------------------------------------------------------------------------
// beginDrag / endDrag — inertia on release
// ---------------------------------------------------------------------------
void OrbitCamera::beginDrag(float timeNow) {
    inertia_.active = false;
    inertia_.dragStartTime = timeNow;
}

void OrbitCamera::endDrag(float timeNow) {
    float elapsed = timeNow - inertia_.dragStartTime;
    // Only apply inertia if the user was actively moving (< 60 ms since last move)
    if (elapsed < 0.06f && (inertia_.lastTemperedX != 0 || inertia_.lastTemperedY != 0)) {
        inertia_.active     = true;
        inertia_.startTime  = timeNow;
        inertia_.lastTime   = timeNow;
        inertia_.momentumX  = inertia_.lastTemperedX;
        inertia_.momentumY  = inertia_.lastTemperedY;
    }
}

// ---------------------------------------------------------------------------
// update — call every frame to apply inertia decay
// ---------------------------------------------------------------------------
void OrbitCamera::update(float dt, float timeNow) {
    if (!inertia_.active) return;

    constexpr float kInertiaMs = 0.5f; // 500ms
    float progBefore = (inertia_.lastTime  - inertia_.startTime) / kInertiaMs;
    float progAfter  = (timeNow            - inertia_.startTime) / kInertiaMs;

    if (progBefore == 0.0f && progAfter > 0.05f) {
        inertia_.active = false; return;
    }

    progAfter = std::min(progAfter, 1.0f);
    float delta = momentumScale(progAfter) - momentumScale(progBefore);

    constexpr float DEG_PER_RAD = 360.0f / (2.0f * 3.14159265f);
    longitude_ -= 2.0f * inertia_.momentumX * delta * 1000.0f * DEG_PER_RAD;
    latitude_  += 2.0f * inertia_.momentumY * delta * 1000.0f * DEG_PER_RAD;
    latitude_   = std::clamp(latitude_, -89.0f, 89.0f);

    if (progAfter >= 1.0f) inertia_.active = false;
    inertia_.lastTime = timeNow;
}

void OrbitCamera::zoom(float delta) {
    radius_ -= delta * zoomSensitivity;
    radius_  = std::clamp(radius_, minRadius, maxRadius);
}

void OrbitCamera::reset() {
    latitude_ = 35.0f; longitude_ = 30.0f; radius_ = 6.0f;
    inertia_.active = false;
}

// ---------------------------------------------------------------------------
glm::vec3 OrbitCamera::position() const {
    float lat = glm::radians(latitude_);
    float lon = glm::radians(longitude_);
    return glm::vec3(
        radius_ * std::cos(lat) * std::sin(lon),
        radius_ * std::sin(lat),
        radius_ * std::cos(lat) * std::cos(lon)
    );
}

glm::mat4 OrbitCamera::view() const {
    return glm::lookAt(position(), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 OrbitCamera::projection() const {
    float aspect = (height_ > 0) ? float(width_) / float(height_) : 1.0f;
    return glm::perspective(glm::radians(kFov), aspect, kNear, kFar);
}

void OrbitCamera::setPosition(float latitude, float longitude, float radius) {
    latitude_ = std::max(-89.0f, std::min(89.0f, latitude));
    longitude_ = longitude;
    radius_ = std::max(minRadius, std::min(maxRadius, radius));
    inertia_.active = false; // Reset inertia when explicitly placing camera
}

} // namespace twizzle::renderer
