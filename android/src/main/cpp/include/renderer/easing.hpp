#pragma once
#include <algorithm>

namespace twizzle::renderer {

/**
 * @brief Quintic ease-in-out, exactly matching cubing.js's `smootherStep`.
 *
 * Formula: x³(10 - x(15 - 6x)). Smoother than the classic cubic
 * `smoothstep` (zero first AND second derivative at the endpoints),
 * which is what gives cube move animations their characteristic "settle".
 *
 * @param x progress in [0,1] (values outside are clamped).
 * @return eased progress in [0,1].
 */
inline float smootherStep(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * x * (10.0f - x * (15.0f - 6.0f * x));
}

} // namespace twizzle::renderer
