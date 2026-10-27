cmake_minimum_required(VERSION 3.22)

# =============================================================================
# twizzledart — Linux Flutter Plugin CMakeLists.txt
# =============================================================================

set(PROJECT_NAME "twizzledart")
project(${PROJECT_NAME} LANGUAGES CXX)

set(PLUGIN_NAME "twizzledart_plugin")

add_library(${PLUGIN_NAME} SHARED
  "twizzledart_plugin.cc"
)

apply_standard_settings(${PLUGIN_NAME})

# Include native/ core (twizzle_core + twizzle_wgpu)
add_subdirectory(
  "${CMAKE_CURRENT_SOURCE_DIR}/../native"
  "${CMAKE_BINARY_DIR}/twizzle_native"
)

set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_VISIBILITY_PRESET hidden
)

target_compile_definitions(${PLUGIN_NAME} PRIVATE
  FLUTTER_PLUGIN_IMPL
  TWIZZLE_LINUX=1
)

target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_include_directories(${PLUGIN_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../native/include"
)

target_link_libraries(${PLUGIN_NAME} PRIVATE
  flutter
  PkgConfig::GTK
  twizzle_wgpu
  dl
  pthread
  m
)

# List of absolute paths to libraries that should be bundled with the plugin.
set(twizzledart_bundled_libraries
  ""
  PARENT_SCOPE
)
