/// @file WebGpuRenderer.cpp
/// @brief WebGPU Rubik's Cube renderer implementation.
///
/// Initialises a wgpu-native device against the platform surface, compiles the
/// WGSL shader, builds the vertex + uniform pipeline, and renders one frame per
/// call to render(). The platform plugin (Windows / Android / iOS) creates the
/// WGPUSurface descriptor and calls init() once; from that point this class is
/// entirely platform-agnostic.
#include "renderer/WebGpuRenderer.hpp"
#include "twizzle/algorithm.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <wgpu/wgpu.h>
#if defined(__ANDROID__)
#include <android/log.h>
#define TW_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "TwizzleWGPU", __VA_ARGS__)
#define TW_LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "TwizzleWGPU", __VA_ARGS__)
#define TW_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "TwizzleWGPU", __VA_ARGS__)
#define TW_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "TwizzleWGPU", __VA_ARGS__)
#else
#define TW_LOGI(...) (std::fprintf(stdout, "[TwizzleWGPU] " __VA_ARGS__), std::fprintf(stdout, "\n"))
#define TW_LOGW(...) (std::fprintf(stderr, "[TwizzleWGPU WARN] " __VA_ARGS__), std::fprintf(stderr, "\n"))
#define TW_LOGE(...) (std::fprintf(stderr, "[TwizzleWGPU ERROR] " __VA_ARGS__), std::fprintf(stderr, "\n"))
#define TW_LOGD(...) (std::fprintf(stdout, "[TwizzleWGPU DEBUG] " __VA_ARGS__), std::fprintf(stdout, "\n"))
#endif

// Embed the WGSL shader source as a C string literal.
// In a real build this would be loaded from the file system or embedded via
// CMake's configure_file / xxd; for now we embed it directly.
static const char* kWgslShader = R"WGSL(
struct FrameUniforms {
    vp : mat4x4<f32>,
    bg : vec4<f32>,
};

struct CubieUniforms {
    model  : mat4x4<f32>,
    params : vec4<f32>,
};

@group(0) @binding(0) var<uniform> frame : FrameUniforms;
@group(1) @binding(0) var<uniform> cubie : CubieUniforms;

struct VertexInput {
    @location(0) position : vec3<f32>,
    @location(1) color    : vec4<f32>,
};

struct VertexOutput {
    @builtin(position) clipPosition : vec4<f32>,
    @location(0)       color        : vec4<f32>,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let worldPos     = cubie.model * vec4<f32>(in.position, 1.0);
    out.clipPosition = frame.vp * worldPos;
    out.color        = in.color;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    return in.color;
}
)WGSL";

namespace twizzle::renderer {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static float nowSec() {
    using namespace std::chrono;
    static auto start = steady_clock::now();
    return duration<float>(steady_clock::now() - start).count();
}

// Minimal synchronous adapter request (wgpu-native extension).
static WGPUAdapter requestAdapterSync(WGPUInstance instance,
                                      WGPUSurface   surface) {
    struct Result { WGPUAdapter adapter = nullptr; bool done = false; };
    Result res;

    WGPURequestAdapterOptions opts = {};
    opts.compatibleSurface = surface;
    opts.powerPreference   = WGPUPowerPreference_HighPerformance;

    wgpuInstanceRequestAdapter(instance, &opts,
        [](WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* msg, void* ud) {
            auto* r = reinterpret_cast<Result*>(ud);
            r->adapter = adapter;
            r->done    = true;
            if (status != WGPURequestAdapterStatus_Success || !adapter) {
                TW_LOGE("wgpuInstanceRequestAdapter failed (status=0x%x): %s",
                        (unsigned)status, msg ? msg : "unknown reason");
            }
        }, &res);

    // wgpu-native processes the callback synchronously on this thread.
    assert(res.done && "Adapter request did not complete synchronously");
    return res.adapter;
}

static WGPUDevice requestDeviceSync(WGPUAdapter adapter) {
    struct Result { WGPUDevice device = nullptr; bool done = false; };
    Result res;

    WGPUDeviceDescriptor desc = {};
    desc.label = "TwizzleDevice";

    wgpuAdapterRequestDevice(adapter, &desc,
        [](WGPURequestDeviceStatus status, WGPUDevice device, const char* msg, void* ud) {
            auto* r = reinterpret_cast<Result*>(ud);
            r->device = device;
            r->done   = true;
            if (status != WGPURequestDeviceStatus_Success || !device) {
                TW_LOGE("wgpuAdapterRequestDevice failed (status=0x%x): %s",
                        (unsigned)status, msg ? msg : "unknown reason");
            }
        }, &res);

    assert(res.done && "Device request did not complete synchronously");
    return res.device;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

WebGpuRenderer::WebGpuRenderer()  = default;
WebGpuRenderer::~WebGpuRenderer() { destroy(); }

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

bool WebGpuRenderer::init(const SurfaceDescriptor& desc) {
    if (initialized_) return true;

#if defined(__ANDROID__)
    wgpuSetLogLevel(debugLogs_ ? WGPULogLevel_Info : WGPULogLevel_Off);
    wgpuSetLogCallback([](WGPULogLevel level, const char* msg, void* ud) {
        auto* self = reinterpret_cast<WebGpuRenderer*>(ud);
        if (self && self->debugLogs_) {
            int priority = ANDROID_LOG_DEBUG;
            if (level == WGPULogLevel_Error) priority = ANDROID_LOG_ERROR;
            else if (level == WGPULogLevel_Warn)  priority = ANDROID_LOG_WARN;
            __android_log_print(priority, "TwizzleWGPU", "[wgpu %d] %s", (int)level, msg);
        }
    }, this);
#endif

    width_  = desc.width;
    height_ = desc.height;
    camera_.setViewport((int)width_, (int)height_);

    // 1. WebGPU Instance
    WGPUInstanceDescriptor instDesc = {};
    instance_ = wgpuCreateInstance(&instDesc);
    if (!instance_) { std::fprintf(stderr, "[Twizzle] wgpuCreateInstance failed\n"); return false; }

    // 2. Platform Surface
    if (!createSurface(desc)) return false;

    // 3. Adapter
    adapter_ = requestAdapterSync(instance_, surface_);
    if (!adapter_) { std::fprintf(stderr, "[Twizzle] No suitable WebGPU adapter\n"); return false; }

    // 4. Device + Queue
    device_ = requestDeviceSync(adapter_);
    if (!device_) { std::fprintf(stderr, "[Twizzle] WebGPU device creation failed\n"); return false; }
    queue_ = wgpuDeviceGetQueue(device_);

    // 5. Configure swap-chain
    configureSurface();

    // 6. Depth texture
    createDepthTexture();

    // 7. Build cube geometry
    CubeGeometry::build(vertices_, cubies_);
    uploadVertexBuffer();

    // 8. Uniform buffer + bind group layout
    createUniformBuffer();

    // 9. Render pipeline
    if (!createPipeline()) return false;

    // 10. Animator
    animator_ = std::make_unique<MoveAnimator>(cubies_);

    initialized_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// createSurface — platform-specific WGPUSurface creation
// ---------------------------------------------------------------------------

bool WebGpuRenderer::createSurface(const SurfaceDescriptor& d) {
    WGPUSurfaceDescriptor sd = {};

#if defined(__ANDROID__)
    WGPUSurfaceDescriptorFromAndroidNativeWindow anw = {};
    anw.chain.sType = WGPUSType_SurfaceDescriptorFromAndroidNativeWindow;
    anw.window      = d.androidNativeWindow;
    sd.nextInChain  = reinterpret_cast<const WGPUChainedStruct*>(&anw);

#elif defined(__APPLE__)
    WGPUSurfaceDescriptorFromMetalLayer ml = {};
    ml.chain.sType = WGPUSType_SurfaceDescriptorFromMetalLayer;
    ml.layer       = d.metalLayer;
    sd.nextInChain = reinterpret_cast<const WGPUChainedStruct*>(&ml);

#elif defined(_WIN32)
    WGPUSurfaceDescriptorFromWindowsHWND hw = {};
    hw.chain.sType = WGPUSType_SurfaceDescriptorFromWindowsHWND;
    hw.hinstance   = d.hinstance;
    hw.hwnd        = d.hwnd;
    sd.nextInChain = reinterpret_cast<const WGPUChainedStruct*>(&hw);

#else
    // Linux — assume Xlib (could also support Wayland)
    (void)d;
    std::fprintf(stderr, "[Twizzle] createSurface: unsupported platform\n");
    return false;
#endif

    surface_ = wgpuInstanceCreateSurface(instance_, &sd);
    if (!surface_) { std::fprintf(stderr, "[Twizzle] wgpuInstanceCreateSurface failed\n"); return false; }
    return true;
}

// ---------------------------------------------------------------------------
// configureSurface — swap-chain format / present mode
// ---------------------------------------------------------------------------

void WebGpuRenderer::configureSurface() {
    WGPUSurfaceCapabilities caps = {};
    wgpuSurfaceGetCapabilities(surface_, adapter_, &caps);

    // Use a linear (non-sRGB) format to match OpenGL's default behavior.
    // Modern APIs often default to sRGB, which causes "double gamma correction"
    // when passing Flutter's already-sRGB colors.
    WGPUTextureFormat fmt = WGPUTextureFormat_BGRA8Unorm;
    if (caps.formatCount > 0) {
        fmt = caps.formats[0]; // fallback to preferred
        for (uint32_t i = 0; i < caps.formatCount; ++i) {
            if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm || 
                caps.formats[i] == WGPUTextureFormat_RGBA8Unorm) {
                fmt = caps.formats[i];
                break;
            }
        }
    }

    WGPUSurfaceConfiguration cfg = {};
    cfg.device      = device_;
    cfg.format      = fmt;
    cfg.usage       = WGPUTextureUsage_RenderAttachment;
    cfg.width       = width_;
    cfg.height      = height_;
    cfg.presentMode = WGPUPresentMode_Fifo; // VSync
    cfg.alphaMode   = WGPUCompositeAlphaMode_Auto; // Reverted to avoid Android crash
    wgpuSurfaceConfigure(surface_, &cfg);

#if defined(__ANDROID__)
    if (debugLogs_) {
        __android_log_print(ANDROID_LOG_INFO, "TwizzleDEBUG",
            "Surface format=%d (BGRA8Unorm=%d, BGRA8UnormSrgb=%d, RGBA8Unorm=%d) size=%ux%u",
            (int)fmt, (int)WGPUTextureFormat_BGRA8Unorm,
            (int)WGPUTextureFormat_BGRA8UnormSrgb,
            (int)WGPUTextureFormat_RGBA8Unorm, width_, height_);
    }
#endif
    surfaceFormat_ = fmt;
}

// ---------------------------------------------------------------------------
// createDepthTexture
// ---------------------------------------------------------------------------

void WebGpuRenderer::createDepthTexture() {
    if (depthTexture_) {
        wgpuTextureViewRelease(depthTextureView_);
        wgpuTextureRelease(depthTexture_);
    }

    WGPUTextureDescriptor td = {};
    td.label         = "depth";
    td.usage         = WGPUTextureUsage_RenderAttachment;
    td.dimension     = WGPUTextureDimension_2D;
    td.size          = {width_, height_, 1};
    td.format        = WGPUTextureFormat_Depth24Plus;
    td.mipLevelCount = 1;
    td.sampleCount   = 1;
    depthTexture_ = wgpuDeviceCreateTexture(device_, &td);

    WGPUTextureViewDescriptor tvd = {};
    tvd.format          = WGPUTextureFormat_Depth24Plus;
    tvd.dimension       = WGPUTextureViewDimension_2D;
    tvd.mipLevelCount   = 1;
    tvd.arrayLayerCount = 1;
    tvd.aspect          = WGPUTextureAspect_DepthOnly;
    depthTextureView_ = wgpuTextureCreateView(depthTexture_, &tvd);
}

// ---------------------------------------------------------------------------
// uploadVertexBuffer
// ---------------------------------------------------------------------------

void WebGpuRenderer::uploadVertexBuffer() {
    if (vertexBuffer_) wgpuBufferRelease(vertexBuffer_);

    uint64_t size = vertices_.size() * sizeof(Vertex);
    WGPUBufferDescriptor bd = {};
    bd.label            = "vertex";
    bd.usage            = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    bd.size             = size;
    bd.mappedAtCreation = false;
    vertexBuffer_ = wgpuDeviceCreateBuffer(device_, &bd);
    wgpuQueueWriteBuffer(queue_, vertexBuffer_, 0, vertices_.data(), size);
}

// ---------------------------------------------------------------------------
// createUniformBuffer + bind group
// ---------------------------------------------------------------------------

void WebGpuRenderer::createUniformBuffer() {
    // Frame uniforms (binding 0, group 0) — VP matrix + background colour.
    constexpr uint64_t kFrameSize = sizeof(float) * (16 + 4); // mat4 + vec4
    // Per-cubie uniforms (binding 0, group 1) — model matrix + bodyAlpha.
    constexpr uint64_t kCubieSize = sizeof(float) * (16 + 4); // mat4 + vec4

    // We allocate one combined buffer and offset into it.
    // Align cubie block to 256 bytes (minUniformBufferOffsetAlignment).
    constexpr uint64_t kAlign = 256;
    constexpr uint64_t kFrameAligned = ((kFrameSize + kAlign - 1) / kAlign) * kAlign;
    constexpr uint64_t kCubieAligned = ((kCubieSize + kAlign - 1) / kAlign) * kAlign;
    // 1 frame block + 26 cubie blocks
    uint64_t totalSize = kFrameAligned + 26 * kCubieAligned;

    if (uniformBuffer_) wgpuBufferRelease(uniformBuffer_);

    WGPUBufferDescriptor bd = {};
    bd.label            = "uniforms";
    bd.usage            = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    bd.size             = totalSize;
    bd.mappedAtCreation = false;
    uniformBuffer_ = wgpuDeviceCreateBuffer(device_, &bd);

    rebuildBindGroup();
}

// ---------------------------------------------------------------------------
// createShaderModule
// ---------------------------------------------------------------------------

WGPUShaderModule WebGpuRenderer::createShaderModule() const {
    WGPUShaderModuleWGSLDescriptor wgsl = {};
    wgsl.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgsl.code        = kWgslShader;

    WGPUShaderModuleDescriptor smd = {};
    smd.nextInChain = reinterpret_cast<const WGPUChainedStruct*>(&wgsl);
    smd.label       = "cube_shader";
    return wgpuDeviceCreateShaderModule(device_, &smd);
}

// ---------------------------------------------------------------------------
// createPipeline
// ---------------------------------------------------------------------------

bool WebGpuRenderer::createPipeline() {
    WGPUShaderModule shader = createShaderModule();
    if (!shader) return false;

    // Vertex buffer layout: position (float3) + color (float4)
    WGPUVertexAttribute attrs[2] = {};
    attrs[0].format         = WGPUVertexFormat_Float32x3;
    attrs[0].offset         = offsetof(Vertex, position);
    attrs[0].shaderLocation = 0;
    attrs[1].format         = WGPUVertexFormat_Float32x4;
    attrs[1].offset         = offsetof(Vertex, color);
    attrs[1].shaderLocation = 1;

    WGPUVertexBufferLayout vbl = {};
    vbl.arrayStride    = sizeof(Vertex);
    vbl.stepMode       = WGPUVertexStepMode_Vertex;
    vbl.attributeCount = 2;
    vbl.attributes     = attrs;

    // Blend state: standard Porter-Duff "over" compositing for RGB
    WGPUBlendComponent blendComp = {};
    blendComp.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendComp.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendComp.operation = WGPUBlendOperation_Add;

    // For Alpha, we want to PRESERVE the destination alpha (which is 1.0 from clear)
    // so the OS doesn't blend the surface with the Flutter UI underneath.
    WGPUBlendComponent alphaBlend = {};
    alphaBlend.srcFactor = WGPUBlendFactor_Zero;
    alphaBlend.dstFactor = WGPUBlendFactor_One;
    alphaBlend.operation = WGPUBlendOperation_Add;

    WGPUBlendState blendState = {};
    blendState.color = blendComp;
    blendState.alpha = alphaBlend;

    // Use the format detected at configureSurface() time — avoids mismatch
    // when the platform returns RGBA8Unorm (19) instead of BGRA8Unorm (23).
    WGPUTextureFormat swapFmt = surfaceFormat_;

    WGPUColorTargetState cts = {};
    cts.format    = swapFmt;
    cts.blend     = &blendState;
    cts.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fs = {};
    fs.module      = shader;
    fs.entryPoint  = "fs_main";
    fs.targetCount = 1;
    fs.targets     = &cts;

    WGPUDepthStencilState dss = {};
    dss.format            = WGPUTextureFormat_Depth24Plus;
    dss.depthWriteEnabled = true;
    dss.depthCompare      = WGPUCompareFunction_Less;
    dss.stencilFront.compare     = WGPUCompareFunction_Always;
    dss.stencilFront.failOp      = WGPUStencilOperation_Keep;
    dss.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    dss.stencilFront.passOp      = WGPUStencilOperation_Keep;
    dss.stencilBack              = dss.stencilFront;
    dss.stencilReadMask   = 0xFFFFFFFF;
    dss.stencilWriteMask  = 0xFFFFFFFF;

    // Pipeline layout (bind group layouts)
    // Group 0: frame uniforms; Group 1: per-cubie uniforms
    WGPUBindGroupLayoutEntry bgle0 = {};
    bgle0.binding               = 0;
    bgle0.visibility            = WGPUShaderStage_Vertex;
    bgle0.buffer.type           = WGPUBufferBindingType_Uniform;
    bgle0.buffer.minBindingSize = sizeof(float) * 20; // mat4 + vec4

    WGPUBindGroupLayoutDescriptor bgld0 = {};
    bgld0.entryCount = 1;
    bgld0.entries    = &bgle0;
    WGPUBindGroupLayout bgl0 = wgpuDeviceCreateBindGroupLayout(device_, &bgld0);

    WGPUBindGroupLayoutEntry bgle1 = bgle0; // same shape for per-cubie
    WGPUBindGroupLayoutDescriptor bgld1 = {};
    bgld1.entryCount = 1;
    bgld1.entries    = &bgle1;
    WGPUBindGroupLayout bgl1 = wgpuDeviceCreateBindGroupLayout(device_, &bgld1);
    bindGroupLayout_ = bgl1;

    WGPUBindGroupLayout bgls[2] = {bgl0, bgl1};
    WGPUPipelineLayoutDescriptor pld = {};
    pld.bindGroupLayoutCount = 2;
    pld.bindGroupLayouts     = bgls;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device_, &pld);

    // ── Base RenderPipelineDescriptor ───────────────────────────────────────
    WGPURenderPipelineDescriptor rpd = {};
    rpd.layout                     = pipelineLayout;
    rpd.vertex.module              = shader;
    rpd.vertex.entryPoint          = "vs_main";
    rpd.vertex.bufferCount         = 1;
    rpd.vertex.buffers             = &vbl;
    rpd.primitive.topology         = WGPUPrimitiveTopology_TriangleList;
    rpd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    rpd.primitive.frontFace        = WGPUFrontFace_CCW;
    rpd.multisample.count          = 1;
    rpd.multisample.mask           = 0xFFFFFFFF;
    rpd.fragment                   = &fs;

    // ── 1. Hint Pipeline (BackSide: cull FRONT so only back faces show through)
    WGPUDepthStencilState dssHint = {};
    dssHint.format            = WGPUTextureFormat_Depth24Plus;
    dssHint.depthWriteEnabled = false;
    dssHint.depthCompare      = WGPUCompareFunction_LessEqual;
    dssHint.stencilFront.compare     = WGPUCompareFunction_Always;
    dssHint.stencilFront.failOp      = WGPUStencilOperation_Keep;
    dssHint.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    dssHint.stencilFront.passOp      = WGPUStencilOperation_Keep;
    dssHint.stencilBack              = dssHint.stencilFront;
    dssHint.stencilReadMask   = 0xFFFFFFFF;
    dssHint.stencilWriteMask  = 0xFFFFFFFF;

    rpd.depthStencil       = &dssHint;
    rpd.primitive.cullMode = WGPUCullMode_Front;
    hintPipeline_ = wgpuDeviceCreateRenderPipeline(device_, &rpd);

    // ── 2. Foundation Pipeline (Back culling, depth write enabled for opaque)
    WGPUDepthStencilState dssFound = dssHint;
    dssFound.depthWriteEnabled = true;
    dssFound.depthCompare      = WGPUCompareFunction_LessEqual;

    rpd.depthStencil       = &dssFound;
    rpd.primitive.cullMode = WGPUCullMode_Back;
    foundPipeline_ = wgpuDeviceCreateRenderPipeline(device_, &rpd);

    // ── 3. Sticker Pipeline (Opaque, Back culling, depth write enabled)
    WGPUDepthStencilState dssSticker = dssHint;
    dssSticker.depthWriteEnabled = true;
    dssSticker.depthCompare      = WGPUCompareFunction_LessEqual;

    WGPUColorTargetState ctsOpaque = {};
    ctsOpaque.format    = swapFmt;
    ctsOpaque.blend     = nullptr; // Opaque
    ctsOpaque.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fsOpaque = fs;
    fsOpaque.targets = &ctsOpaque;

    rpd.fragment           = &fsOpaque;
    rpd.depthStencil       = &dssSticker;
    rpd.primitive.cullMode = WGPUCullMode_Back;
    stickerPipeline_ = wgpuDeviceCreateRenderPipeline(device_, &rpd);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuBindGroupLayoutRelease(bgl0);
    wgpuShaderModuleRelease(shader);

    return (hintPipeline_ != nullptr && foundPipeline_ != nullptr && stickerPipeline_ != nullptr);
}

// ---------------------------------------------------------------------------
// rebuildBindGroup — re-creates bind group 1 (per-cubie) after buffer change.
// Bind group 0 (frame) is re-created inline in render() since it uses dynamic
// offsets; here we allocate the layout reference only.
// ---------------------------------------------------------------------------

void WebGpuRenderer::rebuildBindGroup() {
    // Bind groups are rebuilt per-frame in render() using dynamic offsets;
    // nothing to do at this point beyond ensuring layout_ is valid.
    (void)bindGroup_;
}

// ---------------------------------------------------------------------------
// resize
// ---------------------------------------------------------------------------

void WebGpuRenderer::resize(uint32_t width, uint32_t height) {
    width_  = (width  > 0) ? width  : 1;
    height_ = (height > 0) ? height : 1;
    camera_.setViewport((int)width_, (int)height_);
    if (!initialized_) return;
    configureSurface();
    createDepthTexture();
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------

void WebGpuRenderer::render(float dt) {
    if (!initialized_) return;

    float now = nowSeconds();
    camera_.update(dt, now);
    animator_->update(dt);

    static int s_logFrame = 0;
    if (debugLogs_ && ++s_logFrame % 60 == 1) {
        glm::vec3 p = camera_.position();
        int f0 = cubies_.empty() ? -1 : cubies_[0].foundCount;
        int s0 = cubies_.empty() ? -1 : cubies_[0].stickerCount;
        __android_log_print(ANDROID_LOG_INFO, "TwizzleDEBUG",
            "render: w=%u h=%u bg=(%.2f,%.2f,%.2f,%.2f) camPos=(%.2f,%.2f,%.2f) lat=%.1f lon=%.1f rad=%.1f verts=%zu cubies=%zu foundAlpha=%.2f cubie0[found=%d stick=%d]",
            width_, height_, bgR_, bgG_, bgB_, bgA_, p.x, p.y, p.z,
            camera_.latitude(), camera_.longitude(), camera_.radius(),
            vertices_.size(), cubies_.size(), CubeGeometry::kFoundAlpha, f0, s0);
    }

    // ── Acquire swap-chain texture ──────────────────────────────────────
    WGPUSurfaceTexture st = {};
    wgpuSurfaceGetCurrentTexture(surface_, &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
        if (st.status == WGPUSurfaceGetCurrentTextureStatus_Outdated ||
            st.status == WGPUSurfaceGetCurrentTextureStatus_Lost) {
            configureSurface();
        }
        return;
    }

    WGPUTextureView backbuffer = wgpuTextureCreateView(st.texture, nullptr);

    // ── Build VP uniform data ───────────────────────────────────────────
    glm::mat4 vp = camera_.projection() * camera_.view();
    struct FrameBlock {
        float vp[16];
        float bg[4];
    } fb;
    std::memcpy(fb.vp, glm::value_ptr(vp), sizeof(fb.vp));
    fb.bg[0] = bgR_; fb.bg[1] = bgG_; fb.bg[2] = bgB_; fb.bg[3] = bgA_;

    constexpr uint64_t kAlign        = 256;
    constexpr uint64_t kFrameSize    = sizeof(FrameBlock);
    constexpr uint64_t kFrameAligned = ((kFrameSize + kAlign - 1) / kAlign) * kAlign;
    constexpr uint64_t kCubieSize    = sizeof(float) * 20; // mat4 + vec4 (pad)
    constexpr uint64_t kCubieAligned = ((kCubieSize + kAlign - 1) / kAlign) * kAlign;

    wgpuQueueWriteBuffer(queue_, uniformBuffer_, 0, &fb, kFrameSize);


    for (int i = 0; i < (int)cubies_.size(); ++i) {
        glm::mat4 model = animator_->modelMatrix(i);
        struct CubieBlock {
            float model[16];
            float params[4]; // [0] = bodyAlpha
        } cb;
        std::memcpy(cb.model, glm::value_ptr(model), sizeof(cb.model));
        cb.params[0] = CubeGeometry::kFoundAlpha;
        cb.params[1] = cb.params[2] = cb.params[3] = 0.0f;

        uint64_t offset = kFrameAligned + (uint64_t)i * kCubieAligned;
        wgpuQueueWriteBuffer(queue_, uniformBuffer_, offset, &cb, kCubieSize);
    }

    // ── Command encoder ─────────────────────────────────────────────────
    WGPUCommandEncoderDescriptor ced = {}; ced.label = "frame";
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &ced);

    WGPURenderPassColorAttachment ca = {};
    ca.view         = backbuffer;
    ca.loadOp       = WGPULoadOp_Clear;
    ca.storeOp      = WGPUStoreOp_Store;
    ca.clearValue   = {bgR_, bgG_, bgB_, bgA_};

    WGPURenderPassDepthStencilAttachment da = {};
    da.view              = depthTextureView_;
    da.depthLoadOp       = WGPULoadOp_Clear;
    da.depthStoreOp      = WGPUStoreOp_Store;
    da.depthClearValue   = 1.0f;
    da.stencilLoadOp     = WGPULoadOp_Undefined;
    da.stencilStoreOp    = WGPUStoreOp_Undefined;

    WGPURenderPassDescriptor rpd = {};
    rpd.colorAttachmentCount    = 1;
    rpd.colorAttachments        = &ca;
    rpd.depthStencilAttachment  = &da;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rpd);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer_,
        0, vertices_.size() * sizeof(Vertex));

    // Build frame bind group (group 0)
    WGPUBindGroupEntry bge0 = {};
    bge0.binding = 0;
    bge0.buffer  = uniformBuffer_;
    bge0.offset  = 0;
    bge0.size    = kFrameSize;

    WGPUBindGroupLayout frameLayout = wgpuRenderPipelineGetBindGroupLayout(stickerPipeline_, 0);
    WGPUBindGroupDescriptor bgd0 = {};
    bgd0.layout     = frameLayout;
    bgd0.entryCount = 1;
    bgd0.entries    = &bge0;
    WGPUBindGroup frameGroup = wgpuDeviceCreateBindGroup(device_, &bgd0);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, frameGroup, 0, nullptr);

    // Prepare per-cubie bind groups (group 1)
    WGPUBindGroupLayout cubieLayout = wgpuRenderPipelineGetBindGroupLayout(stickerPipeline_, 1);
    std::vector<WGPUBindGroup> cubieGroups(cubies_.size());
    for (int i = 0; i < (int)cubies_.size(); ++i) {
        uint64_t cubieOffset = kFrameAligned + (uint64_t)i * kCubieAligned;

        WGPUBindGroupEntry bge1 = {};
        bge1.binding = 0;
        bge1.buffer  = uniformBuffer_;
        bge1.offset  = cubieOffset;
        bge1.size    = kCubieSize;

        WGPUBindGroupDescriptor bgd1 = {};
        bgd1.layout     = cubieLayout;
        bgd1.entryCount = 1;
        bgd1.entries    = &bge1;
        cubieGroups[i] = wgpuDeviceCreateBindGroup(device_, &bgd1);
    }

    // ── Pass 1: Hints (only if showHint_ is true) ───────────────────────────
    if (showHint_) {
        wgpuRenderPassEncoderSetPipeline(pass, hintPipeline_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, frameGroup, 0, nullptr);
        for (size_t i = 0; i < cubies_.size(); ++i) {
            const CubieInfo& c = cubies_[i];
            if (c.hintCount == 0) continue;
            wgpuRenderPassEncoderSetBindGroup(pass, 1, cubieGroups[i], 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, (uint32_t)c.hintCount, 1,
                                      (uint32_t)c.hintOffset, 0);
        }
    }

    // ── Pass 2: Foundation (black cubie bodies) ─────────────────────────────
    wgpuRenderPassEncoderSetPipeline(pass, foundPipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, frameGroup, 0, nullptr);
    for (size_t i = 0; i < cubies_.size(); ++i) {
        const CubieInfo& c = cubies_[i];
        if (c.foundCount == 0) continue;
        wgpuRenderPassEncoderSetBindGroup(pass, 1, cubieGroups[i], 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, (uint32_t)c.foundCount, 1,
                                      (uint32_t)c.foundOffset, 0);
    }

    // ── Pass 3: Stickers (opaque coloured quads) ────────────────────────────
    wgpuRenderPassEncoderSetPipeline(pass, stickerPipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, frameGroup, 0, nullptr);
    for (size_t i = 0; i < cubies_.size(); ++i) {
        const CubieInfo& c = cubies_[i];
        if (c.stickerCount == 0) continue;
        wgpuRenderPassEncoderSetBindGroup(pass, 1, cubieGroups[i], 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, (uint32_t)c.stickerCount, 1,
                                  (uint32_t)c.stickerOffset, 0);
    }

    for (auto bg : cubieGroups) {
        wgpuBindGroupRelease(bg);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(frameGroup);
    wgpuBindGroupLayoutRelease(frameLayout);
    wgpuBindGroupLayoutRelease(cubieLayout);

    WGPUCommandBufferDescriptor cbd = {}; cbd.label = "frame_cmd";
    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, &cbd);
    wgpuQueueSubmit(queue_, 1, &cmdBuf);
    wgpuCommandBufferRelease(cmdBuf);
    wgpuCommandEncoderRelease(encoder);

    wgpuSurfacePresent(surface_);
    wgpuTextureViewRelease(backbuffer);
    wgpuTextureRelease(st.texture);
}

// ---------------------------------------------------------------------------
// destroy
// ---------------------------------------------------------------------------

void WebGpuRenderer::destroy() {
    if (!initialized_) return;
    initialized_ = false;
    if (depthTextureView_) { wgpuTextureViewRelease(depthTextureView_); depthTextureView_ = nullptr; }
    if (depthTexture_)     { wgpuTextureRelease(depthTexture_);         depthTexture_     = nullptr; }
    if (uniformBuffer_)    { wgpuBufferRelease(uniformBuffer_);         uniformBuffer_    = nullptr; }
    if (vertexBuffer_)     { wgpuBufferRelease(vertexBuffer_);          vertexBuffer_     = nullptr; }
    if (hintPipeline_)     { wgpuRenderPipelineRelease(hintPipeline_);  hintPipeline_     = nullptr; }
    if (foundPipeline_)    { wgpuRenderPipelineRelease(foundPipeline_); foundPipeline_    = nullptr; }
    if (stickerPipeline_)  { wgpuRenderPipelineRelease(stickerPipeline_); stickerPipeline_ = nullptr; }
    if (queue_)            { wgpuQueueRelease(queue_);                  queue_            = nullptr; }
    if (device_)           { wgpuDeviceRelease(device_);                device_           = nullptr; }
    if (adapter_)          { wgpuAdapterRelease(adapter_);              adapter_          = nullptr; }
    if (surface_)          { wgpuSurfaceRelease(surface_);              surface_          = nullptr; }
    if (instance_)         { wgpuInstanceRelease(instance_);            instance_         = nullptr; }
}

// ---------------------------------------------------------------------------
// Camera input
// ---------------------------------------------------------------------------

void WebGpuRenderer::onDragBegin(float x, float y) {
    dragging_ = true;
    lastDragX_ = x;
    lastDragY_ = y;
    camera_.beginDrag(nowSeconds());
}

void WebGpuRenderer::onDragMove(float x, float y) {
    if (dragging_) {
        camera_.drag(x - lastDragX_, y - lastDragY_);
        lastDragX_ = x;
        lastDragY_ = y;
    }
}

void WebGpuRenderer::onDragEnd() {
    dragging_ = false;
    camera_.endDrag(nowSeconds());
}

void WebGpuRenderer::onDrag(float dx, float dy) { camera_.drag(dx, dy); }
void WebGpuRenderer::onZoom(float delta)         { camera_.zoom(delta); }

// ---------------------------------------------------------------------------
// Move timeline
// ---------------------------------------------------------------------------

void WebGpuRenderer::applyMove(const twizzle::Move& move) {
    cube_.applyMove(move);
    animator_->queueMove(move);
}

void WebGpuRenderer::applyAlgorithm(const std::string& algStr) {
    auto moves = twizzle::AlgorithmParser::parse(algStr);
    for (const auto& m : moves) applyMove(m);
}

void WebGpuRenderer::reset() {
    cube_.reset();
    animator_->clear();
    CubeGeometry::build(vertices_, cubies_);
    if (initialized_) uploadVertexBuffer();
}

// ── Transport ────────────────────────────────────────────────────────────
void  WebGpuRenderer::play()           { animator_->play(); }
void  WebGpuRenderer::pause()          { animator_->pause(); }
void  WebGpuRenderer::togglePlay()     { animator_->togglePlay(); }
void  WebGpuRenderer::jumpToStart()    { animator_->jumpToStart(); }
void  WebGpuRenderer::jumpToEnd()      { animator_->jumpToEnd(); }
void  WebGpuRenderer::stepForward()    { animator_->stepForward(); }
void  WebGpuRenderer::stepBackward()   { animator_->stepBackward(); }

// ── Scrubbing ─────────────────────────────────────────────────────────────
void  WebGpuRenderer::seekFraction(float f)  { animator_->seekFraction(f); }
void  WebGpuRenderer::seekMs(float ms)        { animator_->seekMs(ms); }
float WebGpuRenderer::currentFraction() const { return animator_->currentFraction(); }
float WebGpuRenderer::currentMs()       const { return animator_->currentMs(); }
float WebGpuRenderer::totalDurationMs() const { return animator_->totalDurationMs(); }
bool  WebGpuRenderer::isPlaying()       const { return animator_->isPlaying(); }
bool  WebGpuRenderer::isAnimating()     const { return animator_->isAnimating(); }

// ── Visual ────────────────────────────────────────────────────────────────
void WebGpuRenderer::setSpeed(float s)                         { animator_->setSpeed(s); }
float WebGpuRenderer::getSpeed() const                         { return animator_->getSpeed(); }
void WebGpuRenderer::setBackgroundColor(float r, float g, float b, float a) {
    bgR_ = r; bgG_ = g; bgB_ = b; bgA_ = a;
}
void WebGpuRenderer::setCameraPosition(float lat, float lon, float radius)
    { camera_.setPosition(lat, lon, radius); }
void WebGpuRenderer::setPitchLock(bool locked)
    { camera_.setPitchLock(locked); }
void WebGpuRenderer::setShowHint(bool show)
    { showHint_ = show; }
void WebGpuRenderer::setDebugLogs(bool enabled) {
    debugLogs_ = enabled;
#if defined(__ANDROID__)
    wgpuSetLogLevel(enabled ? WGPULogLevel_Info : WGPULogLevel_Off);
#endif
}

void WebGpuRenderer::setFaceColors(const float colors[6][3]) {
    glm::vec3 c[6];
    for (int i = 0; i < 6; ++i) c[i] = {colors[i][0], colors[i][1], colors[i][2]};
    CubeGeometry::setFaceColors(c);
    CubeGeometry::build(vertices_, cubies_);
    if (initialized_) uploadVertexBuffer();
}

void WebGpuRenderer::setBodyAlpha(float alpha) {
    CubeGeometry::setBodyAlpha(alpha);
    CubeGeometry::build(vertices_, cubies_);
    if (initialized_) uploadVertexBuffer();
}

float WebGpuRenderer::nowSeconds() { return nowSec(); }

} // namespace twizzle::renderer
