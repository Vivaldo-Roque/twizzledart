cmake_minimum_required(VERSION 3.22.1)
project(twizzle)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add all source files
add_library(twizzle SHARED
    jni_bindings.cpp
    jni_renderer.cpp
    src/core/cube.cpp
    src/parser/algorithm.cpp
    src/renderer/Camera.cpp
    src/renderer/CubeGeometry.cpp
    src/renderer/CubeRenderer.cpp
    src/renderer/MoveAnimator.cpp
    src/renderer/Shader.cpp
)

# Set include directories
target_include_directories(twizzle PRIVATE
    include
)

target_compile_definitions(twizzle PRIVATE TWIZZLE_ANDROID)

# Find and link required Android system libraries
find_library(LOG_LIB log)
find_library(GLES3_LIB GLESv3)
find_library(EGL_LIB EGL)

target_link_libraries(twizzle
    ${LOG_LIB}
    ${GLES3_LIB}
    ${EGL_LIB}
)
