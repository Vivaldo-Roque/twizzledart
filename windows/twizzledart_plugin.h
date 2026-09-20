#pragma once
// =============================================================================
// twizzledart_plugin.h — Windows Flutter plugin for TwizzleDart
//
// Manages one WebGpuRenderer per TwizzleView instance (keyed by textureId).
// The renderer writes each frame into a Flutter external texture that the Dart
// side consumes as Texture(textureId: id).
// =============================================================================

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <windows.h>
#include <memory>
#include <unordered_map>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

// Forward declarations (avoid heavy wgpu include in the header)
namespace twizzle::renderer { class WebGpuRenderer; }

namespace twizzledart {

// ---------------------------------------------------------------------------
// CubeViewInstance — one 3D view rendered into one Flutter Texture
// ---------------------------------------------------------------------------
struct CubeViewInstance {
    int64_t textureId = -1;

    // Flutter external texture pixel buffer
    std::unique_ptr<uint8_t[]> pixelBuffer;
    uint32_t bufferWidth  = 0;
    uint32_t bufferHeight = 0;

    // WebGPU renderer (renders to offscreen surface, then readback to pixelBuffer)
    std::unique_ptr<twizzle::renderer::WebGpuRenderer> renderer;

    // Render loop state
    std::thread   renderThread;
    std::atomic<bool> running{false};
    std::mutex    rendererMutex;

    // Method channel for this specific view (twizzledart_view_<id>)
    std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> viewChannel;
};

// ---------------------------------------------------------------------------
// TwizzledartPlugin
// ---------------------------------------------------------------------------
class TwizzledartPlugin final : public flutter::Plugin {
public:
    static void RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar);

    explicit TwizzledartPlugin(flutter::PluginRegistrarWindows* registrar);
    ~TwizzledartPlugin() override;

    // Non-copyable
    TwizzledartPlugin(const TwizzledartPlugin&)            = delete;
    TwizzledartPlugin& operator=(const TwizzledartPlugin&) = delete;

private:
    flutter::PluginRegistrarWindows* registrar_;

    // Main plugin MethodChannel — handles createView / disposeView
    std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel_;

    // Active cube view instances, keyed by textureId
    std::unordered_map<int64_t, std::unique_ptr<CubeViewInstance>> instances_;
    std::mutex instancesMutex_;

    // ── Main channel handler ──────────────────────────────────────────────
    void HandleMethodCall(
        const flutter::MethodCall<flutter::EncodableValue>& call,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    // ── View lifecycle ────────────────────────────────────────────────────
    void OnCreateView(
        const flutter::MethodCall<flutter::EncodableValue>& call,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void OnDisposeView(int64_t textureId);

    // ── Per-view channel handler ──────────────────────────────────────────
    void HandleViewMethodCall(
        int64_t textureId,
        const flutter::MethodCall<flutter::EncodableValue>& call,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    // ── Render loop ───────────────────────────────────────────────────────
    void RenderLoop(CubeViewInstance* inst);

    // ── Helpers ───────────────────────────────────────────────────────────
    CubeViewInstance* FindInstance(int64_t textureId);
};

} // namespace twizzledart
