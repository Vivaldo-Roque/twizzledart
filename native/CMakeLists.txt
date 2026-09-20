cmake_minimum_required(VERSION 3.22)
project(twizzle_native CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# External dependencies
# =============================================================================

include(FetchContent)

# ── GLM (header-only math library) ───────────────────────────────────────────
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        1.0.1
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

# ── wgpu-native (pre-built binaries + headers) ───────────────────────────────
# We download the pre-compiled wgpu-native release for the current platform.
# Supported platforms:
#   x86_64-pc-windows-msvc     → DirectX 12 / Vulkan
#   aarch64-linux-android      → Vulkan
#   x86_64-linux-gnu           → Vulkan
#   aarch64-apple-darwin       → Metal
#   x86_64-apple-darwin        → Metal
#   aarch64-apple-ios          → Metal
#
# See: https://github.com/gfx-rs/wgpu-native/releases

set(WGPU_VERSION "v22.1.0.5")

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(WGPU_TARGET "windows-x86_64-msvc")
  set(WGPU_ARCHIVE_EXT "zip")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
  # Android NDK cross-compile: detect ABI
  if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(WGPU_TARGET "android-aarch64")
  elseif(ANDROID_ABI STREQUAL "x86_64")
    set(WGPU_TARGET "android-x86_64")
  else()
    message(FATAL_ERROR "Unsupported Android ABI: ${ANDROID_ABI}")
  endif()
  set(WGPU_ARCHIVE_EXT "zip")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  # macOS: check host architecture
  if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(WGPU_TARGET "macos-aarch64")
  else()
    set(WGPU_TARGET "macos-x86_64")
  endif()
  set(WGPU_ARCHIVE_EXT "zip")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(WGPU_TARGET "ios-aarch64")
  set(WGPU_ARCHIVE_EXT "zip")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(WGPU_TARGET "linux-x86_64")
  set(WGPU_ARCHIVE_EXT "zip")
else()
  message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

set(WGPU_ARCHIVE_NAME "wgpu-${WGPU_TARGET}-release.${WGPU_ARCHIVE_EXT}")
set(WGPU_DOWNLOAD_URL
  "https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_VERSION}/${WGPU_ARCHIVE_NAME}")

FetchContent_Declare(
  wgpu_native
  URL ${WGPU_DOWNLOAD_URL}
)
FetchContent_MakeAvailable(wgpu_native)

# Point to the extracted directory
set(WGPU_DIR "${wgpu_native_SOURCE_DIR}")

# Locate the static library inside the extracted archive
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(WGPU_LIB "${WGPU_DIR}/lib/wgpu_native.lib")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android" OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(WGPU_LIB "${WGPU_DIR}/lib/libwgpu_native.a")
else()
  # macOS / iOS
  set(WGPU_LIB "${WGPU_DIR}/lib/libwgpu_native.a")
endif()

add_library(wgpu_native STATIC IMPORTED GLOBAL)
set_target_properties(wgpu_native PROPERTIES
  IMPORTED_LOCATION "${WGPU_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${WGPU_DIR}/include;${WGPU_DIR}/include/webgpu"
)

# =============================================================================
# twizzle_core — static library (GPU-agnostic: cube state + geometry + camera)
# =============================================================================

add_library(twizzle_core STATIC
  # Cube state + parser
  src/core/cube.cpp
  src/core/algorithm.cpp

  # Renderer math (geometry, camera, animator)
  src/renderer/CubeGeometry.cpp
  src/renderer/Camera.cpp
  src/renderer/MoveAnimator.cpp
)

target_include_directories(twizzle_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(twizzle_core PUBLIC glm::glm)

target_compile_options(twizzle_core PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

# =============================================================================
# twizzle_wgpu — static library (WebGPU renderer, depends on twizzle_core)
# =============================================================================

add_library(twizzle_wgpu STATIC
  src/wgpu/WebGpuRenderer.cpp
)

target_include_directories(twizzle_wgpu
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(twizzle_wgpu
  PUBLIC
    twizzle_core
    wgpu_native
)

# Android extra: link system libs
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
  target_link_libraries(twizzle_wgpu PUBLIC android log)
endif()
