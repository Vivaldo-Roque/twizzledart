cmake_minimum_required(VERSION 3.22)

# =============================================================================
# twizzledart — Windows Flutter Plugin CMakeLists.txt
# =============================================================================

set(PLUGIN_NAME "twizzledart_plugin")

# ---------------------------------------------------------------------------
# Plugin source
# ---------------------------------------------------------------------------
add_library(${PLUGIN_NAME} SHARED
  "twizzledart_plugin.cpp"
)

# Include native/ core (twizzle_core + twizzle_wgpu)
add_subdirectory(
  "${CMAKE_CURRENT_SOURCE_DIR}/../native"
  "${CMAKE_BINARY_DIR}/twizzle_native"
)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
target_include_directories(${PLUGIN_NAME} PRIVATE
  # Flutter embedder headers (provided by the Flutter Windows SDK)
  "${FLUTTER_MANAGED_DIR}/ephemeral/include"

  # Our native include path (twizzle core + renderer headers)
  "${CMAKE_CURRENT_SOURCE_DIR}/../native/include"
)

# ---------------------------------------------------------------------------
# Link libraries
# ---------------------------------------------------------------------------
target_link_libraries(${PLUGIN_NAME} PRIVATE
  flutter
  flutter_wrapper_plugin
  twizzle_wgpu    # ← WebGPU renderer + core (pulls in wgpu_native, glm)
  # Windows system libs required by wgpu-native (DirectX 12 backend)
  d3d12.lib
  dxgi.lib
  dxguid.lib
)

# ---------------------------------------------------------------------------
# Compiler settings
# ---------------------------------------------------------------------------
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_VISIBILITY_PRESET hidden
  # Put the DLL in the Flutter runner output directory
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_compile_definitions(${PLUGIN_NAME} PRIVATE
  # wgpu-native Windows build
  TWIZZLE_WINDOWS=1
  UNICODE
  _UNICODE
)

target_compile_options(${PLUGIN_NAME} PRIVATE
  /W3        # Warning level 3
  /WX-       # Don't treat warnings as errors (Flutter SDK headers may warn)
  /permissive-
  /Zc:__cplusplus
)

# ---------------------------------------------------------------------------
# Plugin registration (Flutter tooling expects this file convention)
# ---------------------------------------------------------------------------
set(twizzledart_bundled_libraries
  ""
  PARENT_SCOPE
)
