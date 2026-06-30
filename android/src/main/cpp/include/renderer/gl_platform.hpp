#pragma once

/// @file gl_platform.hpp
/// @brief Single include point for OpenGL (ES3/GL3) + GLM across platforms.
///
/// Selects GLES3 headers on Android/iOS and desktop GL3 headers everywhere
/// else, so the rest of the renderer never branches on platform itself.

#if defined(TWIZZLE_ANDROID) || defined(__ANDROID__)
  #include <GLES3/gl3.h>
  #include <GLES3/gl3ext.h>
  #define TWIZZLE_GLES 1

#elif defined(TWIZZLE_IOS)
  #include <OpenGLES/ES3/gl.h>
  #include <OpenGLES/ES3/glext.h>
  #define TWIZZLE_GLES 1

#elif defined(__APPLE__)
  // macOS — suppress deprecation warnings (still works fine)
  #define GL_SILENCE_DEPRECATION
  #include <OpenGL/gl3.h>
  #define TWIZZLE_GLES 0

#else
  // Linux / Windows — GLEW provides the extension loader
  #include <GL/glew.h>
  #define TWIZZLE_GLES 0
#endif

/// @return the GLSL `#version`/precision preamble for the current platform.
#if TWIZZLE_GLES
  static inline const char* glslPreamble() {
      return "#version 300 es\nprecision highp float;\n";
  }
#else
  static inline const char* glslPreamble() {
      return "#version 330 core\n";
  }
#endif

// GLM — enable experimental extensions needed for quat/toMat4 helpers.
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
