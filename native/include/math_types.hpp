#pragma once
/// @file math_types.hpp
/// @brief Single include point for GLM (linear algebra) across all platforms.
///
/// Replaces gl_platform.hpp for code that is GPU-backend-agnostic.
/// Renderer-specific backends (OpenGL, WebGPU, Metal) include their own
/// platform headers on top of this.

// GLM — enable experimental extensions needed for quat/toMat4 helpers.
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
