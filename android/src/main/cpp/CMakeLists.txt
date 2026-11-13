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
  # Phase 3: WebGPU JNI bridge
  jni_wgpu.cpp
  # Phase 3.5: Dual-engine OpenGL ES 3.0 JNI bridge & renderer
  jni_renderer.cpp
  src/renderer/CubeRenderer.cpp
  src/renderer/Shader.cpp
  # Legacy cube-state-only JNI (CubeEngine.kt — pure logic, no GPU)
  jni_bindings.cpp
)

target_include_directories(twizzle PRIVATE
  # native/ headers accessible to the JNI bridge
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../../native/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/renderer"
)

target_compile_definitions(twizzle PRIVATE
  TWIZZLE_ANDROID=1
)

if(ANDROID_ABI STREQUAL "armeabi-v7a")
  target_compile_definitions(twizzle PRIVATE TWIZZLE_NO_WGPU=1)
endif()

# wgpu-native on Android requires Android system libs
find_library(LOG_LIB   log)
find_library(ANDROID_LIB android)
find_library(EGL_LIB   EGL)
find_library(GLES3_LIB GLESv3)

set(TWIZZLE_LIBS
  twizzle_core
  ${LOG_LIB}
  ${ANDROID_LIB}
  ${EGL_LIB}
  ${GLES3_LIB}
)

if(TARGET twizzle_wgpu)
  list(APPEND TWIZZLE_LIBS twizzle_wgpu)
endif()

target_link_libraries(twizzle
  ${TWIZZLE_LIBS}
)

# Support 16KB page sizes on Android 15+ (sdk_gphone16k emulator and modern devices)
target_link_options(twizzle PRIVATE "-Wl,-z,max-page-size=16384")
