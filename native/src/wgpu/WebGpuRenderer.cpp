/// @file WebGpuRenderer.cpp
/// @brief WebGPU Rubik's Cube renderer implementation.
///
/// Initialises a wgpu-native device against the platform surface, compiles the
/// WGSL shader, builds the vertex + uniform pipeline, and renders one frame per
/// call to render(). The platform plugin (Windows / Android / iOS / Linux) creates
/// the WGPUSurface descriptor and calls init() once; from that point this class is
/// entirely platform-agnostic.
#include "renderer/WebGpuRenderer.hpp"
#include "twizzle/algorithm.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <wgpu/wgpu.h>
#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#include <malloc.h>
#endif
#if defined(__ANDROID__)
#include <android/log.h>
#include <sys/system_properties.h>
#include <dlfcn.h>
#define TW_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "TwizzleWGPU", __VA_ARGS__)
#define TW_LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "TwizzleWGPU", __VA_ARGS__)
#define TW_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "TwizzleWGPU", __VA_ARGS__)
#define TW_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "TwizzleWGPU", __VA_ARGS__)

static bool isEmulatorDevice() {
    char prop[PROP_VALUE_MAX] = {0};
    if (__system_property_get("ro.kernel.qemu", prop) > 0 && strcmp(prop, "1") == 0) return true;
    if (__system_property_get("ro.boot.qemu", prop) > 0 && strcmp(prop, "1") == 0) return true;
    if (__system_property_get("ro.hardware", prop) > 0) {
        if (strstr(prop, "goldfish") || strstr(prop, "ranchu") || strstr(prop, "cheets")) return true;
    }
    if (__system_property_get("ro.build.characteristics", prop) > 0) {
        if (strstr(prop, "emulator")) return true;
    }
    if (__system_property_get("ro.product.model", prop) > 0) {
        if (strstr(prop, "sdk_gphone") || strstr(prop, "Android SDK built for x86")) return true;
    }
    return false;
}
#else
#define TW_LOGI(...) (std::fprintf(stdout, "[TwizzleWGPU] " __VA_ARGS__), std::fprintf(stdout, "\n"))
#define TW_LOGW(...) (std::fprintf(stderr, "[TwizzleWGPU WARN] " __VA_ARGS__), std::fprintf(stderr, "\n"))
#define TW_LOGE(...) (std::fprintf(stderr, "[TwizzleWGPU ERROR] " __VA_ARGS__), std::fprintf(stderr, "\n"))
#define TW_LOGD(...) (std::fprintf(stdout, "[TwizzleWGPU DEBUG] " __VA_ARGS__), std::fprintf(stdout, "\n"))
#endif

// Embed the WGSL shader source as a C string literal.
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
#if !defined(__ANDROID__)
    opts.compatibleSurface = surface;
#endif
#if defined(_WIN32)
    // On Windows, prefer HighPerformance (Discrete GPU like NVIDIA/AMD with dedicated VRAM).
    // If not present, wgpu-native automatically selects the Integrated GPU (iGPU).
    opts.powerPreference   = WGPUPowerPreference_HighPerformance;
#else
    // On mobile devices (Android/iOS), prefer low-power to save battery.
    opts.powerPreference   = WGPUPowerPreference_LowPower;
#endif

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

struct SharedOffscreenContext {
    std::mutex      mutex;
    WGPUInstance    instance    = nullptr;
    WGPUAdapter     adapter     = nullptr;
    WGPUDevice      device      = nullptr;
    WGPUQueue       queue       = nullptr;
    WGPUAdapterType adapterType = WGPUAdapterType_Unknown;

    bool ensureInitialized(bool debugLogs) {
        std::lock_guard<std::mutex> lock(mutex);
        if (device && queue) return true;

        WGPUInstanceDescriptor instDesc = {};
#if defined(_WIN32)
        // Explicitly select ONLY the DirectX 12 backend on Windows so wgpu-native
        // does not load and initialize Vulkan (vulkan-1.dll + ICDs + implicit layers)
        // and OpenGL (opengl32.dll + vendor WGL ICD heaps) into the process address space.
        WGPUInstanceExtras extras = {};
        extras.chain.sType        = static_cast<WGPUSType>(WGPUSType_InstanceExtras);
        extras.backends           = WGPUInstanceBackend_DX12;
        extras.flags              = WGPUInstanceFlag_DiscardHalLabels;
        extras.dx12ShaderCompiler = WGPUDx12Compiler_Fxc;
        instDesc.nextInChain      = reinterpret_cast<const WGPUChainedStruct*>(&extras);
#elif defined(__linux__) && !defined(__ANDROID__)
        WGPUInstanceExtras extras = {};
        extras.chain.sType   = static_cast<WGPUSType>(WGPUSType_InstanceExtras);
        extras.backends      = WGPUInstanceBackend_Vulkan;
        extras.flags         = WGPUInstanceFlag_DiscardHalLabels;
        instDesc.nextInChain = reinterpret_cast<const WGPUChainedStruct*>(&extras);
#endif
        instance = wgpuCreateInstance(&instDesc);
        if (instance) {
            adapter = requestAdapterSync(instance, nullptr);
        }

        // Fallback to default backends if primary backend had no suitable adapter
        if (!adapter) {
            if (instance) {
                wgpuInstanceRelease(instance);
                instance = nullptr;
            }
            WGPUInstanceDescriptor fallbackDesc = {};
            instance = wgpuCreateInstance(&fallbackDesc);
            if (!instance) return false;
            adapter = requestAdapterSync(instance, nullptr);
            if (!adapter) {
                wgpuInstanceRelease(instance);
                instance = nullptr;
                return false;
            }
        }

        {
            WGPUAdapterInfo adapterInfo = {};
            wgpuAdapterGetInfo(adapter, &adapterInfo);
            adapterType = adapterInfo.adapterType;
            if (debugLogs) {
                std::fprintf(stderr, "[Twizzle] Shared 3D Adapter: %s (%s), type=0x%x\n",
                             adapterInfo.description ? adapterInfo.description : "unknown",
                             adapterInfo.device ? adapterInfo.device : "unknown",
                             (unsigned)adapterType);
            }
            wgpuAdapterInfoFreeMembers(adapterInfo);
        }

        device = requestDeviceSync(adapter);
        if (!device) {
            wgpuAdapterRelease(adapter);
            wgpuInstanceRelease(instance);
            adapter  = nullptr;
            instance = nullptr;
            return false;
        }
        queue = wgpuDeviceGetQueue(device);
        return (queue != nullptr);
    }
};

static SharedOffscreenContext& sharedOffscreenContext() {
    static SharedOffscreenContext ctx;
    return ctx;
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

    isOffscreen_ = desc.isOffscreen;
    width_  = desc.width;
    height_ = desc.height;
    lastRenderedWidth_  = width_;
    lastRenderedHeight_ = height_;
    camera_.setViewport((int)width_, (int)height_);

    // 1–4. WebGPU Instance, Surface, Adapter, Device + Queue
    if (isOffscreen_) {
        auto& shared = sharedOffscreenContext();
        if (!shared.ensureInitialized(debugLogs_)) {
            std::fprintf(stderr, "[Twizzle] Shared offscreen WebGPU initialization failed\n");
            destroy();
            return false;
        }
        instance_         = shared.instance;
        adapter_          = shared.adapter;
        device_           = shared.device;
        queue_            = shared.queue;
        adapterType_      = shared.adapterType;
        usesSharedDevice_ = true;
    } else {
        usesSharedDevice_ = false;
        WGPUInstanceDescriptor instDesc = {};
#if defined(__ANDROID__)
        WGPUInstanceExtras extras = {};
        extras.chain.sType = static_cast<WGPUSType>(WGPUSType_InstanceExtras);
        if (isEmulatorDevice()) {
            TW_LOGI("Android Emulator detected: selecting GLES (GL) backend for WebGPU");
            extras.backends = WGPUInstanceBackend_GL;
        } else {
            void* vklib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
            if (vklib) {
                dlclose(vklib);
                TW_LOGI("Android Hardware device with Vulkan: selecting Vulkan backend");
                extras.backends = WGPUInstanceBackend_Vulkan;
            } else {
                TW_LOGI("Android Hardware device without Vulkan: selecting GLES (GL) backend");
                extras.backends = WGPUInstanceBackend_GL;
            }
        }
        instDesc.nextInChain = reinterpret_cast<const WGPUChainedStruct*>(&extras);
#elif defined(_WIN32)
        WGPUInstanceExtras extras = {};
        extras.chain.sType        = static_cast<WGPUSType>(WGPUSType_InstanceExtras);
        extras.backends           = WGPUInstanceBackend_DX12;
        extras.flags              = WGPUInstanceFlag_DiscardHalLabels;
        extras.dx12ShaderCompiler = WGPUDx12Compiler_Fxc;
        instDesc.nextInChain      = reinterpret_cast<const WGPUChainedStruct*>(&extras);
#endif
        instance_ = wgpuCreateInstance(&instDesc);
        if (!instance_) { std::fprintf(stderr, "[Twizzle] wgpuCreateInstance failed\n"); return false; }

        if (!createSurface(desc)) { destroy(); return false; }

        adapter_ = requestAdapterSync(instance_, surface_);
        if (!adapter_) { std::fprintf(stderr, "[Twizzle] No suitable WebGPU adapter\n"); destroy(); return false; }

        {
            WGPUAdapterInfo adapterInfo = {};
            wgpuAdapterGetInfo(adapter_, &adapterInfo);
            adapterType_ = adapterInfo.adapterType;
            if (debugLogs_) {
                std::fprintf(stderr, "[Twizzle] 3D Adapter: %s (%s), type=0x%x\n",
                             adapterInfo.description ? adapterInfo.description : "unknown",
                             adapterInfo.device ? adapterInfo.device : "unknown",
                             (unsigned)adapterType_);
            }
            wgpuAdapterInfoFreeMembers(adapterInfo);
        }

        device_ = requestDeviceSync(adapter_);
        if (!device_) { std::fprintf(stderr, "[Twizzle] WebGPU device creation failed\n"); destroy(); return false; }
        queue_ = wgpuDeviceGetQueue(device_);
    }

    // 5. Configure swap-chain or offscreen target
    if (isOffscreen_) {
        createOffscreenTarget();
    } else {
        configureSurface();
    }

    // 6. Depth texture
    createDepthTexture();

    // 7. Build cube geometry + fixed face letters
    CubeGeometry::build(vertices_, cubies_);
    CubeGeometry::buildFaceLetters(vertices_, letterOffset_, letterCount_);
    uploadVertexBuffer();

    // 8. Uniform buffer + staging buffer
    createUniformBuffer();

    // 9. Render pipeline + persistent bind groups
    if (!createPipeline()) { destroy(); return false; }

    // 10. Animator
    animator_ = std::make_unique<MoveAnimator>(cubies_);

    initialized_ = true;
    dirty_       = true;

#if defined(_WIN32)
    // Trim clean and unused D3D12 driver suballocator pages (staging/descriptor heaps)
    // from the Working Set so Task Manager reflects only the active pages in use.
    EmptyWorkingSet(GetCurrentProcess());
#elif defined(__linux__) && !defined(__ANDROID__)
    // Flush unused glibc heap chunks back to kernel via madvise(MADV_DONTNEED)
    malloc_trim(0);
#endif
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
    WGPUTextureFormat fmt = WGPUTextureFormat_BGRA8Unorm;

#if !defined(__ANDROID__)
    WGPUSurfaceCapabilities caps = {};
    wgpuSurfaceGetCapabilities(surface_, adapter_, &caps);

    if (caps.formatCount > 0) {
        fmt = caps.formats[0];
        for (uint32_t i = 0; i < caps.formatCount; ++i) {
            if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm || 
                caps.formats[i] == WGPUTextureFormat_RGBA8Unorm) {
                fmt = caps.formats[i];
                break;
            }
        }
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);
#else
    fmt = WGPUTextureFormat_RGBA8Unorm;
#endif

    WGPUSurfaceConfiguration cfg = {};
    cfg.device      = device_;
    cfg.format      = fmt;
    cfg.usage       = WGPUTextureUsage_RenderAttachment;
    cfg.width       = width_;
    cfg.height      = height_;
    cfg.presentMode = WGPUPresentMode_Fifo; // VSync
    cfg.alphaMode   = WGPUCompositeAlphaMode_Auto;
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
// createOffscreenTarget
// ---------------------------------------------------------------------------

void WebGpuRenderer::createOffscreenTarget() {
    surfaceFormat_ = WGPUTextureFormat_RGBA8Unorm;

    if (renderTargetTexture_) {
        wgpuTextureRelease(renderTargetTexture_);
        renderTargetTexture_ = nullptr;
    }
    if (msaaColorTextureView_) {
        wgpuTextureViewRelease(msaaColorTextureView_);
        msaaColorTextureView_ = nullptr;
    }
    if (msaaColorTexture_) {
        wgpuTextureRelease(msaaColorTexture_);
        msaaColorTexture_ = nullptr;
    }
    if (motionTargetTexture_) {
        wgpuTextureRelease(motionTargetTexture_);
        motionTargetTexture_ = nullptr;
    }
    if (readbackBuffer_) {
        wgpuBufferRelease(readbackBuffer_);
        readbackBuffer_ = nullptr;
    }
    if (device_) {
        wgpuDevicePoll(device_, true, nullptr);
    }

    // Compute lightweight motion resolution (aligned to 64px = 256 bytes, capped at 640px)
    // for 60 FPS animations/dragging on integrated GPUs (Intel UHD / Iris Xe).
    if (width_ > 640u) {
        uint32_t scaledW = ((width_ * 3u / 4u) + 63u) & ~63u;
        motionWidth_ = std::min<uint32_t>(640u, std::max<uint32_t>(384u, scaledW));
    } else {
        motionWidth_ = width_;
    }
    if (height_ > 640u) {
        uint32_t scaledH = ((height_ * 3u / 4u) + 63u) & ~63u;
        motionHeight_ = std::min<uint32_t>(640u, std::max<uint32_t>(384u, scaledH));
    } else {
        motionHeight_ = height_;
    }

    // 1. High-resolution 1x resolve target (width_ x height_)
    WGPUTextureDescriptor td = {};
    td.label         = "TwizzleOffscreenTarget";
    td.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    td.dimension     = WGPUTextureDimension_2D;
    td.size          = {width_, height_, 1};
    td.format        = surfaceFormat_;
    td.mipLevelCount = 1;
    td.sampleCount   = 1;
    renderTargetTexture_ = wgpuDeviceCreateTexture(device_, &td);

    // 2. High-resolution 4x MSAA color attachment (resolved in GPU cache with StoreOp_Discard)
    WGPUTextureDescriptor msaaTd = td;
    msaaTd.label       = "TwizzleOffscreenMSAA4x";
    msaaTd.usage       = WGPUTextureUsage_RenderAttachment;
    msaaTd.sampleCount = 4;
    msaaColorTexture_     = wgpuDeviceCreateTexture(device_, &msaaTd);
    msaaColorTextureView_ = wgpuTextureCreateView(msaaColorTexture_, nullptr);

    // 3. Lightweight 1x motion target (only allocate if dimensions differ from renderTargetTexture_)
    if (motionWidth_ != width_ || motionHeight_ != height_) {
        WGPUTextureDescriptor motionTd = td;
        motionTd.label       = "TwizzleOffscreenMotion1x";
        motionTd.size        = {motionWidth_, motionHeight_, 1};
        motionTd.sampleCount = 1;
        motionTargetTexture_ = wgpuDeviceCreateTexture(device_, &motionTd);
    }

    // Row pitch must be aligned to 256 bytes in WebGPU
    bytesPerRow_       = (width_ * 4 + 255) & ~255;
    bufferSize_        = bytesPerRow_ * height_;
    motionBytesPerRow_ = (motionWidth_ * 4 + 255) & ~255;

    WGPUBufferDescriptor bd = {};
    bd.label = "TwizzleReadbackBuffer";
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bd.size  = bufferSize_;
    readbackBuffer_ = wgpuDeviceCreateBuffer(device_, &bd);
}

// ---------------------------------------------------------------------------
// createDepthTexture
// ---------------------------------------------------------------------------

void WebGpuRenderer::createDepthTexture() {
    if (depthTexture_) {
        wgpuTextureViewRelease(depthTextureView_);
        wgpuTextureRelease(depthTexture_);
        depthTextureView_ = nullptr;
        depthTexture_     = nullptr;
    }
    if (depthTextureMsaa_) {
        wgpuTextureViewRelease(depthTextureViewMsaa_);
        wgpuTextureRelease(depthTextureMsaa_);
        depthTextureViewMsaa_ = nullptr;
        depthTextureMsaa_     = nullptr;
    }
    if (device_) {
        wgpuDevicePoll(device_, true, nullptr);
    }

    const uint32_t dWidth  = isOffscreen_ ? motionWidth_  : width_;
    const uint32_t dHeight = isOffscreen_ ? motionHeight_ : height_;

    WGPUTextureDescriptor td = {};
    td.label         = "depth1x";
    td.usage         = WGPUTextureUsage_RenderAttachment;
    td.dimension     = WGPUTextureDimension_2D;
    td.size          = {dWidth, dHeight, 1};
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

    if (isOffscreen_) {
        WGPUTextureDescriptor msaaDepthTd = td;
        msaaDepthTd.label       = "depth4xMsaa";
        msaaDepthTd.size        = {width_, height_, 1};
        msaaDepthTd.sampleCount = 4;
        depthTextureMsaa_     = wgpuDeviceCreateTexture(device_, &msaaDepthTd);
        depthTextureViewMsaa_ = wgpuTextureCreateView(depthTextureMsaa_, &tvd);
    }
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

    // Align cubie block to 256 bytes (minUniformBufferOffsetAlignment).
    constexpr uint64_t kAlign = 256;
    constexpr uint64_t kFrameAligned = ((kFrameSize + kAlign - 1) / kAlign) * kAlign;
    constexpr uint64_t kCubieAligned = ((kCubieSize + kAlign - 1) / kAlign) * kAlign;
    // 1 frame block + 26 cubie blocks + 1 fixed face letters block
    uint64_t totalSize = kFrameAligned + (26 + 1) * kCubieAligned;

    uniformStagingBuffer_.assign((size_t)totalSize, 0);

    if (uniformBuffer_) wgpuBufferRelease(uniformBuffer_);

    WGPUBufferDescriptor bd = {};
    bd.label            = "uniforms";
    bd.usage            = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    bd.size             = totalSize;
    bd.mappedAtCreation = false;
    uniformBuffer_ = wgpuDeviceCreateBuffer(device_, &bd);

    if (frameBindGroupLayout_ && cubieBindGroupLayout_) {
        rebuildBindGroup();
    }
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

    // For Alpha, preserve destination alpha (1.0 from clear)
    WGPUBlendComponent alphaBlend = {};
    alphaBlend.srcFactor = WGPUBlendFactor_Zero;
    alphaBlend.dstFactor = WGPUBlendFactor_One;
    alphaBlend.operation = WGPUBlendOperation_Add;

    WGPUBlendState blendState = {};
    blendState.color = blendComp;
    blendState.alpha = alphaBlend;

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
    if (frameBindGroupLayout_) wgpuBindGroupLayoutRelease(frameBindGroupLayout_);
    frameBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &bgld0);

    WGPUBindGroupLayoutEntry bgle1 = bgle0;
    WGPUBindGroupLayoutDescriptor bgld1 = {};
    bgld1.entryCount = 1;
    bgld1.entries    = &bgle1;
    if (cubieBindGroupLayout_) wgpuBindGroupLayoutRelease(cubieBindGroupLayout_);
    cubieBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &bgld1);

    WGPUBindGroupLayout bgls[2] = {frameBindGroupLayout_, cubieBindGroupLayout_};
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
    if (hintPipeline_) wgpuRenderPipelineRelease(hintPipeline_);
    hintPipeline_ = wgpuDeviceCreateRenderPipeline(device_, &rpd);

    // ── 2. Foundation Pipeline (Back culling, depth write enabled for opaque)
    WGPUDepthStencilState dssFound = dssHint;
    dssFound.depthWriteEnabled = true;
    dssFound.depthCompare      = WGPUCompareFunction_LessEqual;

    rpd.depthStencil       = &dssFound;
    rpd.primitive.cullMode = WGPUCullMode_Back;
    if (foundPipeline_) wgpuRenderPipelineRelease(foundPipeline_);
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
    if (stickerPipeline_) wgpuRenderPipelineRelease(stickerPipeline_);
    stickerPipeline_ = wgpuDeviceCreateRenderPipeline(device_, &rpd);

    // ── 4x MSAA Pipelines for High-Res Resting Frame in Offscreen Mode ──────
    if (isOffscreen_) {
        rpd.multisample.count  = 4;
        rpd.fragment           = &fs;
        rpd.depthStencil       = &dssHint;
        rpd.primitive.cullMode = WGPUCullMode_Front;
        if (hintPipelineMsaa_) wgpuRenderPipelineRelease(hintPipelineMsaa_);
        hintPipelineMsaa_ = wgpuDeviceCreateRenderPipeline(device_, &rpd);

        rpd.depthStencil       = &dssFound;
        rpd.primitive.cullMode = WGPUCullMode_Back;
        if (foundPipelineMsaa_) wgpuRenderPipelineRelease(foundPipelineMsaa_);
        foundPipelineMsaa_ = wgpuDeviceCreateRenderPipeline(device_, &rpd);

        rpd.fragment           = &fsOpaque;
        rpd.depthStencil       = &dssSticker;
        rpd.primitive.cullMode = WGPUCullMode_Back;
        if (stickerPipelineMsaa_) wgpuRenderPipelineRelease(stickerPipelineMsaa_);
        stickerPipelineMsaa_ = wgpuDeviceCreateRenderPipeline(device_, &rpd);
    }

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shader);

    rebuildBindGroup();

    return (hintPipeline_ != nullptr && foundPipeline_ != nullptr && stickerPipeline_ != nullptr);
}

// ---------------------------------------------------------------------------
// rebuildBindGroup — pre-creates persistent bind groups for frame + 26 cubies
// so renderInternal() performs zero bind group allocations per frame.
// ---------------------------------------------------------------------------

void WebGpuRenderer::rebuildBindGroup() {
    if (!device_ || !uniformBuffer_ || !frameBindGroupLayout_ || !cubieBindGroupLayout_) return;

    if (frameBindGroup_) {
        wgpuBindGroupRelease(frameBindGroup_);
        frameBindGroup_ = nullptr;
    }
    if (faceLettersBindGroup_) {
        wgpuBindGroupRelease(faceLettersBindGroup_);
        faceLettersBindGroup_ = nullptr;
    }
    for (auto bg : cubieBindGroups_) {
        if (bg) wgpuBindGroupRelease(bg);
    }
    cubieBindGroups_.clear();

    constexpr uint64_t kAlign        = 256;
    constexpr uint64_t kFrameSize    = sizeof(float) * 20;
    constexpr uint64_t kFrameAligned = ((kFrameSize + kAlign - 1) / kAlign) * kAlign;
    constexpr uint64_t kCubieSize    = sizeof(float) * 20;
    constexpr uint64_t kCubieAligned = ((kCubieSize + kAlign - 1) / kAlign) * kAlign;

    WGPUBindGroupEntry bge0 = {};
    bge0.binding = 0;
    bge0.buffer  = uniformBuffer_;
    bge0.offset  = 0;
    bge0.size    = kFrameSize;

    WGPUBindGroupDescriptor bgd0 = {};
    bgd0.layout     = frameBindGroupLayout_;
    bgd0.entryCount = 1;
    bgd0.entries    = &bge0;
    frameBindGroup_ = wgpuDeviceCreateBindGroup(device_, &bgd0);

    cubieBindGroups_.resize(cubies_.size(), nullptr);
    for (size_t i = 0; i < cubies_.size(); ++i) {
        uint64_t cubieOffset = kFrameAligned + (uint64_t)i * kCubieAligned;

        WGPUBindGroupEntry bge1 = {};
        bge1.binding = 0;
        bge1.buffer  = uniformBuffer_;
        bge1.offset  = cubieOffset;
        bge1.size    = kCubieSize;

        WGPUBindGroupDescriptor bgd1 = {};
        bgd1.layout     = cubieBindGroupLayout_;
        bgd1.entryCount = 1;
        bgd1.entries    = &bge1;
        cubieBindGroups_[i] = wgpuDeviceCreateBindGroup(device_, &bgd1);
    }

    // Bind group for fixed face letters
    uint64_t lettersOffset = kFrameAligned + (uint64_t)cubies_.size() * kCubieAligned;
    WGPUBindGroupEntry bgeLetters = {};
    bgeLetters.binding = 0;
    bgeLetters.buffer  = uniformBuffer_;
    bgeLetters.offset  = lettersOffset;
    bgeLetters.size    = kCubieSize;

    WGPUBindGroupDescriptor bgdLetters = {};
    bgdLetters.layout     = cubieBindGroupLayout_;
    bgdLetters.entryCount = 1;
    bgdLetters.entries    = &bgeLetters;
    faceLettersBindGroup_ = wgpuDeviceCreateBindGroup(device_, &bgdLetters);
}

// ---------------------------------------------------------------------------
// resize
// ---------------------------------------------------------------------------

void WebGpuRenderer::resize(uint32_t width, uint32_t height) {
    width  = (width  > 0) ? width  : 1;
    height = (height > 0) ? height : 1;
    if (width_ == width && height_ == height) return;
    
    width_  = width;
    height_ = height;
    dirty_  = true;
    camera_.setViewport((int)width_, (int)height_);
    if (!initialized_) return;
    
    if (isOffscreen_) {
        createOffscreenTarget();
    } else {
        configureSurface();
    }
    createDepthTexture();
    if (device_) {
        wgpuDevicePoll(device_, true, nullptr);
    }
}

// ---------------------------------------------------------------------------
// render / renderToBuffer / renderInternal
// ---------------------------------------------------------------------------

void WebGpuRenderer::render(float dt) {
    renderInternal(dt, nullptr);
}

bool WebGpuRenderer::renderToBuffer(float dt, uint8_t* destBuffer) {
    return renderInternal(dt, destBuffer);
}

bool WebGpuRenderer::renderInternal(float dt, uint8_t* destBuffer) {
    if (!initialized_) return false;

    auto frameStartTime = std::chrono::steady_clock::now();
    dirty_ = false;

    float now = nowSeconds();
    camera_.update(dt, now);
    animator_->update(dt);

    // ── Acquire backbuffer texture view & select Dual-Target mode ────────
    WGPUSurfaceTexture st = {};
    WGPUTextureView backbuffer = nullptr;

    const bool inMotion = isOffscreen_ &&
        (dragging_ || camera_.isInertiaActive() || (animator_ && animator_->isPlaying()));

    // Adaptive Quality & Performance profiling:
    // - Ultra mode: NEVER scale down (always maximum resolution + 4x MSAA even during motion)
    // - BatterySaver mode: ALWAYS scale down during motion to save power and shared RAM bandwidth
    // - Auto mode: on Discrete GPUs (NVIDIA/AMD) or fast iGPUs (frame time <= 8ms), NEVER scale down!
    //              Only scale down during motion if running on low-end iGPU/software rasterizer with high frame times.
    bool scaleMotionTarget = false;
    if (inMotion) {
        if (qualityProfile_ == QualityProfile::BatterySaver) {
            scaleMotionTarget = true;
        } else if (qualityProfile_ == QualityProfile::Ultra) {
            scaleMotionTarget = false;
        } else { // Auto
            const bool isDiscrete = (adapterType_ == WGPUAdapterType_DiscreteGPU);
            if (isDiscrete) {
                scaleMotionTarget = false;
            } else {
                scaleMotionTarget = (lastFrameDurationMs_ > 8.0f);
            }
        }
    }

    const bool useMsaa = isOffscreen_ && (!scaleMotionTarget) &&
        msaaColorTextureView_ != nullptr && depthTextureViewMsaa_ != nullptr &&
        hintPipelineMsaa_ != nullptr && foundPipelineMsaa_ != nullptr && stickerPipelineMsaa_ != nullptr;

    WGPUTexture activeOffscreenTex = renderTargetTexture_;
    uint32_t    activeWidth        = width_;
    uint32_t    activeHeight       = height_;
    uint32_t    activeBytesPerRow  = bytesPerRow_;

    if (isOffscreen_) {
        if (scaleMotionTarget) {
            activeOffscreenTex       = motionTargetTexture_ ? motionTargetTexture_ : renderTargetTexture_;
            activeWidth              = motionTargetTexture_ ? motionWidth_         : width_;
            activeHeight             = motionTargetTexture_ ? motionHeight_        : height_;
            activeBytesPerRow        = motionTargetTexture_ ? motionBytesPerRow_   : bytesPerRow_;
            needsHighResSettleFrame_ = true;
        } else {
            activeOffscreenTex       = renderTargetTexture_;
            activeWidth              = width_;
            activeHeight             = height_;
            activeBytesPerRow        = bytesPerRow_;
            needsHighResSettleFrame_ = false;
        }
        if (!activeOffscreenTex) { dirty_ = true; return false; }
        backbuffer = wgpuTextureCreateView(activeOffscreenTex, nullptr);
    } else {
        wgpuSurfaceGetCurrentTexture(surface_, &st);
        if (st.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
            if (st.texture) {
                wgpuTextureRelease(st.texture);
                st.texture = nullptr;
            }
            if (st.status == WGPUSurfaceGetCurrentTextureStatus_Outdated ||
                st.status == WGPUSurfaceGetCurrentTextureStatus_Lost) {
                configureSurface();
            }
            dirty_ = true;
            return false;
        }
        backbuffer = wgpuTextureCreateView(st.texture, nullptr);
    }

    if (!backbuffer) {
        if (st.texture) {
            wgpuTextureRelease(st.texture);
        }
        dirty_ = true;
        return false;
    }

    // ── Build VP + Cubie uniform data in a single staging buffer ────────
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

    uint64_t requiredSize = kFrameAligned + (uint64_t)(cubies_.size() + 1) * kCubieAligned;
    if (uniformStagingBuffer_.size() < requiredSize) {
        uniformStagingBuffer_.resize((size_t)requiredSize, 0);
    }

    std::memcpy(uniformStagingBuffer_.data(), &fb, kFrameSize);

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
        std::memcpy(uniformStagingBuffer_.data() + offset, &cb, kCubieSize);
    }

    // Staging data for fixed face letters (identity model transform so they stay fixed)
    uint64_t letterOffset = kFrameAligned + (uint64_t)cubies_.size() * kCubieAligned;
    struct {
        float model[16];
        float params[4];
    } letterCb;
    glm::mat4 letterModel = glm::mat4(1.0f);
    std::memcpy(letterCb.model, glm::value_ptr(letterModel), sizeof(letterCb.model));
    letterCb.params[0] = 1.0f;
    letterCb.params[1] = letterCb.params[2] = letterCb.params[3] = 0.0f;
    std::memcpy(uniformStagingBuffer_.data() + letterOffset, &letterCb, kCubieSize);

    // Single consolidated buffer upload per frame instead of 27 calls
    wgpuQueueWriteBuffer(queue_, uniformBuffer_, 0,
                         uniformStagingBuffer_.data(), requiredSize);

    // ── Command encoder ─────────────────────────────────────────────────
    WGPUCommandEncoderDescriptor ced = {}; ced.label = "frame";
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &ced);

    WGPURenderPassColorAttachment ca = {};
    if (useMsaa) {
        // 4x MSAA hardware resolve into backbuffer; discard 4x samples in GPU tile/L3 cache
        ca.view          = msaaColorTextureView_;
        ca.resolveTarget = backbuffer;
        ca.loadOp        = WGPULoadOp_Clear;
        ca.storeOp       = WGPUStoreOp_Discard;
    } else {
        ca.view          = backbuffer;
        ca.resolveTarget = nullptr;
        ca.loadOp        = WGPULoadOp_Clear;
        ca.storeOp       = WGPUStoreOp_Store;
    }
    ca.clearValue = {bgR_, bgG_, bgB_, bgA_};

    WGPURenderPassDepthStencilAttachment da = {};
    da.view              = useMsaa ? depthTextureViewMsaa_ : depthTextureView_;
    da.depthLoadOp       = WGPULoadOp_Clear;
    da.depthStoreOp      = useMsaa ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
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

    WGPURenderPipeline activeHintPipe    = useMsaa ? hintPipelineMsaa_    : hintPipeline_;
    WGPURenderPipeline activeFoundPipe   = useMsaa ? foundPipelineMsaa_   : foundPipeline_;
    WGPURenderPipeline activeStickerPipe = useMsaa ? stickerPipelineMsaa_ : stickerPipeline_;

    // ── Pass 1: Hints (only if showHint_ is true) ───────────────────────────
    if (showHint_) {
        wgpuRenderPassEncoderSetPipeline(pass, activeHintPipe);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, frameBindGroup_, 0, nullptr);
        for (size_t i = 0; i < cubies_.size(); ++i) {
            const CubieInfo& c = cubies_[i];
            if (c.hintCount == 0) continue;
            wgpuRenderPassEncoderSetBindGroup(pass, 1, cubieBindGroups_[i], 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, (uint32_t)c.hintCount, 1,
                                      (uint32_t)c.hintOffset, 0);
        }
    }

    // ── Pass 2: Foundation (black cubie bodies) ─────────────────────────────
    wgpuRenderPassEncoderSetPipeline(pass, activeFoundPipe);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, frameBindGroup_, 0, nullptr);
    for (size_t i = 0; i < cubies_.size(); ++i) {
        const CubieInfo& c = cubies_[i];
        if (c.foundCount == 0) continue;
        wgpuRenderPassEncoderSetBindGroup(pass, 1, cubieBindGroups_[i], 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, (uint32_t)c.foundCount, 1,
                                      (uint32_t)c.foundOffset, 0);
    }

    // ── Pass 3: Stickers (opaque coloured quads) ────────────────────────────
    wgpuRenderPassEncoderSetPipeline(pass, activeStickerPipe);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, frameBindGroup_, 0, nullptr);
    for (size_t i = 0; i < cubies_.size(); ++i) {
        const CubieInfo& c = cubies_[i];
        if (c.stickerCount == 0) continue;
        wgpuRenderPassEncoderSetBindGroup(pass, 1, cubieBindGroups_[i], 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, (uint32_t)c.stickerCount, 1,
                                  (uint32_t)c.stickerOffset, 0);
    }

    // ── Pass 4: Fixed Face Letters (U, D, L, R, F, B) ──────────────────────
    if (showFaceLetters_ && letterCount_ > 0 && faceLettersBindGroup_) {
        wgpuRenderPassEncoderSetPipeline(pass, activeStickerPipe);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, frameBindGroup_, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, faceLettersBindGroup_, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, (uint32_t)letterCount_, 1,
                                  (uint32_t)letterOffset_, 0);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    const uint32_t activeBufferSize = activeBytesPerRow * activeHeight;

    if (isOffscreen_) {
        WGPUImageCopyTexture src = {};
        src.texture = activeOffscreenTex;
        src.mipLevel = 0;
        src.origin = {0, 0, 0};
        src.aspect = WGPUTextureAspect_All;

        WGPUImageCopyBuffer dst = {};
        dst.buffer = readbackBuffer_;
        dst.layout.offset = 0;
        dst.layout.bytesPerRow = activeBytesPerRow;
        dst.layout.rowsPerImage = activeHeight;

        WGPUExtent3D copySize = {activeWidth, activeHeight, 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &copySize);
    }

    WGPUCommandBufferDescriptor cbd = {}; cbd.label = "frame_cmd";
    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, &cbd);
    wgpuQueueSubmit(queue_, 1, &cmdBuf);
    wgpuCommandBufferRelease(cmdBuf);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(backbuffer);

    if (isOffscreen_) {
        if (destBuffer && readbackBuffer_) {
            struct MapContext { bool done = false; bool success = false; };
            MapContext mapCtx;
            wgpuBufferMapAsync(readbackBuffer_, WGPUMapMode_Read, 0, activeBufferSize,
                [](WGPUBufferMapAsyncStatus status, void* ud) {
                    auto* c = reinterpret_cast<MapContext*>(ud);
                    c->done = true;
                    c->success = (status == WGPUBufferMapAsyncStatus_Success);
                }, &mapCtx);

            wgpuDevicePoll(device_, true, nullptr);

            if (mapCtx.done && mapCtx.success) {
                const uint8_t* mapped = (const uint8_t*)wgpuBufferGetConstMappedRange(readbackBuffer_, 0, activeBufferSize);
                if (mapped) {
                    if (activeBytesPerRow == activeWidth * 4) {
                        std::memcpy(destBuffer, mapped, activeWidth * activeHeight * 4);
                    } else {
                        for (uint32_t y = 0; y < activeHeight; ++y) {
                            std::memcpy(destBuffer + y * activeWidth * 4, mapped + y * activeBytesPerRow, activeWidth * 4);
                        }
                    }
                    wgpuBufferUnmap(readbackBuffer_);
                    lastRenderedWidth_  = activeWidth;
                    lastRenderedHeight_ = activeHeight;

                    auto frameEndTime = std::chrono::steady_clock::now();
                    lastFrameDurationMs_ = std::chrono::duration<float, std::milli>(frameEndTime - frameStartTime).count();
                    return true;
                }
                wgpuBufferUnmap(readbackBuffer_);
            }
        }
        return false;
    } else {
        wgpuSurfacePresent(surface_);
        wgpuTextureRelease(st.texture);
        return true;
    }
}

// ---------------------------------------------------------------------------
// destroy
// ---------------------------------------------------------------------------

void WebGpuRenderer::destroy() {
    initialized_ = false;
#if defined(__ANDROID__)
    wgpuSetLogCallback(nullptr, nullptr);
#endif
    if (device_) {
        wgpuDevicePoll(device_, true, nullptr);
    }
    if (frameBindGroup_) { wgpuBindGroupRelease(frameBindGroup_); frameBindGroup_ = nullptr; }
    if (faceLettersBindGroup_) { wgpuBindGroupRelease(faceLettersBindGroup_); faceLettersBindGroup_ = nullptr; }
    for (auto bg : cubieBindGroups_) {
        if (bg) wgpuBindGroupRelease(bg);
    }
    cubieBindGroups_.clear();
    cubieBindGroups_.shrink_to_fit();
    if (frameBindGroupLayout_) { wgpuBindGroupLayoutRelease(frameBindGroupLayout_); frameBindGroupLayout_ = nullptr; }
    if (cubieBindGroupLayout_) { wgpuBindGroupLayoutRelease(cubieBindGroupLayout_); cubieBindGroupLayout_ = nullptr; }
    if (msaaColorTextureView_) { wgpuTextureViewRelease(msaaColorTextureView_); msaaColorTextureView_ = nullptr; }
    if (msaaColorTexture_)     { wgpuTextureRelease(msaaColorTexture_);         msaaColorTexture_     = nullptr; }
    if (motionTargetTexture_)  { wgpuTextureRelease(motionTargetTexture_);      motionTargetTexture_  = nullptr; }
    if (renderTargetTexture_)  { wgpuTextureRelease(renderTargetTexture_);      renderTargetTexture_  = nullptr; }
    if (readbackBuffer_)       { wgpuBufferRelease(readbackBuffer_);            readbackBuffer_       = nullptr; }
    if (depthTextureViewMsaa_) { wgpuTextureViewRelease(depthTextureViewMsaa_); depthTextureViewMsaa_ = nullptr; }
    if (depthTextureMsaa_)     { wgpuTextureRelease(depthTextureMsaa_);         depthTextureMsaa_     = nullptr; }
    if (depthTextureView_)     { wgpuTextureViewRelease(depthTextureView_);     depthTextureView_     = nullptr; }
    if (depthTexture_)         { wgpuTextureRelease(depthTexture_);             depthTexture_         = nullptr; }
    if (uniformBuffer_)        { wgpuBufferRelease(uniformBuffer_);             uniformBuffer_        = nullptr; }
    if (vertexBuffer_)         { wgpuBufferRelease(vertexBuffer_);              vertexBuffer_         = nullptr; }
    if (hintPipelineMsaa_)     { wgpuRenderPipelineRelease(hintPipelineMsaa_);  hintPipelineMsaa_     = nullptr; }
    if (foundPipelineMsaa_)    { wgpuRenderPipelineRelease(foundPipelineMsaa_); foundPipelineMsaa_    = nullptr; }
    if (stickerPipelineMsaa_)  { wgpuRenderPipelineRelease(stickerPipelineMsaa_); stickerPipelineMsaa_ = nullptr; }
    if (hintPipeline_)         { wgpuRenderPipelineRelease(hintPipeline_);      hintPipeline_         = nullptr; }
    if (foundPipeline_)        { wgpuRenderPipelineRelease(foundPipeline_);     foundPipeline_        = nullptr; }
    if (stickerPipeline_)      { wgpuRenderPipelineRelease(stickerPipeline_);   stickerPipeline_      = nullptr; }
    if (device_) {
        wgpuDevicePoll(device_, true, nullptr);
    }
    if (usesSharedDevice_) {
        queue_            = nullptr;
        device_           = nullptr;
        adapter_          = nullptr;
        instance_         = nullptr;
        usesSharedDevice_ = false;
    } else {
        if (queue_)    { wgpuQueueRelease(queue_);       queue_    = nullptr; }
        if (device_)   { wgpuDeviceRelease(device_);     device_   = nullptr; }
        if (adapter_)  { wgpuAdapterRelease(adapter_);   adapter_  = nullptr; }
        if (surface_)  { wgpuSurfaceRelease(surface_);   surface_  = nullptr; }
        if (instance_) { wgpuInstanceRelease(instance_); instance_ = nullptr; }
    }

    vertices_.clear();
    vertices_.shrink_to_fit();
    cubies_.clear();
    cubies_.shrink_to_fit();
    uniformStagingBuffer_.clear();
    uniformStagingBuffer_.shrink_to_fit();
    animator_.reset();
}

// ---------------------------------------------------------------------------
// Camera input
// ---------------------------------------------------------------------------

void WebGpuRenderer::onDragBegin(float x, float y) {
    dragging_ = true;
    dirty_    = true;
    lastDragX_ = x;
    lastDragY_ = y;
    camera_.beginDrag(nowSeconds());
}

void WebGpuRenderer::onDragMove(float x, float y) {
    if (dragging_) {
        camera_.drag(x - lastDragX_, y - lastDragY_);
        lastDragX_ = x;
        lastDragY_ = y;
        dirty_     = true;
    }
}

void WebGpuRenderer::onDragEnd() {
    dragging_ = false;
    dirty_    = true;
    camera_.endDrag(nowSeconds());
}

void WebGpuRenderer::onDrag(float dx, float dy) {
    camera_.drag(dx, dy);
    dirty_ = true;
}

void WebGpuRenderer::onZoom(float delta) {
    camera_.zoom(delta);
    dirty_ = true;
}

// ---------------------------------------------------------------------------
// Move timeline
// ---------------------------------------------------------------------------

void WebGpuRenderer::applyMove(const twizzle::Move& move) {
    cube_.applyMove(move);
    animator_->queueMove(move);
    dirty_ = true;
}

void WebGpuRenderer::applyAlgorithm(const std::string& algStr) {
    auto moves = twizzle::AlgorithmParser::parse(algStr);
    for (const auto& m : moves) applyMove(m);
    dirty_ = true;
}

void WebGpuRenderer::reset() {
    cube_.reset();
    animator_->clear();
    CubeGeometry::build(vertices_, cubies_);
    CubeGeometry::buildFaceLetters(vertices_, letterOffset_, letterCount_);
    if (initialized_) uploadVertexBuffer();
    dirty_ = true;
}

// ── Transport ────────────────────────────────────────────────────────────
void  WebGpuRenderer::play()           { animator_->play();         dirty_ = true; }
void  WebGpuRenderer::pause()          { animator_->pause();        dirty_ = true; }
void  WebGpuRenderer::togglePlay()     { animator_->togglePlay();   dirty_ = true; }
void  WebGpuRenderer::jumpToStart()    { animator_->jumpToStart();  dirty_ = true; }
void  WebGpuRenderer::jumpToEnd()      { animator_->jumpToEnd();    dirty_ = true; }
void  WebGpuRenderer::stepForward()    { animator_->stepForward();  dirty_ = true; }
void  WebGpuRenderer::stepBackward()   { animator_->stepBackward(); dirty_ = true; }

// ── Scrubbing ─────────────────────────────────────────────────────────────
void  WebGpuRenderer::seekFraction(float f)  { animator_->seekFraction(f); dirty_ = true; }
void  WebGpuRenderer::seekMs(float ms)       { animator_->seekMs(ms);      dirty_ = true; }
float WebGpuRenderer::currentFraction() const { return animator_->currentFraction(); }
float WebGpuRenderer::currentMs()       const { return animator_->currentMs(); }
float WebGpuRenderer::totalDurationMs() const { return animator_->totalDurationMs(); }
bool  WebGpuRenderer::isPlaying()       const { return animator_->isPlaying(); }
bool  WebGpuRenderer::isAnimating()     const { return animator_->isAnimating(); }

bool  WebGpuRenderer::needsRender() const {
    if (!initialized_) return false;
    return dirty_ || needsHighResSettleFrame_ || dragging_ || camera_.isInertiaActive() || (animator_ && animator_->isPlaying());
}

// ── Visual ────────────────────────────────────────────────────────────────
void WebGpuRenderer::setSpeed(float s)                         { animator_->setSpeed(s); dirty_ = true; }
float WebGpuRenderer::getSpeed() const                         { return animator_->getSpeed(); }
void WebGpuRenderer::setBackgroundColor(float r, float g, float b, float a) {
    bgR_ = r; bgG_ = g; bgB_ = b; bgA_ = a;
    dirty_ = true;
}
void WebGpuRenderer::setCameraPosition(float lat, float lon, float radius) {
    camera_.setPosition(lat, lon, radius);
    dirty_ = true;
}
void WebGpuRenderer::setPitchLock(bool locked) {
    camera_.setPitchLock(locked);
    dirty_ = true;
}
void WebGpuRenderer::setShowHint(bool show) {
    showHint_ = show;
    dirty_ = true;
}
void WebGpuRenderer::setShowFaceLetters(bool show) {
    showFaceLetters_ = show;
    dirty_ = true;
}
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
    CubeGeometry::buildFaceLetters(vertices_, letterOffset_, letterCount_);
    if (initialized_) uploadVertexBuffer();
    dirty_ = true;
}

void WebGpuRenderer::setBodyAlpha(float alpha) {
    CubeGeometry::setBodyAlpha(alpha);
    CubeGeometry::build(vertices_, cubies_);
    CubeGeometry::buildFaceLetters(vertices_, letterOffset_, letterCount_);
    if (initialized_) uploadVertexBuffer();
    dirty_ = true;
}

void WebGpuRenderer::setQualityProfile(QualityProfile profile) {
    qualityProfile_ = profile;
    dirty_ = true;
}

float WebGpuRenderer::nowSeconds() { return nowSec(); }

} // namespace twizzle::renderer
