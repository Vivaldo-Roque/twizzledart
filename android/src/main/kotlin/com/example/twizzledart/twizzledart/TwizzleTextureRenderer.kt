package com.example.twizzledart.twizzledart

import android.graphics.SurfaceTexture
import android.view.Choreographer
import android.view.Surface
import android.view.TextureView
import android.content.Context
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View

/**
 * WebGPU-backed cube renderer using a [TextureView] + Vulkan/GLES (via wgpu-native).
 *
 * Uses event-driven (on-demand) [Choreographer] scheduling:
 *  - Runs at full VSync (60–120 FPS) while dragging, zooming, decaying camera inertia,
 *    or playing move animations.
 *  - Automatically suspends VSync callbacks (0 FPS) as soon as [nativeNeedsRender]
 *    returns false, eliminating idle GPU/CPU battery drain.
 */
class TwizzleTextureRenderer(context: Context) :
    TextureView(context),
    TextureView.SurfaceTextureListener,
    Choreographer.FrameCallback {

    // ── State ───────────────────────────────────────────────────────────────
    var touchEnabled: Boolean = true
    var onTapListener: (() -> Unit)? = null

    private var nativeHandle: Long = 0L          // opaque ptr to RendererInstance
    private var isDestroyed: Boolean = false
    private var choreographerScheduled: Boolean = false
    private var lastNano: Long = System.nanoTime()

    // Cached params — applied after surface is ready
    private var pendingAlgorithm: String? = null
    private var pendingSpeed: Float? = null
    private var pendingBg: FloatArray? = null     // [r,g,b,a]
    private var pendingCamera: FloatArray? = null  // [lat,lon,rad]
    private var pendingFaceColors: FloatArray? = null
    private var pendingBodyAlpha: Float? = null
    private var pendingShowHint: Boolean? = null
    private var pendingShowFaceLetters: Boolean? = null
    private var pendingPitchLock: Boolean? = null
    private var pendingDebugLogs: Boolean? = null

    // Touch input
    private var lastTouchX = 0f
    private var lastTouchY = 0f
    private var isClickGesture = false

    private val scaleDetector = ScaleGestureDetector(context,
        object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
            private var lastSpan = 0f
            override fun onScaleBegin(d: ScaleGestureDetector): Boolean {
                lastSpan = d.currentSpan; return true
            }
            override fun onScale(d: ScaleGestureDetector): Boolean {
                val delta = (d.currentSpan - lastSpan) * 0.01f
                zoom(delta)
                lastSpan = d.currentSpan
                return true
            }
        })

    init {
        surfaceTextureListener = this
        isOpaque = false // Enable transparency
    }

    // ── TextureView.SurfaceTextureListener ──────────────────────────────────

    override fun onSurfaceTextureAvailable(surfaceTexture: SurfaceTexture, width: Int, height: Int) {
        if (isDestroyed) return
        if (nativeHandle != 0L) {
            stopRenderLoop()
            nativeDestroy(nativeHandle)
            nativeHandle = 0L
        }
        var surface: Surface? = null
        try {
            surface = Surface(surfaceTexture)
            nativeHandle = nativeCreate(surface, width, height)
        } catch (e: Throwable) {
            android.util.Log.e("TwizzleTextureRenderer", "Failed to create native renderer: ${e.message}", e)
            nativeHandle = 0L
        } finally {
            surface?.release()
        }

        if (nativeHandle == 0L) return

        // Apply any parameters that arrived before the surface was ready
        pendingSpeed?.let { nativeSetSpeed(nativeHandle, it) }
        pendingBg?.let { nativeSetBackgroundColor(nativeHandle, it[0], it[1], it[2], it[3]) }
        pendingCamera?.let { nativeSetCameraPosition(nativeHandle, it[0], it[1], it[2]) }
        pendingFaceColors?.let { nativeSetFaceColors(nativeHandle, it) }
        pendingBodyAlpha?.let { nativeSetBodyAlpha(nativeHandle, it) }
        pendingShowHint?.let { nativeSetShowHint(nativeHandle, it) }
        pendingShowFaceLetters?.let { nativeSetShowFaceLetters(nativeHandle, it) }
        pendingPitchLock?.let { nativeSetPitchLock(nativeHandle, it) }
        pendingDebugLogs?.let { nativeSetDebugLogs(nativeHandle, it) }
        pendingAlgorithm?.let { if (it.isNotEmpty()) nativeApplyAlgorithm(nativeHandle, it) }

        startRenderLoop()
    }

    override fun onSurfaceTextureSizeChanged(surfaceTexture: SurfaceTexture, width: Int, height: Int) {
        if (nativeHandle != 0L) {
            nativeResize(nativeHandle, width, height)
            startRenderLoop()
        }
    }

    override fun onSurfaceTextureDestroyed(surfaceTexture: SurfaceTexture): Boolean {
        stopRenderLoop()
        if (nativeHandle != 0L) {
            nativeDestroy(nativeHandle)
            nativeHandle = 0L
        }
        return true
    }

    override fun onSurfaceTextureUpdated(surfaceTexture: SurfaceTexture) {
        // No-op
    }

    // ── Choreographer render loop (Event-Driven / On-Demand) ───────────────

    private fun startRenderLoop() {
        if (!choreographerScheduled && !isDestroyed && nativeHandle != 0L) {
            choreographerScheduled = true
            lastNano = System.nanoTime()
            Choreographer.getInstance().postFrameCallback(this)
        }
    }

    private fun stopRenderLoop() {
        if (choreographerScheduled) {
            choreographerScheduled = false
            Choreographer.getInstance().removeFrameCallback(this)
        }
    }

    override fun doFrame(frameTimeNanos: Long) {
        if (!choreographerScheduled || nativeHandle == 0L || isDestroyed) {
            choreographerScheduled = false
            return
        }

        val dt = ((frameTimeNanos - lastNano) / 1_000_000_000f).coerceIn(0f, 0.05f)
        lastNano = frameTimeNanos
        nativeRenderFrame(nativeHandle, dt)

        // Continue VSync loop only while dragging, decaying inertia, or animating
        if (nativeNeedsRender(nativeHandle)) {
            Choreographer.getInstance().postFrameCallback(this)
        } else {
            choreographerScheduled = false
        }
    }

    // ── View Visibility & Lifecycle ─────────────────────────────────────────

    override fun onWindowVisibilityChanged(visibility: Int) {
        super.onWindowVisibilityChanged(visibility)
        if (visibility == View.VISIBLE) {
            startRenderLoop()
        } else {
            stopRenderLoop()
        }
    }

    override fun onDetachedFromWindow() {
        stopRenderLoop()
        super.onDetachedFromWindow()
    }

    fun onResume() {
        startRenderLoop()
    }

    fun onPause() {
        stopRenderLoop()
    }

    fun onDestroyView() {
        isDestroyed = true
        stopRenderLoop()
        if (nativeHandle != 0L) {
            nativeDestroy(nativeHandle)
            nativeHandle = 0L
        }
    }

    // ── Touch input ─────────────────────────────────────────────────────────

    private var wasScaling = false

    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (!touchEnabled) {
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN -> { isClickGesture = true; lastTouchX = e.x; lastTouchY = e.y }
                MotionEvent.ACTION_MOVE -> {
                    if (Math.abs(e.x - lastTouchX) > 10 || Math.abs(e.y - lastTouchY) > 10)
                        isClickGesture = false
                }
                MotionEvent.ACTION_UP -> if (isClickGesture) onTapListener?.invoke()
            }
            return true
        }

        scaleDetector.onTouchEvent(e)

        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                lastTouchX = e.x; lastTouchY = e.y
                wasScaling = false
                if (nativeHandle != 0L) {
                    nativeDragBegin(nativeHandle, e.x, e.y)
                    startRenderLoop()
                }
            }
            MotionEvent.ACTION_MOVE -> {
                if (scaleDetector.isInProgress) {
                    wasScaling = true
                } else if (e.pointerCount == 1) {
                    if (wasScaling) {
                        wasScaling = false
                        if (nativeHandle != 0L) {
                            nativeDragBegin(nativeHandle, e.x, e.y)
                            startRenderLoop()
                        }
                    } else {
                        if (nativeHandle != 0L) {
                            nativeDragMove(nativeHandle, e.x, e.y)
                            startRenderLoop()
                        }
                    }
                }
                lastTouchX = e.x; lastTouchY = e.y
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                if (nativeHandle != 0L) {
                    nativeDragEnd(nativeHandle)
                    startRenderLoop()
                }
            }
        }
        return true
    }

    private fun zoom(delta: Float) {
        if (nativeHandle != 0L) {
            nativeZoom(nativeHandle, delta)
            startRenderLoop()
        }
    }

    // ── Public API (mirrors TwizzleGLSurfaceView) ─────────────────────────

    fun applyAlgorithm(alg: String) {
        if (nativeHandle != 0L) {
            nativeApplyAlgorithm(nativeHandle, alg)
            startRenderLoop()
        } else {
            pendingAlgorithm = alg
        }
    }

    fun reset() {
        if (nativeHandle != 0L) {
            nativeReset(nativeHandle)
            startRenderLoop()
        }
    }

    fun isAnimating(): Boolean = nativeHandle != 0L && nativeIsAnimating(nativeHandle)

    fun play() {
        if (nativeHandle != 0L) {
            nativePlay(nativeHandle)
            startRenderLoop()
        }
    }

    fun pause() {
        if (nativeHandle != 0L) {
            nativePause(nativeHandle)
            startRenderLoop()
        }
    }

    fun stepForward() {
        if (nativeHandle != 0L) {
            nativeStepForward(nativeHandle)
            startRenderLoop()
        }
    }

    fun stepBackward() {
        if (nativeHandle != 0L) {
            nativeStepBackward(nativeHandle)
            startRenderLoop()
        }
    }

    fun isPlaying(): Boolean = nativeHandle != 0L && nativeIsPlaying(nativeHandle)

    fun setSpeed(speed: Float) {
        if (nativeHandle != 0L) {
            nativeSetSpeed(nativeHandle, speed)
            startRenderLoop()
        } else {
            pendingSpeed = speed
        }
    }
    fun getSpeed(): Float = if (nativeHandle != 0L) nativeGetSpeed(nativeHandle) else 1.0f

    fun setGLBackgroundColor(r: Float, g: Float, b: Float, a: Float) {
        if (nativeHandle != 0L) {
            nativeSetBackgroundColor(nativeHandle, r, g, b, a)
            startRenderLoop()
        } else {
            pendingBg = floatArrayOf(r, g, b, a)
        }
    }

    fun setCameraPosition(lat: Float, lon: Float, rad: Float) {
        if (nativeHandle != 0L) {
            nativeSetCameraPosition(nativeHandle, lat, lon, rad)
            startRenderLoop()
        } else {
            pendingCamera = floatArrayOf(lat, lon, rad)
        }
    }

    fun setShowHint(enabled: Boolean) {
        if (nativeHandle != 0L) {
            nativeSetShowHint(nativeHandle, enabled)
            startRenderLoop()
        } else {
            pendingShowHint = enabled
        }
    }

    fun setShowFaceLetters(enabled: Boolean) {
        if (nativeHandle != 0L) {
            nativeSetShowFaceLetters(nativeHandle, enabled)
            startRenderLoop()
        } else {
            pendingShowFaceLetters = enabled
        }
    }

    fun setFaceColors(colors: FloatArray) {
        if (nativeHandle != 0L) {
            nativeSetFaceColors(nativeHandle, colors)
            startRenderLoop()
        } else {
            pendingFaceColors = colors
        }
    }

    fun setBodyAlpha(alpha: Float) {
        if (nativeHandle != 0L) {
            nativeSetBodyAlpha(nativeHandle, alpha)
            startRenderLoop()
        } else {
            pendingBodyAlpha = alpha
        }
    }

    fun seekFraction(f: Float) {
        if (nativeHandle != 0L) {
            nativeSeekFraction(nativeHandle, f)
            startRenderLoop()
        }
    }
    fun getCurrentFraction(): Float = if (nativeHandle != 0L) nativeCurrentFraction(nativeHandle) else 0f

    fun setPitchLock(locked: Boolean) {
        if (nativeHandle != 0L) {
            nativeSetPitchLock(nativeHandle, locked)
            startRenderLoop()
        } else {
            pendingPitchLock = locked
        }
    }

    fun setDebugLogs(enabled: Boolean) {
        if (nativeHandle != 0L) nativeSetDebugLogs(nativeHandle, enabled)
        else pendingDebugLogs = enabled
    }

    // ── JNI declarations ────────────────────────────────────────────────────

    private external fun nativeCreate(surface: Surface, width: Int, height: Int): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeResize(handle: Long, width: Int, height: Int)
    private external fun nativeRenderFrame(handle: Long, dt: Float)

    private external fun nativeDragBegin(handle: Long, x: Float, y: Float)
    private external fun nativeDragMove(handle: Long, x: Float, y: Float)
    private external fun nativeDragEnd(handle: Long)
    private external fun nativeZoom(handle: Long, delta: Float)

    private external fun nativeApplyAlgorithm(handle: Long, alg: String)
    private external fun nativeReset(handle: Long)
    private external fun nativeIsAnimating(handle: Long): Boolean
    private external fun nativeNeedsRender(handle: Long): Boolean
    private external fun nativePlay(handle: Long)
    private external fun nativePause(handle: Long)
    private external fun nativeStepForward(handle: Long)
    private external fun nativeStepBackward(handle: Long)
    private external fun nativeIsPlaying(handle: Long): Boolean

    private external fun nativeSeekFraction(handle: Long, f: Float)
    private external fun nativeCurrentFraction(handle: Long): Float

    private external fun nativeSetSpeed(handle: Long, speed: Float)
    private external fun nativeGetSpeed(handle: Long): Float
    private external fun nativeSetBackgroundColor(handle: Long, r: Float, g: Float, b: Float, a: Float)
    private external fun nativeSetCameraPosition(handle: Long, lat: Float, lon: Float, rad: Float)
    private external fun nativeSetShowHint(handle: Long, enabled: Boolean)
    private external fun nativeSetShowFaceLetters(handle: Long, enabled: Boolean)
    private external fun nativeSetFaceColors(handle: Long, colors: FloatArray)
    private external fun nativeSetBodyAlpha(handle: Long, alpha: Float)
    private external fun nativeSetPitchLock(handle: Long, locked: Boolean)
    private external fun nativeSetDebugLogs(handle: Long, enabled: Boolean)

    companion object {
        init { System.loadLibrary("twizzle") }
    }
}
