package com.example.twizzledart.twizzledart

import android.view.View

/**
 * Common contract for 3D Rubik's Cube render views in Twizzle.
 *
 * Implemented by:
 *  - [TwizzleTextureRenderer] (Modern WebGPU / wgpu-native / Vulkan)
 *  - [TwizzleGLSurfaceView] (Classic OpenGL ES 3.0 / GLSurfaceView)
 */
interface TwizzleCubeView {
    fun getView(): View

    var touchEnabled: Boolean
    var onTapListener: (() -> Unit)?

    fun applyAlgorithm(alg: String)
    fun reset()
    fun setSpeed(speed: Float)
    fun getSpeed(): Float
    fun isAnimating(): Boolean
    fun play()
    fun pause()
    fun stepForward()
    fun stepBackward()
    fun isPlaying(): Boolean
    fun setGLBackgroundColor(r: Float, g: Float, b: Float, a: Float)
    fun setCameraPosition(lat: Float, lon: Float, rad: Float)
    fun seekFraction(f: Float)
    fun getCurrentFraction(): Float
    fun setShowHint(enabled: Boolean)
    fun setShowFaceLetters(enabled: Boolean)
    fun setFaceColors(colors: FloatArray)
    fun setBodyAlpha(alpha: Float)
    fun setPitchLock(locked: Boolean)
    fun setDebugLogs(enabled: Boolean)
    fun onDestroyView()
}
