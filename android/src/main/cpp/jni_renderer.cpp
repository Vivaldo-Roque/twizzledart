// platform/android/jni_renderer.cpp
// OpenGL ES 3.0 renderer bridge for Android via JNI.
// Called by TwizzleGLSurfaceView.kt's TwizzleRenderer implementation.

#include <jni.h>
#include <android/log.h>
#include <string>
#include "renderer/CubeRenderer.hpp"
#include "twizzle/twizzle.hpp"

#define LOG_TAG "TwizzleRendererGL"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace twizzle;
using namespace twizzle::renderer;

static inline CubeRenderer* toRenderer(jlong handle) {
    return reinterpret_cast<CubeRenderer*>(handle);
}

// ---------------------------------------------------------------------------
// Lifecycle — called by GLSurfaceView.Renderer
// ---------------------------------------------------------------------------

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeCreate(
        JNIEnv*, jobject) {
    auto* renderer = new CubeRenderer();
    LOGI("Created CubeRenderer instance: %p", renderer);
    return reinterpret_cast<jlong>(renderer);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeDestroy(
        JNIEnv*, jobject, jlong handle) {
    auto* renderer = toRenderer(handle);
    if (renderer) {
        renderer->destroy();
        delete renderer;
        LOGI("Destroyed CubeRenderer instance: %p", renderer);
    }
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeOnSurfaceCreated(
        JNIEnv*, jobject, jlong handle) {
    auto* renderer = toRenderer(handle);
    if (!renderer) return;
    if (!renderer->init()) {
        LOGE("CubeRenderer::init() failed on handle %p", renderer);
    } else {
        LOGI("CubeRenderer initialized on handle %p", renderer);
    }
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeOnSurfaceChanged(
        JNIEnv*, jobject, jlong handle, jint width, jint height) {
    auto* renderer = toRenderer(handle);
    if (renderer) {
        renderer->resize(width, height);
        LOGI("GL Surface changed: %d×%d", width, height);
    }
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeOnDrawFrame(
        JNIEnv*, jobject, jlong handle, jfloat dt) {
    auto* renderer = toRenderer(handle);
    if (renderer) renderer->render(dt);
}

// ---------------------------------------------------------------------------
// Input — drag + zoom
// ---------------------------------------------------------------------------

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeOnDrag(
        JNIEnv*, jobject, jlong handle, jfloat dx, jfloat dy) {
    auto* renderer = toRenderer(handle);
    if (renderer) renderer->onDrag(dx, dy);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeOnZoom(
        JNIEnv*, jobject, jlong handle, jfloat delta) {
    auto* renderer = toRenderer(handle);
    if (renderer) renderer->onZoom(delta);
}

// ---------------------------------------------------------------------------
// Cube moves & animation
// ---------------------------------------------------------------------------

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeApplyAlgorithm(
        JNIEnv* env, jobject, jlong handle, jstring algStr) {
    auto* renderer = toRenderer(handle);
    if (!renderer || !algStr) return;
    const char* cAlg = env->GetStringUTFChars(algStr, nullptr);
    if (cAlg) {
        try {
            renderer->applyAlgorithm(cAlg);
            LOGI("Applied algorithm: %s", cAlg);
        } catch (const std::exception& e) {
            LOGE("Algorithm error: %s", e.what());
        }
        env->ReleaseStringUTFChars(algStr, cAlg);
    }
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeReset(
        JNIEnv*, jobject, jlong handle) {
    auto* renderer = toRenderer(handle);
    if (renderer) renderer->reset();
}

JNIEXPORT jboolean JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeIsAnimating(
        JNIEnv*, jobject, jlong handle) {
    auto* renderer = toRenderer(handle);
    return (renderer && renderer->isAnimating()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeSetSpeed(
        JNIEnv*, jobject, jlong handle, jfloat speed) {
    auto* renderer = toRenderer(handle);
    if (renderer) renderer->setSpeed(speed);
}

JNIEXPORT jfloat JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeGetSpeed(
        JNIEnv*, jobject, jlong handle) {
    auto* renderer = toRenderer(handle);
    return renderer ? renderer->getSpeed() : 1.0f;
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeSetShowHint(
        JNIEnv*, jobject, jlong handle, jboolean enabled) {
    auto* renderer = toRenderer(handle);
    if (renderer) renderer->setShowHint(enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeSetBodyAlpha(
        JNIEnv*, jobject, jlong handle, jfloat alpha) {
    auto* renderer = toRenderer(handle);
    if (renderer) renderer->setBodyAlpha((float)alpha);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeSetFaceColors(
        JNIEnv* env, jobject, jlong handle, jfloatArray colors) {
    auto* renderer = toRenderer(handle);
    if (!renderer || !colors) return;
    jsize len = env->GetArrayLength(colors);
    if (len < 18) return;
    jfloat* elems = env->GetFloatArrayElements(colors, nullptr);
    if (!elems) return;
    // elems layout: [Rr,Rg,Rb, Lr,Lg,Lb, Ur,Ug,Ub, Dr,Dg,Db, Fr,Fg,Fb, Br,Bg,Bb]
    float faceColors[6][3];
    for (int i = 0; i < 6; ++i) {
        faceColors[i][0] = elems[i * 3 + 0];
        faceColors[i][1] = elems[i * 3 + 1];
        faceColors[i][2] = elems[i * 3 + 2];
    }
    env->ReleaseFloatArrayElements(colors, elems, JNI_ABORT);
    renderer->setFaceColors(faceColors);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativePlay(
        JNIEnv*, jobject, jlong handle) {
    auto* renderer = toRenderer(handle);
    if (renderer) renderer->play();
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativePause(
        JNIEnv*, jobject, jlong handle) {
    auto* renderer = toRenderer(handle);
    if (renderer) renderer->pause();
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeStepForward(
        JNIEnv*, jobject, jlong handle) {
    auto* renderer = toRenderer(handle);
    if (renderer) renderer->stepForward();
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeStepBackward(
        JNIEnv*, jobject, jlong handle) {
    auto* renderer = toRenderer(handle);
    if (renderer) renderer->stepBackward();
}

JNIEXPORT jboolean JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeIsPlaying(
        JNIEnv*, jobject, jlong handle) {
    auto* renderer = toRenderer(handle);
    return (renderer && renderer->isPlaying()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeSetBackgroundColor(
        JNIEnv*, jobject, jlong handle, jfloat red, jfloat green, jfloat blue, jfloat alpha) {
    auto* renderer = toRenderer(handle);
    if (renderer) renderer->setBackgroundColor(red, green, blue, alpha);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeSetCameraPosition(
        JNIEnv*, jobject, jlong handle, jfloat lat, jfloat lon, jfloat rad) {
    auto* renderer = toRenderer(handle);
    if (renderer) renderer->setCameraPosition(lat, lon, rad);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeSeekFraction(
        JNIEnv*, jobject, jlong handle, jfloat f) {
    auto* renderer = toRenderer(handle);
    if (renderer) renderer->seekFraction(f);
}

JNIEXPORT jfloat JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeCurrentFraction(
        JNIEnv*, jobject, jlong handle) {
    auto* renderer = toRenderer(handle);
    return renderer ? renderer->currentFraction() : 0.0f;
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeSetPitchLock(
        JNIEnv*, jobject, jlong handle, jboolean locked) {
    auto* renderer = toRenderer(handle);
    if (renderer) renderer->setPitchLock(locked == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeSetShowFaceLetters(
        JNIEnv*, jobject, jlong /*handle*/, jboolean /*enabled*/) {
    // Face letter quads can be wired here if texture quad pipeline is extended to GLES
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeSetDebugLogs(
        JNIEnv*, jobject, jlong /*handle*/, jboolean /*enabled*/) {
    // Debug log flag
}

} // extern "C"
