package com.example.twizzledart.twizzledart

import android.content.Context
import android.graphics.PixelFormat
import android.opengl.GLSurfaceView
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

// ---------------------------------------------------------------------------
// TwizzleGLSurfaceView
// ---------------------------------------------------------------------------
/**
 * Drop-in OpenGL ES 3.0 GLSurfaceView that renders the Rubik's cube via native C++ renderer.
 *
 * Implements [TwizzleCubeView], providing full API parity with [TwizzleTextureRenderer].
 */
class TwizzleGLSurfaceView(context: Context) : GLSurfaceView(context), TwizzleCubeView {

    override fun getView(): View = this

    private val renderer = TwizzleRenderer()

    // Touch state
    override var touchEnabled: Boolean = true
    override var onTapListener: (() -> Unit)? = null
    private var isClickGesture = false
    private var lastTouchX = 0f
    private var lastTouchY = 0f
    private var lastSpan = 0f

    private val scaleDetector = ScaleGestureDetector(context,
        object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
            override fun onScaleBegin(d: ScaleGestureDetector): Boolean {
                lastSpan = d.currentSpan
                return true
            }
            override fun onScale(detector: ScaleGestureDetector): Boolean {
                val delta = (detector.currentSpan - lastSpan) * 0.01f
                if (delta != 0f) {
                    queueEvent {
                        if (renderer.nativeHandle != 0L) {
                            renderer.nativeOnZoom(renderer.nativeHandle, delta)
                        }
                    }
                }
                lastSpan = detector.currentSpan
                return true
            }
        })

    init {
        setEGLContextClientVersion(3) // OpenGL ES 3.0

        // Request 8888 with 16-bit depth buffer and 4× MSAA
        setEGLConfigChooser(8, 8, 8, 8, 16, 4)
        holder.setFormat(PixelFormat.TRANSLUCENT)

        setRenderer(renderer)
        renderMode = RENDERMODE_CONTINUOUSLY
    }

    // ── Input ──────────────────────────────────────────────────────────────

    private var wasScaling = false

    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (!touchEnabled) {
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    isClickGesture = true
                    lastTouchX = e.x
                    lastTouchY = e.y
                }
                MotionEvent.ACTION_UP -> {
                    if (isClickGesture) {
                        onTapListener?.invoke()
                    }
                }
                MotionEvent.ACTION_MOVE -> {
                    val dx = Math.abs(e.x - lastTouchX)
                    val dy = Math.abs(e.y - lastTouchY)
                    if (dx > 10 || dy > 10) {
                        isClickGesture = false
                    }
                }
            }
            return true
        }

        scaleDetector.onTouchEvent(e)

        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                wasScaling = false
                lastTouchX   = e.x
                lastTouchY   = e.y
            }
            MotionEvent.ACTION_MOVE -> {
                if (scaleDetector.isInProgress) {
                    wasScaling = true
                } else if (e.pointerCount == 1) {
                    if (wasScaling) {
                        wasScaling = false
                    } else {
                        val dx = e.x - lastTouchX
                        val dy = e.y - lastTouchY
                        queueEvent {
                            if (renderer.nativeHandle != 0L) {
                                renderer.nativeOnDrag(renderer.nativeHandle, dx, dy)
                            }
                        }
                    }
                }
                lastTouchX = e.x
                lastTouchY = e.y
            }
        }
        return true
    }

    // ── TwizzleCubeView API ─────────────────────────────────────────────────

    override fun applyAlgorithm(alg: String) {
        renderer.cachedAlgorithm = alg
        queueEvent {
            if (renderer.nativeHandle != 0L) {
                renderer.nativeApplyAlgorithm(renderer.nativeHandle, alg)
            }
        }
    }

    override fun reset() {
        queueEvent {
            if (renderer.nativeHandle != 0L) {
                renderer.nativeReset(renderer.nativeHandle)
            }
        }
    }

    override fun isAnimating(): Boolean {
        return if (renderer.nativeHandle != 0L) renderer.nativeIsAnimating(renderer.nativeHandle)
        else false
    }

    override fun play() {
        queueEvent {
            if (renderer.nativeHandle != 0L) {
                renderer.nativePlay(renderer.nativeHandle)
            }
        }
    }

    override fun pause() {
        queueEvent {
            if (renderer.nativeHandle != 0L) {
                renderer.nativePause(renderer.nativeHandle)
            }
        }
    }

    override fun stepForward() {
        queueEvent {
            if (renderer.nativeHandle != 0L) {
                renderer.nativeStepForward(renderer.nativeHandle)
            }
        }
    }

    override fun stepBackward() {
        queueEvent {
            if (renderer.nativeHandle != 0L) {
                renderer.nativeStepBackward(renderer.nativeHandle)
            }
        }
    }

    override fun isPlaying(): Boolean {
        return if (renderer.nativeHandle != 0L) renderer.nativeIsPlaying(renderer.nativeHandle)
        else false
    }

    override fun setSpeed(speed: Float) {
        renderer.cachedSpeed = speed
        queueEvent {
            if (renderer.nativeHandle != 0L) {
                renderer.nativeSetSpeed(renderer.nativeHandle, speed)
            }
        }
    }

    override fun getSpeed(): Float {
        return if (renderer.nativeHandle != 0L) renderer.nativeGetSpeed(renderer.nativeHandle)
        else renderer.cachedSpeed ?: 1.0f
    }

    override fun setGLBackgroundColor(r: Float, g: Float, b: Float, a: Float) {
        renderer.cachedBgColor = floatArrayOf(r, g, b, a)
        queueEvent {
            if (renderer.nativeHandle != 0L) {
                renderer.nativeSetBackgroundColor(renderer.nativeHandle, r, g, b, a)
            }
        }
    }

    override fun setCameraPosition(lat: Float, lon: Float, rad: Float) {
        renderer.cachedCameraPosition = floatArrayOf(lat, lon, rad)
        queueEvent {
            if (renderer.nativeHandle != 0L) {
                renderer.nativeSetCameraPosition(renderer.nativeHandle, lat, lon, rad)
            }
        }
    }

    override fun setShowHint(enabled: Boolean) {
        renderer.cachedShowHint = enabled
        queueEvent {
            if (renderer.nativeHandle != 0L) {
                renderer.nativeSetShowHint(renderer.nativeHandle, enabled)
            }
        }
    }

    override fun setShowFaceLetters(enabled: Boolean) {
        renderer.cachedShowFaceLetters = enabled
        queueEvent {
            if (renderer.nativeHandle != 0L) {
                renderer.nativeSetShowFaceLetters(renderer.nativeHandle, enabled)
            }
        }
    }

    override fun setFaceColors(colors: FloatArray) {
        renderer.cachedFaceColors = colors
        queueEvent {
            if (renderer.nativeHandle != 0L) {
                renderer.nativeSetFaceColors(renderer.nativeHandle, colors)
            }
        }
    }

    override fun setBodyAlpha(alpha: Float) {
        renderer.cachedBodyAlpha = alpha
        queueEvent {
            if (renderer.nativeHandle != 0L) {
                renderer.nativeSetBodyAlpha(renderer.nativeHandle, alpha)
            }
        }
    }

    override fun seekFraction(f: Float) {
        queueEvent {
            if (renderer.nativeHandle != 0L) {
                renderer.nativeSeekFraction(renderer.nativeHandle, f)
            }
        }
    }

    override fun getCurrentFraction(): Float {
        return if (renderer.nativeHandle != 0L) renderer.nativeCurrentFraction(renderer.nativeHandle)
        else 0.0f
    }

    override fun setPitchLock(locked: Boolean) {
        renderer.cachedPitchLock = locked
        queueEvent {
            if (renderer.nativeHandle != 0L) {
                renderer.nativeSetPitchLock(renderer.nativeHandle, locked)
            }
        }
    }

    override fun setDebugLogs(enabled: Boolean) {
        renderer.cachedDebugLogs = enabled
        queueEvent {
            if (renderer.nativeHandle != 0L) {
                renderer.nativeSetDebugLogs(renderer.nativeHandle, enabled)
            }
        }
    }

    override fun onDestroyView() {
        queueEvent {
            renderer.onDestroy()
        }
        try {
            onPause()
        } catch (_: Throwable) {}
    }

    override fun onPause() {
        super.onPause()
        queueEvent {
            renderer.onDestroy()
        }
    }

    override fun onDetachedFromWindow() {
        onDestroyView()
        super.onDetachedFromWindow()
    }
}

// ---------------------------------------------------------------------------
// TwizzleRenderer — GLSurfaceView.Renderer delegating to C++ via JNI
// ---------------------------------------------------------------------------
class TwizzleRenderer : GLSurfaceView.Renderer {

    var nativeHandle: Long = 0L
    private var lastNano = System.nanoTime()

    var cachedBgColor: FloatArray? = null
    var cachedCameraPosition: FloatArray? = null
    var cachedShowHint: Boolean? = null
    var cachedShowFaceLetters: Boolean? = null
    var cachedFaceColors: FloatArray? = null
    var cachedBodyAlpha: Float? = null
    var cachedPitchLock: Boolean? = null
    var cachedSpeed: Float? = null
    var cachedAlgorithm: String? = null
    var cachedDebugLogs: Boolean? = null

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        if (nativeHandle != 0L) {
            nativeDestroy(nativeHandle)
            nativeHandle = 0L
        }
        nativeHandle = nativeCreate()
        if (nativeHandle != 0L) {
            nativeOnSurfaceCreated(nativeHandle)
            lastNano = System.nanoTime()

            cachedBgColor?.let {
                nativeSetBackgroundColor(nativeHandle, it[0], it[1], it[2], it[3])
            }
            cachedCameraPosition?.let {
                nativeSetCameraPosition(nativeHandle, it[0], it[1], it[2])
            }
            cachedShowHint?.let {
                nativeSetShowHint(nativeHandle, it)
            }
            cachedShowFaceLetters?.let {
                nativeSetShowFaceLetters(nativeHandle, it)
            }
            cachedFaceColors?.let {
                nativeSetFaceColors(nativeHandle, it)
            }
            cachedBodyAlpha?.let {
                nativeSetBodyAlpha(nativeHandle, it)
            }
            cachedPitchLock?.let {
                nativeSetPitchLock(nativeHandle, it)
            }
            cachedSpeed?.let {
                nativeSetSpeed(nativeHandle, it)
            }
            cachedAlgorithm?.let {
                nativeApplyAlgorithm(nativeHandle, it)
            }
            cachedDebugLogs?.let {
                nativeSetDebugLogs(nativeHandle, it)
            }
        }
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        if (nativeHandle != 0L) {
            nativeOnSurfaceChanged(nativeHandle, width, height)
        }
    }

    override fun onDrawFrame(gl: GL10?) {
        if (nativeHandle == 0L) return
        val now  = System.nanoTime()
        val dt   = (now - lastNano) / 1_000_000_000f
        lastNano = now
        nativeOnDrawFrame(nativeHandle, dt.coerceIn(0f, 0.05f))
    }

    fun onDestroy() {
        if (nativeHandle != 0L) {
            val h = nativeHandle
            nativeHandle = 0L
            nativeDestroy(h)
        }
    }

    // ── JNI declarations ──────────────────────────────────────────────────

    external fun nativeCreate(): Long
    external fun nativeDestroy(handle: Long)

    external fun nativeOnSurfaceCreated(handle: Long)
    external fun nativeOnSurfaceChanged(handle: Long, width: Int, height: Int)
    external fun nativeOnDrawFrame(handle: Long, dt: Float)

    external fun nativeOnDrag(handle: Long, dx: Float, dy: Float)
    external fun nativeOnZoom(handle: Long, delta: Float)

    external fun nativeApplyAlgorithm(handle: Long, alg: String)
    external fun nativeReset(handle: Long)
    external fun nativeIsAnimating(handle: Long): Boolean
    external fun nativeSetSpeed(handle: Long, speed: Float)
    external fun nativeGetSpeed(handle: Long): Float
    external fun nativePlay(handle: Long)
    external fun nativePause(handle: Long)
    external fun nativeStepForward(handle: Long)
    external fun nativeStepBackward(handle: Long)
    external fun nativeIsPlaying(handle: Long): Boolean
    external fun nativeSetBackgroundColor(handle: Long, r: Float, g: Float, b: Float, a: Float)
    external fun nativeSetCameraPosition(handle: Long, lat: Float, lon: Float, rad: Float)
    external fun nativeSeekFraction(handle: Long, f: Float)
    external fun nativeCurrentFraction(handle: Long): Float
    external fun nativeSetShowHint(handle: Long, enabled: Boolean)
    external fun nativeSetShowFaceLetters(handle: Long, enabled: Boolean)
    external fun nativeSetFaceColors(handle: Long, colors: FloatArray)
    external fun nativeSetBodyAlpha(handle: Long, alpha: Float)
    external fun nativeSetPitchLock(handle: Long, locked: Boolean)
    external fun nativeSetDebugLogs(handle: Long, enabled: Boolean)

    companion object {
        init { System.loadLibrary("twizzle") }
    }
}
