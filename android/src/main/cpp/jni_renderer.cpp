// platform/android/jni_renderer.cpp
// OpenGL ES 3.0 renderer bridge for Android via JNI.
// Called by CubeGLSurfaceView.kt's Renderer implementation.

#include <jni.h>
#include <android/log.h>
#include "renderer/CubeRenderer.hpp"
#include "twizzle/twizzle.hpp"

#define LOG_TAG "TwizzleRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace twizzle;
using namespace twizzle::renderer;

// One global renderer instance per GL context
static CubeRenderer* g_renderer = nullptr;

// ---------------------------------------------------------------------------
// Lifecycle — called by GLSurfaceView.Renderer
// ---------------------------------------------------------------------------

extern "C" {

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeOnSurfaceCreated(
        JNIEnv*, jobject) {
    if (g_renderer) { g_renderer->destroy(); delete g_renderer; }
    g_renderer = new CubeRenderer();
    if (!g_renderer->init()) {
        LOGE("CubeRenderer::init() failed");
    } else {
        LOGI("CubeRenderer initialised");
    }
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeOnSurfaceChanged(
        JNIEnv*, jobject, jint width, jint height) {
    if (g_renderer) g_renderer->resize(width, height);
    LOGI("Surface changed: %d×%d", width, height);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeOnDrawFrame(
        JNIEnv*, jobject, jfloat dt) {
    if (g_renderer) g_renderer->render(dt);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeOnDestroy(
        JNIEnv*, jobject) {
    if (g_renderer) {
        g_renderer->destroy();
        delete g_renderer;
        g_renderer = nullptr;
        LOGI("CubeRenderer destroyed");
    }
}

// ---------------------------------------------------------------------------
// Input — drag + zoom
// ---------------------------------------------------------------------------

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeOnDrag(
        JNIEnv*, jobject, jfloat dx, jfloat dy) {
    if (g_renderer) g_renderer->onDrag(dx, dy);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeOnZoom(
        JNIEnv*, jobject, jfloat delta) {
    if (g_renderer) g_renderer->onZoom(delta);
}

// ---------------------------------------------------------------------------
// Cube moves
// ---------------------------------------------------------------------------

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeApplyAlgorithm(
        JNIEnv* env, jobject, jstring algStr) {
    if (!g_renderer) return;
    const char* cAlg = env->GetStringUTFChars(algStr, nullptr);
    if (cAlg) {
        try {
            g_renderer->applyAlgorithm(cAlg);
            LOGI("Applied algorithm: %s", cAlg);
        } catch (const std::exception& e) {
            LOGE("Algorithm error: %s", e.what());
        }
        env->ReleaseStringUTFChars(algStr, cAlg);
    }
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeReset(
        JNIEnv*, jobject) {
    if (g_renderer) g_renderer->reset();
}

JNIEXPORT jboolean JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeIsAnimating(
        JNIEnv*, jobject) {
    return g_renderer && g_renderer->isAnimating() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeSetSpeed(
        JNIEnv*, jobject, jfloat speed) {
    if (g_renderer) g_renderer->setSpeed(speed);
}

JNIEXPORT jfloat JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeGetSpeed(
        JNIEnv*, jobject) {
    return g_renderer ? g_renderer->getSpeed() : 1.0f;
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativePlay(
        JNIEnv*, jobject) {
    if (g_renderer) g_renderer->play();
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativePause(
        JNIEnv*, jobject) {
    if (g_renderer) g_renderer->pause();
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeStepForward(
        JNIEnv*, jobject) {
    if (g_renderer) g_renderer->stepForward();
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeStepBackward(
        JNIEnv*, jobject) {
    if (g_renderer) g_renderer->stepBackward();
}

JNIEXPORT jboolean JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeIsPlaying(
        JNIEnv*, jobject) {
    return g_renderer && g_renderer->isPlaying() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeSetBackgroundColor(
        JNIEnv*, jobject, jfloat r, jfloat g, jfloat b, jfloat a) {
    if (g_renderer) g_renderer->setBackgroundColor(r, g, b, a);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeSetCameraPosition(
        JNIEnv*, jobject, jfloat lat, jfloat lon, jfloat rad) {
    if (g_renderer) g_renderer->setCameraPosition(lat, lon, rad);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeSeekFraction(
        JNIEnv*, jobject, jfloat f) {
    if (g_renderer) g_renderer->seekFraction(f);
}

JNIEXPORT jfloat JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeCurrentFraction(
        JNIEnv*, jobject) {
    return g_renderer ? g_renderer->currentFraction() : 0.0f;
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeSetShowHint(
        JNIEnv*, jobject, jboolean enabled) {
    if (g_renderer) g_renderer->setShowHint(enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeSetBodyAlpha(
        JNIEnv*, jobject, jfloat alpha) {
    if (g_renderer) g_renderer->setBodyAlpha((float)alpha);
}

JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_TwizzleRenderer_nativeSetFaceColors(
        JNIEnv* env, jobject, jfloatArray colors) {
    if (!g_renderer) return;
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
    g_renderer->setFaceColors(faceColors);
}

} // extern "C"
