// =============================================================================
// twizzledart_plugin.cpp — Windows Flutter plugin for TwizzleDart (WebGPU)
//
// Architecture:
//   Dart  ─MethodChannel("twizzledart")──► OnCreateView
//                                           └─ registers Flutter Texture
//                                           └─ starts RenderLoop thread
//                                           └─ opens per-view MethodChannel
//
//   RenderLoop (background thread, ~60 fps)
//     WebGpuRenderer::render(dt)
//       └─ renders to offscreen BGRA8 pixel buffer
//     TextureVariant::PixelBufferTexture::CopyPixelBuffer callback
//       └─ Flutter reads the buffer on its compositor thread (zero-copy via ptr)
//
// The PixelBufferTexture approach is the simplest Windows texture path:
//   flutter::TextureVariant  ← wraps a PixelBufferTexture callback
//   FlutterDesktopPixelBuffer ← {data ptr, width, height}
// Flutter calls CopyPixelBuffer() every time it composites the Texture widget.
// =============================================================================
#include "twizzledart_plugin.h"

#include <flutter/encodable_value.h>
#include <flutter/method_result_functions.h>
#include <flutter/texture_registrar.h>

#include <chrono>
#include <cstring>
#include <sstream>

// WebGPU renderer (native/)
#include "renderer/WebGpuRenderer.hpp"

namespace twizzledart {

using flutter::EncodableMap;
using flutter::EncodableValue;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static double GetDouble(const EncodableMap& map, const std::string& key,
                        double def = 0.0) {
    auto it = map.find(EncodableValue(key));
    if (it == map.end()) return def;
    if (auto* d = std::get_if<double>(&it->second)) return *d;
    if (auto* i = std::get_if<int32_t>(&it->second)) return (double)*i;
    return def;
}

static bool GetBool(const EncodableMap& map, const std::string& key,
                    bool def = false) {
    auto it = map.find(EncodableValue(key));
    if (it == map.end()) return def;
    if (auto* b = std::get_if<bool>(&it->second)) return *b;
    return def;
}

static std::string GetString(const EncodableMap& map, const std::string& key,
                              const std::string& def = "") {
    auto it = map.find(EncodableValue(key));
    if (it == map.end()) return def;
    if (auto* s = std::get_if<std::string>(&it->second)) return *s;
    return def;
}

static const EncodableMap* GetMap(const EncodableValue& value) {
    return std::get_if<EncodableMap>(&value);
}

// ---------------------------------------------------------------------------
// RegisterWithRegistrar
// ---------------------------------------------------------------------------

// static
void TwizzledartPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
    auto plugin = std::make_unique<TwizzledartPlugin>(registrar);
    registrar->AddPlugin(std::move(plugin));
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

TwizzledartPlugin::TwizzledartPlugin(flutter::PluginRegistrarWindows* registrar)
    : registrar_(registrar) {

    channel_ = std::make_unique<flutter::MethodChannel<EncodableValue>>(
        registrar->messenger(),
        "twizzledart",
        &flutter::StandardMethodCodec::GetInstance());

    channel_->SetMethodCallHandler(
        [this](const auto& call, auto result) {
            HandleMethodCall(call, std::move(result));
        });
}

TwizzledartPlugin::~TwizzledartPlugin() {
    // Stop all render loops
    std::lock_guard<std::mutex> lock(instancesMutex_);
    for (auto& [id, inst] : instances_) {
        inst->running = false;
        if (inst->renderThread.joinable()) inst->renderThread.join();
    }
}

// ---------------------------------------------------------------------------
// Main channel — createView / disposeView
// ---------------------------------------------------------------------------

void TwizzledartPlugin::HandleMethodCall(
    const flutter::MethodCall<EncodableValue>& call,
    std::unique_ptr<flutter::MethodResult<EncodableValue>> result) {

    if (call.method_name() == "createView") {
        OnCreateView(call, std::move(result));
    } else if (call.method_name() == "disposeView") {
        const auto* args = GetMap(*call.arguments());
        int64_t texId = args
            ? (int64_t)GetDouble(*args, "textureId", -1)
            : -1;
        OnDisposeView(texId);
        result->Success();
    } else {
        result->NotImplemented();
    }
}

// ---------------------------------------------------------------------------
// OnCreateView — registers a Flutter Texture + starts the render loop
// ---------------------------------------------------------------------------

void TwizzledartPlugin::OnCreateView(
    const flutter::MethodCall<EncodableValue>& call,
    std::unique_ptr<flutter::MethodResult<EncodableValue>> result) {

    const auto* args = GetMap(*call.arguments());
    if (!args) { result->Error("INVALID_ARGS", "Expected map"); return; }

    uint32_t width  = (uint32_t)GetDouble(*args, "width",  400);
    uint32_t height = (uint32_t)GetDouble(*args, "height", 400);

    // Parse creation params (mirrors Android TwizzledartPlugin)
    std::string initialAlgorithm = GetString(*args, "initialAlgorithm");
    double speed                 = GetDouble(*args, "speed",       1.0);
    bool   touchEnabled          = GetBool  (*args, "touchEnabled", true);
    bool   pitchLock             = GetBool  (*args, "pitchLock",    true);
    double bodyAlpha             = GetDouble(*args, "bodyAlpha",    1.0);

    // Background colour
    double bgR = 0.08, bgG = 0.08, bgB = 0.10;
    auto bgIt = args->find(EncodableValue("backgroundColor"));
    if (bgIt != args->end()) {
        if (const auto* bgMap = GetMap(bgIt->second)) {
            bgR = GetDouble(*bgMap, "r", 0.08);
            bgG = GetDouble(*bgMap, "g", 0.08);
            bgB = GetDouble(*bgMap, "b", 0.10);
        }
    }

    // Face colours (18 floats → 6 x RGB)
    float faceColors[6][3] = {};
    bool  hasFaceColors = false;
    auto fcIt = args->find(EncodableValue("faceColors"));
    if (fcIt != args->end()) {
        if (const auto* fcList = std::get_if<std::vector<EncodableValue>>(&fcIt->second)) {
            if (fcList->size() >= 18) {
                hasFaceColors = true;
                for (int i = 0; i < 6; ++i)
                for (int j = 0; j < 3; ++j) {
                    int idx = i * 3 + j;
                    if (auto* d = std::get_if<double>(&(*fcList)[idx]))
                        faceColors[i][j] = (float)*d;
                }
            }
        }
    }

    // Camera position
    double camLat = 35.0, camLon = 30.0, camRad = 6.0;
    auto camIt = args->find(EncodableValue("cameraPosition"));
    if (camIt != args->end()) {
        if (const auto* camMap = GetMap(camIt->second)) {
            camLat = GetDouble(*camMap, "latitude",  35.0);
            camLon = GetDouble(*camMap, "longitude", 30.0);
            camRad = GetDouble(*camMap, "radius",     6.0);
        }
    }

    // ── Create the instance ──────────────────────────────────────────────
    auto inst = std::make_unique<CubeViewInstance>();
    inst->bufferWidth  = width;
    inst->bufferHeight = height;
    inst->pixelBuffer  = std::make_unique<uint8_t[]>(width * height * 4);

    // ── Register Flutter PixelBufferTexture ──────────────────────────────
    // Flutter calls this callback to pull each frame from our pixel buffer.
    auto* rawInst = inst.get();
    auto textureVariant = std::make_unique<flutter::TextureVariant>(
        flutter::PixelBufferTexture(
            [rawInst](size_t w, size_t h) -> const FlutterDesktopPixelBuffer* {
                static FlutterDesktopPixelBuffer buf{};
                std::lock_guard<std::mutex> lock(rawInst->rendererMutex);
                buf.buffer   = rawInst->pixelBuffer.get();
                buf.width    = rawInst->bufferWidth;
                buf.height   = rawInst->bufferHeight;
                return &buf;
            }));

    inst->textureId = registrar_->texture_registrar()->RegisterTexture(
        textureVariant.get());

    // Keep textureVariant alive (Flutter holds a raw ptr to it)
    // We store it inside the instance via a generic holder.
    // Since TextureVariant must outlive the texture registration, we move it
    // into a heap-allocated unique_ptr stored as void* (erased type).
    struct TextureHolder { std::unique_ptr<flutter::TextureVariant> variant; };
    auto* holder = new TextureHolder{std::move(textureVariant)};
    // Attach cleanup to the instance destructor via a lambda-captured deleter.
    // We'll release it in OnDisposeView.
    inst->renderer = nullptr; // will be set below

    // ── Initialise WebGpuRenderer (offscreen, no native window) ──────────
    // On Windows we use wgpu-native's "windowless" / headless surface path:
    // create an offscreen surface backed by a D3D11 texture, render into it,
    // then use wgpuBufferMapAsync to readback pixels each frame into our
    // pixelBuffer. For initial simplicity we use software-emulated readback
    // (wgpu's DX12 backend + mapped staging buffer).
    {
        auto renderer = std::make_unique<twizzle::renderer::WebGpuRenderer>();

        // Get HWND of the Flutter window (for D3D surface creation)
        HWND hwnd = registrar_->GetView()->GetNativeWindow();
        HINSTANCE hInst = GetModuleHandle(nullptr);

        twizzle::renderer::SurfaceDescriptor surfDesc;
        surfDesc.hwnd      = hwnd;
        surfDesc.hinstance = hInst;
        surfDesc.width     = width;
        surfDesc.height    = height;

        if (renderer->init(surfDesc)) {
            renderer->setBackgroundColor((float)bgR, (float)bgG, (float)bgB, 1.0f);
            renderer->setSpeed((float)speed);
            renderer->setPitchLock(pitchLock);
            renderer->setCameraPosition((float)camLat, (float)camLon, (float)camRad);
            renderer->setBodyAlpha((float)bodyAlpha);
            if (hasFaceColors) renderer->setFaceColors(faceColors);
            if (!initialAlgorithm.empty()) renderer->applyAlgorithm(initialAlgorithm);
        }

        inst->renderer = std::move(renderer);
    }

    // ── Per-view MethodChannel ───────────────────────────────────────────
    std::string viewChannelName = "twizzledart_view_" + std::to_string(inst->textureId);
    inst->viewChannel = std::make_unique<flutter::MethodChannel<EncodableValue>>(
        registrar_->messenger(),
        viewChannelName,
        &flutter::StandardMethodCodec::GetInstance());

    int64_t texId = inst->textureId;
    inst->viewChannel->SetMethodCallHandler(
        [this, texId](const auto& viewCall, auto viewResult) {
            HandleViewMethodCall(texId, viewCall, std::move(viewResult));
        });

    // ── Render loop thread ───────────────────────────────────────────────
    inst->running = true;
    inst->renderThread = std::thread([this, rawInst]() {
        RenderLoop(rawInst);
    });

    // Store holder raw ptr for cleanup (simple approach)
    // We intentionally leak the TextureHolder until OnDisposeView because
    // Flutter holds a raw ptr to the TextureVariant. In production this should
    // be a proper shared_ptr or Flutter texture lifecycle hook.
    (void)holder; // TODO: store in inst for proper cleanup

    int64_t registeredId = inst->textureId;

    {
        std::lock_guard<std::mutex> lock(instancesMutex_);
        instances_[registeredId] = std::move(inst);
    }

    // Return textureId to Dart
    result->Success(EncodableValue(registeredId));
}

// ---------------------------------------------------------------------------
// OnDisposeView
// ---------------------------------------------------------------------------

void TwizzledartPlugin::OnDisposeView(int64_t textureId) {
    std::unique_ptr<CubeViewInstance> inst;
    {
        std::lock_guard<std::mutex> lock(instancesMutex_);
        auto it = instances_.find(textureId);
        if (it == instances_.end()) return;
        inst = std::move(it->second);
        instances_.erase(it);
    }

    inst->running = false;
    if (inst->renderThread.joinable()) inst->renderThread.join();

    registrar_->texture_registrar()->UnregisterTexture(textureId);
    if (inst->renderer) inst->renderer->destroy();
}

// ---------------------------------------------------------------------------
// RenderLoop — background thread: ~60 fps render + Flutter texture mark dirty
// ---------------------------------------------------------------------------

void TwizzledartPlugin::RenderLoop(CubeViewInstance* inst) {
    using Clock = std::chrono::steady_clock;
    auto prev   = Clock::now();

    constexpr float kTargetFps  = 60.0f;
    constexpr int   kFrameMs    = (int)(1000.0f / kTargetFps);

    while (inst->running) {
        auto now = Clock::now();
        float dt = std::chrono::duration<float>(now - prev).count();
        prev = now;

        {
            std::lock_guard<std::mutex> lock(inst->rendererMutex);
            if (inst->renderer) {
                // Render one frame — writes into the wgpu surface texture.
                inst->renderer->render(dt);

                // NOTE: Pixel readback from the GPU surface into inst->pixelBuffer
                // will be added in a follow-up step using a wgpu staging buffer.
                // For now the loop runs to drive animations (WebGPU presents to
                // an offscreen surface); the actual Flutter Texture integration
                // requires mapping a WGPUBuffer via wgpuBufferMapAsync and
                // memcpy-ing into pixelBuffer before marking the texture dirty.
            }
        }

        // Tell Flutter the texture has new data
        registrar_->texture_registrar()->MarkTextureFrameAvailable(
            inst->textureId);

        // Sleep to target ~60 fps
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - prev).count();
        int sleepMs = kFrameMs - (int)elapsed;
        if (sleepMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }
    }
}

// ---------------------------------------------------------------------------
// HandleViewMethodCall — per-view commands (mirrors Android MethodChannel API)
// ---------------------------------------------------------------------------

void TwizzledartPlugin::HandleViewMethodCall(
    int64_t textureId,
    const flutter::MethodCall<EncodableValue>& call,
    std::unique_ptr<flutter::MethodResult<EncodableValue>> result) {

    CubeViewInstance* inst = FindInstance(textureId);
    if (!inst) { result->Error("NOT_FOUND", "View not found"); return; }

    std::lock_guard<std::mutex> lock(inst->rendererMutex);
    auto* r = inst->renderer.get();

    const std::string& method = call.method_name();
    const auto* args = call.arguments()
        ? GetMap(*call.arguments())
        : nullptr;

    // ── Algorithm & state ────────────────────────────────────────────────
    if (method == "applyAlgorithm") {
        std::string alg = args ? GetString(*args, "algorithm") : "";
        if (r && !alg.empty()) r->applyAlgorithm(alg);
        result->Success();

    } else if (method == "reset") {
        if (r) r->reset();
        result->Success();

    // ── Transport ────────────────────────────────────────────────────────
    } else if (method == "play") {
        if (r) r->play();
        result->Success();
    } else if (method == "pause") {
        if (r) r->pause();
        result->Success();
    } else if (method == "stepForward") {
        if (r) r->stepForward();
        result->Success();
    } else if (method == "stepBackward") {
        if (r) r->stepBackward();
        result->Success();

    // ── Scrubbing ────────────────────────────────────────────────────────
    } else if (method == "seekFraction") {
        double f = args ? GetDouble(*args, "fraction") : 0.0;
        if (r) r->seekFraction((float)f);
        result->Success();
    } else if (method == "getCurrentFraction") {
        double frac = r ? r->currentFraction() : 0.0;
        result->Success(EncodableValue(frac));

    // ── Speed ────────────────────────────────────────────────────────────
    } else if (method == "setSpeed") {
        double s = args ? GetDouble(*args, "speed", 1.0) : 1.0;
        if (r) r->setSpeed((float)s);
        result->Success();
    } else if (method == "getSpeed") {
        double s = r ? r->getSpeed() : 1.0;
        result->Success(EncodableValue(s));

    // ── State queries ────────────────────────────────────────────────────
    } else if (method == "isAnimating") {
        bool v = r && r->isAnimating();
        result->Success(EncodableValue(v));
    } else if (method == "isPlaying") {
        bool v = r && r->isPlaying();
        result->Success(EncodableValue(v));

    // ── Visual properties ─────────────────────────────────────────────────
    } else if (method == "setBackgroundColor") {
        if (r && args) {
            float rr = (float)GetDouble(*args, "r", 0.08);
            float gg = (float)GetDouble(*args, "g", 0.08);
            float bb = (float)GetDouble(*args, "b", 0.10);
            float aa = (float)GetDouble(*args, "a", 1.0);
            r->setBackgroundColor(rr, gg, bb, aa);
        }
        result->Success();

    } else if (method == "setCameraPosition") {
        if (r && args) {
            float lat = (float)GetDouble(*args, "latitude",  35.0);
            float lon = (float)GetDouble(*args, "longitude", 30.0);
            float rad = (float)GetDouble(*args, "radius",     6.0);
            r->setCameraPosition(lat, lon, rad);
        }
        result->Success();

    } else if (method == "setTouchEnabled") {
        // Touch/mouse gestures are handled by Flutter gesture detectors in Dart;
        // this flag is a no-op on Windows (gestures forwarded via drag methods).
        result->Success();

    } else if (method == "setPitchLock") {
        bool locked = args ? GetBool(*args, "locked", true) : true;
        if (r) r->setPitchLock(locked);
        result->Success();

    } else if (method == "setShowHint") {
        bool enabled = args ? GetBool(*args, "enabled", false) : false;
        if (r) r->setShowHint(enabled);
        result->Success();

    } else if (method == "setBodyAlpha") {
        double alpha = args ? GetDouble(*args, "alpha", 1.0) : 1.0;
        if (r) r->setBodyAlpha((float)alpha);
        result->Success();

    } else if (method == "setFaceColors") {
        if (r && args) {
            auto fcIt = args->find(EncodableValue("colors"));
            if (fcIt != args->end()) {
                if (const auto* list = std::get_if<std::vector<EncodableValue>>(&fcIt->second)) {
                    if (list->size() >= 18) {
                        float fc[6][3] = {};
                        for (int i = 0; i < 6; ++i)
                        for (int j = 0; j < 3; ++j) {
                            if (auto* d = std::get_if<double>(&(*list)[i*3+j]))
                                fc[i][j] = (float)*d;
                        }
                        r->setFaceColors(fc);
                    }
                }
            }
        }
        result->Success();

    // ── Mouse drag forwarding (from Dart GestureDetector) ────────────────
    } else if (method == "onDragBegin") {
        if (r && args) {
            float x = (float)GetDouble(*args, "x");
            float y = (float)GetDouble(*args, "y");
            r->onDragBegin(x, y);
        }
        result->Success();
    } else if (method == "onDragMove") {
        if (r && args) {
            float x = (float)GetDouble(*args, "x");
            float y = (float)GetDouble(*args, "y");
            r->onDragMove(x, y);
        }
        result->Success();
    } else if (method == "onDragEnd") {
        if (r) r->onDragEnd();
        result->Success();
    } else if (method == "onZoom") {
        if (r && args) {
            float delta = (float)GetDouble(*args, "delta");
            r->onZoom(delta);
        }
        result->Success();

    } else {
        result->NotImplemented();
    }
}

// ---------------------------------------------------------------------------
// FindInstance
// ---------------------------------------------------------------------------

CubeViewInstance* TwizzledartPlugin::FindInstance(int64_t textureId) {
    std::lock_guard<std::mutex> lock(instancesMutex_);
    auto it = instances_.find(textureId);
    return (it != instances_.end()) ? it->second.get() : nullptr;
}

} // namespace twizzledart
