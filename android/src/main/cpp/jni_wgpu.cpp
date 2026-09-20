// =============================================================================
// jni_wgpu.cpp — Android WebGPU renderer bridge via JNI
//
// Replaces jni_renderer.cpp (OpenGL ES 3.0 + GLSurfaceView) with a WebGPU
// renderer driven from Kotlin via Flutter Texture / SurfaceTexture.
//
// Architecture (Phase 3):
//   Kotlin TwizzleTextureRenderer
//     ↕ JNI (this file)
//   WebGpuRenderer (native/) — uses wgpu-native Vulkan backend on Android
//
// The Kotlin side:
//   1. Gets an ANativeWindow* from a SurfaceTexture / ImageReader.
//   2. Calls nativeCreate() — creates the wgpu Instance + Surface + Device.
//   3. Calls nativeResize() on layout changes.
//   4. Calls nativeRenderFrame(dt) on each Choreographer tick.
//   5. Calls nativeDestroy() when the view is disposed.
// =============================================================================
#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <stdexcept>
#include <string>
#include <cstring>

#include "renderer/WebGpuRenderer.hpp"
#include "twizzle/twizzle.hpp"

#define LOG_TAG "TwizzleWGPU"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace twizzle;
using namespace twizzle::renderer;

// ---------------------------------------------------------------------------
// Per-instance renderer — keyed by opaque jlong handle.
// This allows multiple TwizzleView instances to coexist on the same Activity.
// ---------------------------------------------------------------------------
struct RendererInstance {
    WebGpuRenderer renderer;
    ANativeWindow* window = nullptr;
};

// ---------------------------------------------------------------------------
// JNI helpers
// ---------------------------------------------------------------------------

static RendererInstance* toInstance(jlong handle) {
    return reinterpret_cast<RendererInstance*>(handle);
}

static std::string jstr(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string result(c);
    env->ReleaseStringUTFChars(s, c);
    return result;
}

// ---------------------------------------------------------------------------
// JNI exports — com.example.twizzledart.twizzledart.TwizzleTextureRenderer
// ---------------------------------------------------------------------------

extern "C" {

/**
 * Creates a new WebGpuRenderer against the provided ANativeWindow (from
 * a Surface / SurfaceTexture on the Kotlin side).
 *
 * @param surface  jobject — android.view.Surface
 * @param width    initial surface width
 * @param height   initial surface height
 * @return opaque handle (pointer cast to jlong), or 0 on failure.
 */
JNIEXPORT jlong JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeCreate(
        JNIEnv* env, jobject, jobject surface, jint width, jint height) {

    ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
    if (!win) {
        LOGE("ANativeWindow_fromSurface returned null");
        return 0L;
    }

    auto* inst = new RendererInstance();
    inst->window = win;

    SurfaceDescriptor desc;
    desc.androidNativeWindow = win;
    desc.width  = (uint32_t)width;
    desc.height = (uint32_t)height;

    if (!inst->renderer.init(desc)) {
        LOGE("WebGpuRenderer::init() failed");
        ANativeWindow_release(win);
        delete inst;
        return 0L;
    }

    LOGI("WebGpuRenderer created: handle=%p  %dx%d", (void*)inst, width, height);
    return reinterpret_cast<jlong>(inst);
}

/**
 * Releases all WebGPU resources and the ANativeWindow reference.
 */
JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeDestroy(
        JNIEnv*, jobject, jlong handle) {
    auto* inst = toInstance(handle);
    if (!inst) return;
    inst->renderer.destroy();
    if (inst->window) { ANativeWindow_release(inst->window); inst->window = nullptr; }
    delete inst;
    LOGI("WebGpuRenderer destroyed");
}

/**
 * Updates the swap-chain dimensions on surface resize.
 */
JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeResize(
        JNIEnv*, jobject, jlong handle, jint width, jint height) {
    auto* inst = toInstance(handle);
    if (inst) inst->renderer.resize((uint32_t)width, (uint32_t)height);
}

/**
 * Renders one frame. Called on the Choreographer callback thread from Kotlin.
 * @param dt  delta time in seconds since last call.
 */
JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeRenderFrame(
        JNIEnv*, jobject, jlong handle, jfloat dt) {
    auto* inst = toInstance(handle);
    if (inst) inst->renderer.render(dt);
}

// ---------------------------------------------------------------------------
// Camera input
// ---------------------------------------------------------------------------

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeDragBegin(
        JNIEnv*, jobject, jlong handle, jfloat x, jfloat y) {
    auto* inst = toInstance(handle);
    if (inst) inst->renderer.onDragBegin(x, y);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeDragMove(
        JNIEnv*, jobject, jlong handle, jfloat x, jfloat y) {
    auto* inst = toInstance(handle);
    if (inst) inst->renderer.onDragMove(x, y);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeDragEnd(
        JNIEnv*, jobject, jlong handle) {
    auto* inst = toInstance(handle);
    if (inst) inst->renderer.onDragEnd();
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeZoom(
        JNIEnv*, jobject, jlong handle, jfloat delta) {
    auto* inst = toInstance(handle);
    if (inst) inst->renderer.onZoom(delta);
}

// ---------------------------------------------------------------------------
// Cube state
// ---------------------------------------------------------------------------

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeApplyAlgorithm(
        JNIEnv* env, jobject, jlong handle, jstring algStr) {
    auto* inst = toInstance(handle);
    if (!inst) return;
    std::string alg = jstr(env, algStr);
    if (!alg.empty()) {
        try {
            inst->renderer.applyAlgorithm(alg);
            LOGI("Applied: %s", alg.c_str());
        } catch (const std::exception& e) {
            LOGE("Algorithm error: %s", e.what());
        }
    }
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeReset(
        JNIEnv*, jobject, jlong handle) {
    auto* inst = toInstance(handle);
    if (inst) inst->renderer.reset();
}

JNIEXPORT jboolean JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeIsAnimating(
        JNIEnv*, jobject, jlong handle) {
    auto* inst = toInstance(handle);
    return (inst && inst->renderer.isAnimating()) ? JNI_TRUE : JNI_FALSE;
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativePlay(
        JNIEnv*, jobject, jlong handle) {
    if (auto* inst = toInstance(handle)) inst->renderer.play();
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativePause(
        JNIEnv*, jobject, jlong handle) {
    if (auto* inst = toInstance(handle)) inst->renderer.pause();
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeStepForward(
        JNIEnv*, jobject, jlong handle) {
    if (auto* inst = toInstance(handle)) inst->renderer.stepForward();
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeStepBackward(
        JNIEnv*, jobject, jlong handle) {
    if (auto* inst = toInstance(handle)) inst->renderer.stepBackward();
}

JNIEXPORT jboolean JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeIsPlaying(
        JNIEnv*, jobject, jlong handle) {
    auto* inst = toInstance(handle);
    return (inst && inst->renderer.isPlaying()) ? JNI_TRUE : JNI_FALSE;
}

// ---------------------------------------------------------------------------
// Scrubbing
// ---------------------------------------------------------------------------

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeSeekFraction(
        JNIEnv*, jobject, jlong handle, jfloat f) {
    if (auto* inst = toInstance(handle)) inst->renderer.seekFraction(f);
}

JNIEXPORT jfloat JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeCurrentFraction(
        JNIEnv*, jobject, jlong handle) {
    auto* inst = toInstance(handle);
    return inst ? inst->renderer.currentFraction() : 0.0f;
}

// ---------------------------------------------------------------------------
// Visual properties
// ---------------------------------------------------------------------------

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeSetSpeed(
        JNIEnv*, jobject, jlong handle, jfloat speed) {
    if (auto* inst = toInstance(handle)) inst->renderer.setSpeed(speed);
}

JNIEXPORT jfloat JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeGetSpeed(
        JNIEnv*, jobject, jlong handle) {
    auto* inst = toInstance(handle);
    return inst ? inst->renderer.getSpeed() : 1.0f;
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeSetBackgroundColor(
        JNIEnv*, jobject, jlong handle, jfloat r, jfloat g, jfloat b, jfloat a) {
    if (auto* inst = toInstance(handle)) inst->renderer.setBackgroundColor(r, g, b, a);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeSetCameraPosition(
        JNIEnv*, jobject, jlong handle, jfloat lat, jfloat lon, jfloat rad) {
    if (auto* inst = toInstance(handle)) inst->renderer.setCameraPosition(lat, lon, rad);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeSetShowHint(
        JNIEnv*, jobject, jlong handle, jboolean enabled) {
    if (auto* inst = toInstance(handle)) inst->renderer.setShowHint(enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeSetBodyAlpha(
        JNIEnv*, jobject, jlong handle, jfloat alpha) {
    if (auto* inst = toInstance(handle)) inst->renderer.setBodyAlpha(alpha);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeSetFaceColors(
        JNIEnv* env, jobject, jlong handle, jfloatArray colors) {
    auto* inst = toInstance(handle);
    if (!inst) return;
    jsize len = env->GetArrayLength(colors);
    if (len < 18) return;
    jfloat* elems = env->GetFloatArrayElements(colors, nullptr);
    if (!elems) return;
    float fc[6][3];
    for (int i = 0; i < 6; ++i) {
        fc[i][0] = elems[i * 3 + 0];
        fc[i][1] = elems[i * 3 + 1];
        fc[i][2] = elems[i * 3 + 2];
    }
    env->ReleaseFloatArrayElements(colors, elems, JNI_ABORT);
    inst->renderer.setFaceColors(fc);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeSetPitchLock(
    JNIEnv* env, jobject thiz, jlong handle, jboolean locked)
{
    if (auto* inst = toInstance(handle)) inst->renderer.setPitchLock(locked == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleTextureRenderer_nativeSetDebugLogs(
    JNIEnv* env, jobject thiz, jlong handle, jboolean enabled)
{
    if (auto* inst = toInstance(handle)) inst->renderer.setDebugLogs(enabled == JNI_TRUE);
}

} // extern "C"
