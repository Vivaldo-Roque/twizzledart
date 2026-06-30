#include <jni.h>
#include <string>
#include <android/log.h>
#include "twizzle/twizzle.hpp"

#define LOG_TAG "TwizzleNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace twizzle;

// ---------------------------------------------------------------------------
// JNI Interface – matches com.example.twizzle.CubeEngine
// ---------------------------------------------------------------------------

extern "C" {

/**
 * Creates a new Cube3x3 on the heap.
 * @return opaque handle (pointer cast to jlong)
 */
JNIEXPORT jlong JNICALL
Java_com_example_twizzledart_twizzledart_CubeEngine_nativeCreate(JNIEnv* env, jobject obj) {
    auto* cube = new Cube3x3();
    LOGI("Cube created at %p", (void*)cube);
    return reinterpret_cast<jlong>(cube);
}

/**
 * Destroys a previously created cube.
 */
JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_CubeEngine_nativeDestroy(JNIEnv* env, jobject obj, jlong handle) {
    auto* cube = reinterpret_cast<Cube3x3*>(handle);
    if (cube) {
        LOGI("Destroying cube at %p", (void*)cube);
        delete cube;
    }
}

/**
 * Resets the cube to the solved state.
 */
JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_CubeEngine_nativeReset(JNIEnv* env, jobject obj, jlong handle) {
    auto* cube = reinterpret_cast<Cube3x3*>(handle);
    if (!cube) return;
    cube->reset();
}

/**
 * Applies an algorithm string to the cube.
 * @param algStr  e.g. "R U R' U'"
 */
JNIEXPORT void JNICALL
Java_com_example_twizzledart_twizzledart_CubeEngine_nativeApplyAlgorithm(
    JNIEnv*  env,
    jobject  obj,
    jlong    handle,
    jstring  algStr)
{
    auto* cube = reinterpret_cast<Cube3x3*>(handle);
    if (!cube) return;

    const char* cAlg = env->GetStringUTFChars(algStr, nullptr);
    if (!cAlg) return;

    try {
        Algorithm alg(cAlg);
        alg.execute(*cube);
        LOGI("Applied: %s", cAlg);
    } catch (const std::exception& e) {
        LOGE("Error applying algorithm '%s': %s", cAlg, e.what());
    }

    env->ReleaseStringUTFChars(algStr, cAlg);
}

/**
 * Returns true if the cube is in the solved state.
 */
JNIEXPORT jboolean JNICALL
Java_com_example_twizzledart_twizzledart_CubeEngine_nativeIsSolved(JNIEnv* env, jobject obj, jlong handle) {
    auto* cube = reinterpret_cast<Cube3x3*>(handle);
    if (!cube) return JNI_FALSE;
    return cube->isSolved() ? JNI_TRUE : JNI_FALSE;
}

/**
 * Returns the cube state as a jintArray of 40 ints:
 *   [0..7]   = corner permutation
 *   [8..15]  = corner orientation
 *   [16..27] = edge permutation
 *   [28..39] = edge orientation
 */
JNIEXPORT jintArray JNICALL
Java_com_example_twizzledart_twizzledart_CubeEngine_nativeGetState(JNIEnv* env, jobject obj, jlong handle) {
    auto* cube = reinterpret_cast<Cube3x3*>(handle);
    jintArray result = env->NewIntArray(40);
    if (!cube || !result) return result;

    auto state = cube->getState();
    jint buf[40];
    for (int i = 0; i <  8; ++i) buf[i]      = state.cp[i];
    for (int i = 0; i <  8; ++i) buf[i +  8] = state.co[i];
    for (int i = 0; i < 12; ++i) buf[i + 16] = state.ep[i];
    for (int i = 0; i < 12; ++i) buf[i + 28] = state.eo[i];
    env->SetIntArrayRegion(result, 0, 40, buf);
    return result;
}

/**
 * Returns the algorithm string as parsed + re-serialised (useful for
 * canonicalisation / inverse).
 * @param algStr    input algorithm
 * @param doInverse if non-zero, returns the inverse algorithm
 */
JNIEXPORT jstring JNICALL
Java_com_example_twizzledart_twizzledart_CubeEngine_nativeParseAlgorithm(
    JNIEnv*  env,
    jobject  obj,
    jstring  algStr,
    jboolean doInverse)
{
    const char* cAlg = env->GetStringUTFChars(algStr, nullptr);
    if (!cAlg) return env->NewStringUTF("");

    std::string result;
    try {
        Algorithm alg(cAlg);
        if (doInverse) alg = alg.inverse();
        result = alg.toString();
    } catch (...) {
        result = "";
    }

    env->ReleaseStringUTFChars(algStr, cAlg);
    return env->NewStringUTF(result.c_str());
}

} // extern "C"
