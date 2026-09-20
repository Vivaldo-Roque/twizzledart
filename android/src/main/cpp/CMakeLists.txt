cmake_minimum_required(VERSION 3.22.1)
project(twizzle CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# native/ core — pulls in twizzle_core + twizzle_wgpu + wgpu-native + GLM
# =============================================================================
add_subdirectory(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../../native"
  "${CMAKE_BINARY_DIR}/twizzle_native"
)

# =============================================================================
# twizzle — JNI shared library loaded by Kotlin via System.loadLibrary("twizzle")
# =============================================================================
add_library(twizzle SHARED
  # Phase 3: WebGPU JNI bridge (replaces jni_renderer.cpp / OpenGL)
  jni_wgpu.cpp
  # Legacy cube-state-only JNI (CubeEngine.kt — pure logic, no GPU)
  jni_bindings.cpp
)

target_include_directories(twizzle PRIVATE
  # native/ headers accessible to the JNI bridge
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../../native/include"
)

target_compile_definitions(twizzle PRIVATE
  TWIZZLE_ANDROID=1
)

# wgpu-native on Android requires Android system libs
find_library(LOG_LIB   log)
find_library(ANDROID_LIB android)

target_link_libraries(twizzle
  # WebGPU renderer + core (glm, cube state, geometry, camera, animator)
  twizzle_wgpu
  # Android system
  ${LOG_LIB}
  ${ANDROID_LIB}
  # NOTE: GLES3 / EGL are NO LONGER needed — wgpu-native uses Vulkan directly.
  # They remain removed intentionally to enforce the migration.
)

# Support 16KB page sizes on Android 15+ (sdk_gphone16k emulator and modern devices)
target_link_options(twizzle PRIVATE "-Wl,-z,max-page-size=16384")

