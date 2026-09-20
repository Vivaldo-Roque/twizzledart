#pragma once
/// @file WebGpuRenderer.hpp
/// @brief Top-level WebGPU renderer for a 3x3x3 Rubik's Cube.
///
/// This is the platform-agnostic interface of the WebGPU cube renderer.
/// It owns the wgpu device/queue/pipeline, the orbit camera, and the move
/// timeline (MoveAnimator). Platform glue code (Windows, Android, iOS, macOS)
/// creates a WGPUSurface, calls init(), and calls render() once per frame.
///
/// Backends selected automatically by wgpu-native:
///   Android / Linux → Vulkan
///   iOS / macOS     → Metal
///   Windows         → DirectX 12 (primary) / Vulkan (fallback)
#include "math_types.hpp"
#include "Camera.hpp"
#include "CubeGeometry.hpp"
#include "MoveAnimator.hpp"
#include "twizzle/twizzle.hpp"
#include <webgpu/webgpu.h>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace twizzle::renderer {

/**
 * @brief All platform-specific handles needed to create the WGPUSurface.
 *
 * The platform plugin fills this struct and passes it to init().
 */
struct SurfaceDescriptor {
    // --- Android ---
    void* androidNativeWindow = nullptr;  ///< ANativeWindow* on Android

    // --- Windows ---
    void* hinstance            = nullptr; ///< HINSTANCE on Windows
    void* hwnd                 = nullptr; ///< HWND on Windows

    // --- Apple ---
    void* metalLayer           = nullptr; ///< CAMetalLayer* on iOS/macOS

    uint32_t width  = 1;
    uint32_t height = 1;
};

class WebGpuRenderer {
public:
    WebGpuRenderer();
    ~WebGpuRenderer();

    // Non-copyable, non-movable (owns GPU resources).
    WebGpuRenderer(const WebGpuRenderer&)            = delete;
    WebGpuRenderer& operator=(const WebGpuRenderer&) = delete;

    /// Initialises the WebGPU instance, adapter, device, swap-chain and
    /// render pipeline. Call once after the native window/surface is ready.
    /// @return false if any WebGPU initialisation step fails.
    bool init(const SurfaceDescriptor& desc);

    /// Updates the swap-chain size. Call on every resize event.
    void resize(uint32_t width, uint32_t height);

    /// Advances animation, encodes and submits one frame.
    /// @param dt Seconds since the last call to render().
    void render(float dt);

    /// Releases all WebGPU resources. Call before destroying the surface.
    void destroy();

    // ── Camera input ──────────────────────────────────────────────────────
    void onDragBegin(float x, float y);
    void onDragMove (float x, float y);
    void onDragEnd  ();
    void onDrag     (float dx, float dy);
    void onZoom     (float delta);

    // ── Move timeline ─────────────────────────────────────────────────────
    void applyMove     (const twizzle::Move& move);
    void applyAlgorithm(const std::string&   algStr);
    void reset         ();

    // ── Transport ─────────────────────────────────────────────────────────
    void play();
    void pause();
    void togglePlay();
    void jumpToStart();
    void jumpToEnd();
    void stepForward();
    void stepBackward();

    // ── Scrubbing ─────────────────────────────────────────────────────────
    void  seekFraction(float f);
    void  seekMs      (float ms);
    float currentFraction() const;
    float currentMs()       const;
    float totalDurationMs() const;

    bool  isPlaying()   const;
    bool  isAnimating() const;

    // ── Visual properties ─────────────────────────────────────────────────
    void  setSpeed          (float speed);
    float getSpeed          () const;
    void  setBackgroundColor(float r, float g, float b, float a);
    void  setCameraPosition (float latitude, float longitude, float radius);
    void  setPitchLock      (bool  locked);
    void  setShowHint       (bool  show);
    void  setDebugLogs      (bool  enabled);
    void  setFaceColors     (const float colors[6][3]);
    void  setBodyAlpha      (float alpha);

private:
    // ── WebGPU objects ────────────────────────────────────────────────────
    WGPUInstance  instance_  = nullptr;
    WGPUAdapter   adapter_   = nullptr;
    WGPUDevice    device_    = nullptr;
    WGPUQueue     queue_     = nullptr;
    WGPUSurface   surface_   = nullptr;
    WGPURenderPipeline hintPipeline_    = nullptr;
    WGPURenderPipeline foundPipeline_   = nullptr;
    WGPURenderPipeline stickerPipeline_ = nullptr;

    // Buffers
    WGPUBuffer vertexBuffer_  = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;

    // Bind group / layout
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUBindGroup       bindGroup_       = nullptr;

    // Depth-stencil texture
    WGPUTexture     depthTexture_     = nullptr;
    WGPUTextureView depthTextureView_ = nullptr;

    // ── Cube state ────────────────────────────────────────────────────────
    std::vector<Vertex>    vertices_;
    std::vector<CubieInfo> cubies_;
    OrbitCamera                   camera_;
    std::unique_ptr<MoveAnimator> animator_;
    twizzle::Cube3x3              cube_;

    // ── Frame state ───────────────────────────────────────────────────────────
    bool     initialized_ = false;
    uint32_t width_  = 1, height_ = 1;
    WGPUTextureFormat surfaceFormat_ = WGPUTextureFormat_BGRA8Unorm; ///< detected at runtime
    // Background clear color (linear RGB, default off-white ~#F4F4F7)
    float    bgR_ = 0.91f, bgG_ = 0.91f, bgB_ = 0.93f, bgA_ = 1.0f;
    bool     isPlaying_ = false;
    bool     showHint_  = false;
    bool     debugLogs_ = false;
    bool     dragging_  = false;
    float    lastDragX_ = 0, lastDragY_ = 0;

    // ── Internal helpers ──────────────────────────────────────────────────

    /// Creates the WGPUSurface from the platform surface descriptor.
    bool createSurface    (const SurfaceDescriptor& desc);
    /// Requests an adapter synchronously (blocks until callback fires).
    bool requestAdapter   ();
    /// Requests a device synchronously.
    bool requestDevice    ();
    /// Configures (or re-configures) the swap-chain for the current size.
    void configureSurface ();
    /// Compiles the WGSL shader module.
    WGPUShaderModule createShaderModule() const;
    /// Builds the render pipeline (layout, vertex state, fragment state).
    bool createPipeline   ();
    /// Creates the depth texture + view for the current size.
    void createDepthTexture();
    /// Uploads `vertices_` to `vertexBuffer_` (creates if needed).
    void uploadVertexBuffer();
    /// Creates / updates the uniform buffer (MVP + bodyAlpha).
    void createUniformBuffer();
    /// Rebuilds the bind group after buffer re-creation.
    void rebuildBindGroup();

    /// @return a monotonic wall-clock timestamp in seconds.
    static float nowSeconds();
};

/// Uniform block layout (std140/wgsl layout).
struct CubeUniforms {
    float mvp[16];      ///< 4×4 column-major model-view-projection matrix
    float model[16];    ///< per-cubie model matrix (overridden per draw call via push-constants)
    float bodyAlpha;    ///< foundation opacity [0,1]
    float _pad[3];      ///< align to 16 bytes
};

} // namespace twizzle::renderer
